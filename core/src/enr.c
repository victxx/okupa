#include "okupa_enr.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_rlp.h"
#include "secp256k1.h"
#include <string.h>

static int b64(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

ok_result ok_base64url_decode(const char *in, size_t n, uint8_t *out, size_t cap, size_t *len)
{
    uint32_t acc = 0;
    size_t i, o = 0;
    int bits = 0;
    if ((!in && n) || !out || !len) return OK_ARGUMENT;
    if (n % 4 == 1) return OK_FORMAT;
    for (i = 0; i < n; i++) {
        int v = b64(in[i]);
        if (v < 0) return OK_FORMAT;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return OK_RANGE;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    if (acc & ((1u << bits) - 1)) return OK_FORMAT; /* leftover bits must be zero */
    *len = o;
    return OK_OK;
}

static int key_is(const ok_rlp_item *k, const char *name)
{
    return !k->list && k->len == strlen(name) && !memcmp(k->data, name, k->len);
}

/* Strictly increasing keys, compared as byte strings. */
static int before(const ok_rlp_item *a, const ok_rlp_item *b)
{
    size_t n = a->len < b->len ? a->len : b->len;
    int c = memcmp(a->data, b->data, n);
    return c < 0 || (c == 0 && a->len < b->len);
}

static int port(const ok_rlp_item *v, uint16_t *out)
{
    uint64_t p;
    if (ok_rlp_to_uint(v, &p) != OK_OK || p > 65535) return 0;
    *out = (uint16_t)p;
    return 1;
}

/* eth = [[fork hash, fork next], ...]: the first entry is the current one. */
static int eth_entry(const ok_rlp_item *v, ok_enr *enr)
{
    ok_rlp_item entry, item;
    size_t pos = 0, epos = 0;
    if (!v->list || ok_rlp_next(v, &pos, &entry) != OK_OK || !entry.list) return 0;
    if (ok_rlp_next(&entry, &epos, &item) != OK_OK || item.list || item.len != 4) return 0;
    memcpy(enr->fork_hash, item.data, 4);
    return ok_rlp_next(&entry, &epos, &item) == OK_OK && ok_rlp_to_uint(&item, &enr->fork_next) == OK_OK;
}

ok_result ok_enr_parse(const uint8_t *raw, size_t n, ok_enr *enr)
{
    const secp256k1_context *ctx = ok_secp();
    secp256k1_ecdsa_signature sig;
    secp256k1_pubkey key;
    ok_rlp_item list, s, seq, k, v, prev;
    uint8_t head[9], digest[32], full[65];
    size_t pos = 0, content, hn, full_len = sizeof full;
    int have_prev = 0, v4 = 0, have_key = 0;
    ok_keccak h;
    if (!raw || !enr) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    if (n > OK_ENR_MAX || ok_rlp_decode(raw, n, &list) != OK_OK || !list.list) return OK_FORMAT;
    memset(enr, 0, sizeof *enr);
    if (ok_rlp_next(&list, &pos, &s) != OK_OK || s.list || s.len != 64) return OK_FORMAT;
    content = pos; /* the signed content is the list without its first item */
    if (ok_rlp_next(&list, &pos, &seq) != OK_OK || ok_rlp_to_uint(&seq, &enr->seq) != OK_OK) return OK_FORMAT;
    while (ok_rlp_next(&list, &pos, &k) == OK_OK) {
        if (k.list || ok_rlp_next(&list, &pos, &v) != OK_OK) return OK_FORMAT;
        if (have_prev && !before(&prev, &k)) return OK_FORMAT;
        prev = k;
        have_prev = 1;
        if (key_is(&k, "id")) {
            v4 = !v.list && v.len == 2 && !memcmp(v.data, "v4", 2);
        } else if (key_is(&k, "secp256k1")) {
            if (v.list || v.len != 33 || !secp256k1_ec_pubkey_parse(ctx, &key, v.data, 33)) return OK_FORMAT;
            have_key = 1;
        } else if (key_is(&k, "ip")) {
            if (v.list || v.len != 4) return OK_FORMAT;
            memcpy(enr->ip, v.data, 4);
            enr->has_ip = 1;
        } else if (key_is(&k, "tcp")) {
            if (!port(&v, &enr->tcp)) return OK_FORMAT;
        } else if (key_is(&k, "udp")) {
            if (!port(&v, &enr->udp)) return OK_FORMAT;
        } else if (key_is(&k, "eth")) {
            enr->has_eth = eth_entry(&v, enr);
        }
    }
    if (!v4 || !have_key) return OK_FORMAT;

    /* keccak(rlp([seq, k, v, ...])), signed as a plain 64-byte r || s. */
    hn = list.len - content;
    if (hn < 56) {
        head[0] = (uint8_t)(0xc0 + hn);
        hn = 1;
    } else {
        size_t len = hn, bytes = 0, i;
        while (len) {
            bytes++;
            len >>= 8;
        }
        head[0] = (uint8_t)(0xf7 + bytes);
        for (i = 0; i < bytes; i++) head[1 + i] = (uint8_t)(hn >> (8 * (bytes - 1 - i)));
        hn = 1 + bytes;
    }
    ok_keccak_init(&h);
    ok_keccak_update(&h, head, hn);
    ok_keccak_update(&h, list.data + content, list.len - content);
    ok_keccak_final(&h, digest);
    if (!secp256k1_ecdsa_signature_parse_compact(ctx, &sig, s.data)) return OK_CRYPTO;
    secp256k1_ecdsa_signature_normalize(ctx, &sig, &sig);
    if (!secp256k1_ecdsa_verify(ctx, &sig, digest, &key)) return OK_CRYPTO;
    secp256k1_ec_pubkey_serialize(ctx, full, &full_len, &key, SECP256K1_EC_UNCOMPRESSED);
    memcpy(enr->pub, full + 1, 64);
    return OK_OK;
}

ok_result ok_enr_parse_text(const char *text, size_t n, ok_enr *enr)
{
    uint8_t raw[OK_ENR_MAX];
    size_t len;
    ok_result r;
    if (!text || !enr) return OK_ARGUMENT;
    if (n < 4 || memcmp(text, "enr:", 4)) return OK_FORMAT;
    r = ok_base64url_decode(text + 4, n - 4, raw, sizeof raw, &len);
    if (r == OK_RANGE) return OK_FORMAT;
    return r != OK_OK ? r : ok_enr_parse(raw, len, enr);
}
