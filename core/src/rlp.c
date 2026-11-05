#include "okupa_rlp.h"
#include <string.h>

void ok_rlp_init(ok_rlp_writer *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = buf ? cap : 0;
    w->len = 0;
    w->overflow = 0;
}

static void put(ok_rlp_writer *w, const uint8_t *data, size_t n)
{
    if (w->overflow || n > w->cap - w->len) {
        w->overflow = 1;
        return;
    }
    if (n) memcpy(w->buf + w->len, data, n);
    w->len += n;
}

/* Header for a payload of n bytes: short form below 56, long form above. */
static size_t header(uint8_t out[9], uint8_t base, size_t n)
{
    size_t width = 0, i;
    if (n < 56) {
        out[0] = (uint8_t)(base + n);
        return 1;
    }
    for (i = n; i; i >>= 8) width++;
    out[0] = (uint8_t)(base + 55 + width);
    for (i = 0; i < width; i++) out[width - i] = (uint8_t)(n >> (8 * i));
    return width + 1;
}

void ok_rlp_bytes(ok_rlp_writer *w, const uint8_t *data, size_t n)
{
    uint8_t h[9];
    if (n == 1 && data[0] < 0x80) {
        put(w, data, 1);
        return;
    }
    put(w, h, header(h, 0x80, n));
    put(w, data, n);
}

void ok_rlp_uint_be(ok_rlp_writer *w, const uint8_t *be, size_t n)
{
    while (n && !*be) {
        be++;
        n--;
    }
    ok_rlp_bytes(w, be, n);
}

void ok_rlp_uint(ok_rlp_writer *w, uint64_t v)
{
    uint8_t be[8];
    int i;
    for (i = 7; i >= 0; i--, v >>= 8) be[i] = (uint8_t)v;
    ok_rlp_uint_be(w, be, sizeof be);
}

size_t ok_rlp_list_begin(ok_rlp_writer *w)
{
    return w->len;
}

void ok_rlp_list_end(ok_rlp_writer *w, size_t mark)
{
    uint8_t h[9];
    size_t payload, n;
    if (w->overflow) return;
    payload = w->len - mark;
    n = header(h, 0xc0, payload);
    if (n > w->cap - w->len) {
        w->overflow = 1;
        return;
    }
    memmove(w->buf + mark + n, w->buf + mark, payload);
    memcpy(w->buf + mark, h, n);
    w->len += n;
}

ok_result ok_rlp_done(const ok_rlp_writer *w, size_t *len)
{
    if (!w->buf || w->overflow) return OK_RANGE;
    if (len) *len = w->len;
    return OK_OK;
}
