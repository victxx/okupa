#include "okupa_balance.h"
#include "okupa_store.h"
#include <stdio.h>
#include <string.h>

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The quantity as 32 big-endian bytes; 0 if it is not one. */
static int wei_bytes(const char *s, uint8_t v[32])
{
    size_t n, i;
    if (!s || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return 0;
    s += 2;
    n = strlen(s);
    if (!n || n > 64) return 0;
    memset(v, 0, 32);
    for (i = 0; i < n; i++) {
        int d = hexval(s[n - 1 - i]);
        if (d < 0) return 0;
        v[31 - i / 2] |= (uint8_t)(i % 2 ? d << 4 : d);
    }
    return 1;
}

ok_result ok_wei_to_eth(const char *hex0x, char *out, size_t cap)
{
    uint8_t v[32];
    char digits[80];
    int nd = 0, i, zero, k;
    if (!out || !cap || !wei_bytes(hex0x, v)) return OK_FORMAT;
    /* To decimal: divide the 256-bit number by ten until nothing is left. */
    do {
        unsigned rem = 0;
        zero = 1;
        for (i = 0; i < 32; i++) {
            unsigned cur = rem << 8 | v[i];
            v[i] = (uint8_t)(cur / 10);
            rem = cur % 10;
            if (v[i]) zero = 0;
        }
        digits[nd++] = (char)('0' + rem);
    } while (!zero);
    while (nd < 19) digits[nd++] = '0'; /* at least one digit before the 18 decimals */
    /* digits holds the number least significant first: the ether before the
     * 18th digit, then six of the decimals (the rest is cut: rounded down). */
    k = 0;
    for (i = nd - 1; i >= 18 && k + 1 < (int)sizeof digits; i--) {
        if (k + 1 >= (int)cap) return OK_RANGE;
        out[k++] = digits[i];
    }
    if ((size_t)k + 12 > cap) return OK_RANGE;
    out[k++] = '.';
    for (i = 17; i >= 12; i--) out[k++] = digits[i];
    memcpy(out + k, " ETH", 5);
    return OK_OK;
}

static int quantity64(const ok_json *v, uint64_t *out)
{
    char s[80];
    size_t n, i;
    uint64_t x = 0;
    if (ok_json_string(v, s, sizeof s, &n) != OK_OK || n < 3 || s[0] != '0' || s[1] != 'x' || n > 18) return 0;
    for (i = 2; i < n; i++) {
        int d = hexval(s[i]);
        if (d < 0) return 0;
        x = x << 4 | (uint64_t)d;
    }
    *out = x;
    return 1;
}

ok_result ok_balance_ask(const char *host, const char *address0x, ok_think_fetch_fn fetch, void *ctx, char *buf,
                         size_t cap, ok_balance *b)
{
    char body[200], wei[80];
    ok_json result;
    uint8_t v[32];
    size_t n, i;
    int k;
    if (!b) return OK_ARGUMENT;
    memset(b, 0, sizeof *b);
    if (!host || !host[0] || !address0x || !fetch || !buf || cap < 64 || strlen(address0x) != 42 || address0x[0] != '0' ||
        address0x[1] != 'x') {
        snprintf(b->error, sizeof b->error, "bad address or host");
        return OK_ARGUMENT;
    }
    for (i = 2; i < 42; i++)
        if (hexval(address0x[i]) < 0) {
            snprintf(b->error, sizeof b->error, "bad address");
            return OK_ARGUMENT;
        }
    k = snprintf(body, sizeof body, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":[]}");
    if (!ok_jsonrpc_call(fetch, ctx, host, body, (size_t)k, buf, cap, &result, b->error, sizeof b->error)) return OK_OK;
    if (!quantity64(&result, &b->block)) {
        snprintf(b->error, sizeof b->error, "odd block number");
        return OK_OK;
    }
    k = snprintf(body, sizeof body,
                 "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"eth_getBalance\",\"params\":[\"%s\",\"0x%llx\"]}", address0x,
                 (unsigned long long)b->block);
    if (!ok_jsonrpc_call(fetch, ctx, host, body, (size_t)k, buf, cap, &result, b->error, sizeof b->error)) return OK_OK;
    if (ok_json_string(&result, wei, sizeof wei, &n) != OK_OK || ok_wei_to_eth(wei, b->eth, sizeof b->eth) != OK_OK ||
        !wei_bytes(wei, v)) {
        b->eth[0] = 0;
        snprintf(b->error, sizeof b->error, "odd balance");
        return OK_OK;
    }
    {
        uint64_t low = 0;
        int big = 0;
        for (i = 0; i < 24; i++) big |= v[i];
        for (i = 24; i < 32; i++) low = low << 8 | v[i];
        b->low = !big && low < OK_BALANCE_LOW_WEI;
    }
    return OK_OK;
}
