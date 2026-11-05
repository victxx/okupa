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

ok_result ok_rlp_item_at(const uint8_t *in, size_t n, ok_rlp_item *item, size_t *used)
{
    uint8_t b;
    size_t head = 1, len = 0, width, i;
    if (!in || !item || !used || !n) return OK_ARGUMENT;
    b = in[0];
    item->list = b >= 0xc0;
    if (b < 0x80) {
        item->data = in;
        item->len = 1;
        *used = 1;
        return OK_OK;
    }
    if (b <= 0xb7 || (b >= 0xc0 && b <= 0xf7)) {
        len = b - (item->list ? 0xc0 : 0x80);
    } else {
        width = b - (item->list ? 0xf7 : 0xb7);
        if (width > sizeof(size_t) || width >= n) return OK_FORMAT;
        if (in[1] == 0) return OK_FORMAT;            /* leading zero in the length */
        for (i = 0; i < width; i++) len = (len << 8) | in[1 + i];
        if (len < 56) return OK_FORMAT;              /* should have been short form */
        head += width;
    }
    if (len > n - head) return OK_FORMAT;
    if (!item->list && len == 1 && in[head] < 0x80) return OK_FORMAT; /* single byte is its own encoding */
    item->data = in + head;
    item->len = len;
    *used = head + len;
    return OK_OK;
}

/* Nested lists must be well formed all the way down, within a fixed depth. */
#define OK_RLP_MAX_DEPTH 16
static ok_result check_list(const ok_rlp_item *list, unsigned depth)
{
    ok_rlp_item child;
    size_t pos = 0, used;
    ok_result r;
    if (depth > OK_RLP_MAX_DEPTH) return OK_FORMAT;
    while (pos < list->len) {
        r = ok_rlp_item_at(list->data + pos, list->len - pos, &child, &used);
        if (r != OK_OK) return OK_FORMAT;
        if (child.list && (r = check_list(&child, depth + 1)) != OK_OK) return r;
        pos += used;
    }
    return OK_OK;
}

ok_result ok_rlp_decode(const uint8_t *in, size_t n, ok_rlp_item *item)
{
    size_t used;
    ok_result r = ok_rlp_item_at(in, n, item, &used);
    if (r != OK_OK) return r;
    if (used != n) return OK_FORMAT;
    return item->list ? check_list(item, 1) : OK_OK;
}

ok_result ok_rlp_next(const ok_rlp_item *list, size_t *pos, ok_rlp_item *child)
{
    size_t used;
    ok_result r;
    if (!list || !pos || !child || !list->list || *pos > list->len) return OK_ARGUMENT;
    if (*pos == list->len) return OK_RANGE;
    r = ok_rlp_item_at(list->data + *pos, list->len - *pos, child, &used);
    if (r == OK_OK) *pos += used;
    return r;
}

ok_result ok_rlp_to_uint(const ok_rlp_item *item, uint64_t *v)
{
    size_t i;
    if (!item || !v) return OK_ARGUMENT;
    if (item->list || item->len > 8) return OK_FORMAT;
    if (item->len && item->data[0] == 0) return OK_FORMAT;
    *v = 0;
    for (i = 0; i < item->len; i++) *v = (*v << 8) | item->data[i];
    return OK_OK;
}
