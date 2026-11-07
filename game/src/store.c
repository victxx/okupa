#include "okupa_store.h"
#include "okupa_http.h"
#include "okupa_json.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_tx.h"
#include <stdio.h>
#include <string.h>

static char wire[16384];

static void hex(const uint8_t *in, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = 0;
}

static void say(ok_store *s, void (*log)(const char *), const char *fmt, const char *a)
{
    snprintf(s->step, sizeof s->step, fmt, a);
    if (log) log(s->step);
}

int ok_jsonrpc_call(ok_think_fetch_fn fetch, void *ctx, const char *host, const char *body, size_t body_len, char *buf,
                    size_t cap, ok_json *result, char *error, size_t error_cap)
{
    char head[512], headers[128];
    ok_http_response r;
    ok_json root, err;
    size_t n, got, len;
    int e;
    snprintf(headers, sizeof headers, "Content-Type: application/json\r\nContent-Length: %lu\r\n", (unsigned long)body_len);
    if (ok_http_request_alive("POST", host, "/", headers, NULL, 0, head, sizeof head, &n) != OK_OK) {
        snprintf(error, error_cap, "bad host");
        return 0;
    }
    e = fetch(ctx, host, head, n, body, body_len, buf, cap - 1, &got);
    if (e || ok_http_response_read(buf, got, &r, &len) != OK_OK || r.status != 200) {
        snprintf(error, error_cap, "%.20s: %s %d", host, e ? "net" : "http", e ? e : r.status);
        return 0;
    }
    r.body[len] = 0;
    if (ok_json_parse(r.body, len, &root) != OK_OK) {
        snprintf(error, error_cap, "bad JSON from %.20s", host);
        return 0;
    }
    if (ok_json_member(&root, "error", &err) == OK_OK) {
        ok_json m;
        char msg[48] = "";
        if (ok_json_member(&err, "message", &m) == OK_OK) ok_json_string(&m, msg, sizeof msg, &n);
        snprintf(error, error_cap, "%.60s", msg[0] ? msg : "RPC error");
        return 0;
    }
    if (ok_json_member(&root, "result", result) != OK_OK) {
        snprintf(error, error_cap, "no result from %.20s", host);
        return 0;
    }
    return 1;
}

/* One JSON-RPC call; *result is the "result" member of the answer. */
static int rpc(ok_store *s, ok_think_fetch_fn fetch, void *ctx, const char *host, const char *body, size_t body_len,
               ok_json *result)
{
    return ok_jsonrpc_call(fetch, ctx, host, body, body_len, wire, sizeof wire, result, s->error, sizeof s->error);
}

/* A "0x..." quantity (or 32-byte word) that fits in 64 bits. */
static int quantity(const ok_json *v, uint64_t *out)
{
    char s[80];
    size_t n, i;
    uint64_t x = 0;
    if (ok_json_string(v, s, sizeof s, &n) != OK_OK || n < 3 || s[0] != '0' || s[1] != 'x') return 0;
    for (i = 2; i < n; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0 || (x >> 60)) return 0;
        x = x << 4 | (uint64_t)d;
    }
    *out = x;
    return 1;
}

/* What the nodes know of the file: seen = some node has its submission (and
 * so its sequence); finalized = every shard has a node that finished it.
 * Which shard holds a segment depends on where the file sits in the whole
 * log, so the segment goes to all of them. */
static int node_info(ok_store *s, const ok_store_net *n, ok_think_fetch_fn http, void *ctx, int *seen, int *finalized)
{
    char body[200];
    ok_json r, tx, v;
    int k, any = 0, done[8] = {0}, shard;
    *seen = *finalized = 0;
    snprintf(body, sizeof body, "{\"jsonrpc\":\"2.0\",\"method\":\"zgs_getFileInfo\",\"params\":[\"%s\",false],\"id\":1}",
             s->root);
    for (k = 0; k < n->node_count; k++) {
        if (!rpc(s, http, ctx, n->node[k], body, strlen(body), &r)) continue;
        any = 1;
        if (r.len == 4 && !memcmp(r.at, "null", 4)) continue;
        if (ok_json_member(&r, "tx", &tx) == OK_OK && ok_json_member(&tx, "seq", &v) == OK_OK) {
            uint64_t seq = 0;
            size_t i;
            for (i = 0; i < v.len && v.at[i] >= '0' && v.at[i] <= '9'; i++) seq = seq * 10 + (uint64_t)(v.at[i] - '0');
            s->seq = seq;
            *seen = 1;
        }
        if (ok_json_member(&r, "finalized", &v) == OK_OK && v.len == 4 && !memcmp(v.at, "true", 4) &&
            n->node_shard[k] >= 0 && n->node_shard[k] < 8)
            done[n->node_shard[k]] = 1;
    }
    *finalized = n->shards > 0;
    for (shard = 0; shard < n->shards && shard < 8; shard++)
        if (!done[shard]) *finalized = 0;
    return any;
}

ok_result ok_store_put(ok_store *s, const ok_store_net *n, const uint8_t key[32], const uint8_t *data, size_t size,
                       char *buf, size_t cap, ok_think_fetch_fn https, ok_think_fetch_fn http, void *ctx,
                       void (*wait_ms)(int ms), void (*log)(const char *step))
{
    static uint8_t call[1024], raw[2048];
    char addr[43], fee_hex[24], call_hex[2100], sel_hex[9], word_hex[65];
    uint8_t pub[64], me[20], sel[32], hash[32];
    ok_json r;
    size_t len, rawlen;
    int k, seen, fin, ok_nodes = 0;
    ok_tx tx;

    if (!s) return OK_ARGUMENT;
    memset(s, 0, sizeof *s);
    s->stage = OK_STORE_SETUP;
    if (!n || !key || !data || !buf || !https || !http || !wait_ms || !n->node_count) return OK_ARGUMENT;
    if (ok_zgs_file_hash(data, size, &s->file) != OK_OK) return OK_RANGE;
    s->root[0] = '0', s->root[1] = 'x';
    hex(s->file.root, 32, s->root + 2);
    if (ok_keys_public(key, pub) != OK_OK) return OK_CRYPTO;
    ok_keys_address(pub, me);
    addr[0] = '0', addr[1] = 'x';
    hex(me, 20, addr + 2);
    say(s, log, "root %.24s", s->root);

    /* Already there? Then nothing to pay; already submitted, only the
     * segment is missing. */
    if (node_info(s, n, http, ctx, &seen, &fin) && seen && fin) {
        s->already = s->finalized = 1;
        s->stage = OK_STORE_DONE;
        say(s, log, "already stored %.20s", s->root);
        return OK_OK;
    }
    if (seen) {
        s->already = 1;
        say(s, log, "submitted before, uploading %.12s", s->root);
        goto upload;
    }

    /* 1. Nonce, gas price and the market's price per sector. */
    s->stage = OK_STORE_RPC;
    snprintf(buf, cap, "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getTransactionCount\",\"params\":[\"%s\",\"pending\"],\"id\":1}", addr);
    if (!rpc(s, https, ctx, n->rpc_host, buf, strlen(buf), &r) || !quantity(&r, &s->nonce)) return OK_OK;
    snprintf(buf, cap, "{\"jsonrpc\":\"2.0\",\"method\":\"eth_gasPrice\",\"params\":[],\"id\":1}");
    if (!rpc(s, https, ctx, n->rpc_host, buf, strlen(buf), &r) || !quantity(&r, &s->gas_price)) return OK_OK;
    ok_keccak256("pricePerSector()", 16, sel);
    hex(sel, 4, sel_hex);
    hex(n->market, 20, word_hex);
    snprintf(buf, cap,
             "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\",\"params\":[{\"to\":\"0x%s\",\"data\":\"0x%s\"},\"latest\"],\"id\":1}",
             word_hex, sel_hex);
    {
        uint64_t price = 0;
        char w[80];
        size_t m;
        if (!rpc(s, https, ctx, n->rpc_host, buf, strlen(buf), &r) || ok_json_string(&r, w, sizeof w, &m) != OK_OK ||
            m != 66) {
            snprintf(s->error, sizeof s->error, "no price per sector");
            return OK_OK;
        }
        /* The price is a 32-byte word: it must fit in the last 16 hex digits. */
        for (k = 2; k < 50; k++)
            if (w[k] != '0') {
                snprintf(s->error, sizeof s->error, "price too big");
                return OK_OK;
            }
        for (k = 50; k < 66; k++) price = price << 4 | (uint64_t)(w[k] <= '9' ? w[k] - '0' : (w[k] | 32) - 'a' + 10);
        s->fee = ok_zgs_fee(&s->file, price);
    }

    /* 2. The submission, estimated, signed here, sent. */
    if (ok_zgs_submit_calldata(&s->file, me, call, sizeof call, &len) != OK_OK) return OK_RANGE;
    call_hex[0] = '0', call_hex[1] = 'x';
    hex(call, len, call_hex + 2);
    snprintf(fee_hex, sizeof fee_hex, "0x%llx", (unsigned long long)s->fee);
    hex(n->flow, 20, word_hex);
    snprintf(buf, cap,
             "{\"jsonrpc\":\"2.0\",\"method\":\"eth_estimateGas\",\"params\":[{\"from\":\"%s\",\"to\":\"0x%s\",\"value\":\"%s\","
             "\"data\":\"%s\"}],\"id\":1}",
             addr, word_hex, fee_hex, call_hex);
    if (!rpc(s, https, ctx, n->rpc_host, buf, strlen(buf), &r) || !quantity(&r, &s->gas)) return OK_OK;
    memset(&tx, 0, sizeof tx);
    tx.chain_id = n->chain_id;
    tx.nonce = s->nonce;
    tx.max_priority_fee = s->gas_price;
    tx.max_fee = 2 * s->gas_price;
    tx.gas_limit = s->gas + s->gas / 3; /* a third more than estimated */
    memcpy(tx.to, n->flow, 20);
    tx.value = s->fee;
    tx.data = call;
    tx.data_len = len;
    if (ok_tx_sign(&tx, key, raw, sizeof raw, &rawlen, hash) != OK_OK) return OK_CRYPTO;
    s->stage = OK_STORE_TX;
    {
        static char rawhex[4200];
        rawhex[0] = '0', rawhex[1] = 'x';
        hex(raw, rawlen, rawhex + 2);
        snprintf(buf, cap, "{\"jsonrpc\":\"2.0\",\"method\":\"eth_sendRawTransaction\",\"params\":[\"%s\"],\"id\":1}", rawhex);
    }
    s->tx[0] = '0', s->tx[1] = 'x';
    hex(hash, 32, s->tx + 2);
    if (!rpc(s, https, ctx, n->rpc_host, buf, strlen(buf), &r)) return OK_OK;
    say(s, log, "submitted %.22s", s->tx);

    /* 3. Wait for a node to see it (it then knows the file's sequence). */
    s->stage = OK_STORE_WAIT;
    for (k = 0; k < 40; k++) {
        wait_ms(2000);
        if (node_info(s, n, http, ctx, &seen, &fin) && seen) break;
    }
    if (!seen) {
        snprintf(s->error, sizeof s->error, "nodes never saw it");
        return OK_OK;
    }
    if (fin) {
        s->finalized = 1;
        s->stage = OK_STORE_DONE;
        return OK_OK;
    }

upload:
    /* 4. The segment, to every node (each keeps what its shard holds). */
    s->stage = OK_STORE_UPLOAD;
    if (ok_zgs_upload_body(&s->file, data, s->seq, buf, cap, &len) != OK_OK) return OK_RANGE;
    for (k = 0; k < n->node_count; k++)
        if (rpc(s, http, ctx, n->node[k], buf, len, &r)) ok_nodes++;
    if (!ok_nodes) return OK_OK;
    say(s, log, "segment uploaded to %s", n->node_count > 1 ? "the nodes" : "the node");
    s->stage = OK_STORE_DONE;
    s->error[0] = 0; /* a node that holds another shard may refuse: it does not matter once one took it */
    for (k = 0; k < 15 && !s->finalized; k++) {
        wait_ms(2000);
        if (node_info(s, n, http, ctx, &seen, &fin) && fin) s->finalized = 1;
    }
    return OK_OK;
}
