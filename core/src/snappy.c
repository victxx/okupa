#include "okupa_snappy.h"
#include <string.h>

#define CHUNK 65536 /* longest literal the encoder writes (2-byte length) */

static ok_result varint(const uint8_t *in, size_t n, uint32_t *v, size_t *used)
{
    uint32_t x = 0;
    size_t i;
    for (i = 0; i < n && i < 5; i++) {
        if (i == 4 && in[i] > 0x0f) return OK_FORMAT; /* more than 32 bits */
        x |= (uint32_t)(in[i] & 0x7f) << (7 * i);
        if (!(in[i] & 0x80)) {
            *v = x;
            *used = i + 1;
            return OK_OK;
        }
    }
    return OK_FORMAT;
}

ok_result ok_snappy_length(const uint8_t *in, size_t n, size_t *len)
{
    uint32_t v;
    size_t used;
    ok_result r;
    if (!in || !len) return OK_ARGUMENT;
    if ((r = varint(in, n, &v, &used)) != OK_OK) return r;
    *len = v;
    return OK_OK;
}

static uint32_t le(const uint8_t *p, size_t k)
{
    uint32_t v = 0;
    while (k--) v = v << 8 | p[k];
    return v;
}

ok_result ok_snappy_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *len)
{
    uint32_t want;
    size_t i, o = 0, used;
    ok_result r;
    if (!in || !out || !len) return OK_ARGUMENT;
    if ((r = varint(in, n, &want, &used)) != OK_OK) return r;
    if (want > cap) return OK_RANGE;
    for (i = used; i < n;) {
        uint8_t tag = in[i++];
        size_t length, offset, k;
        if ((tag & 3) == 0) { /* literal */
            length = tag >> 2;
            if (length >= 60) {
                k = length - 59;
                if (n - i < k) return OK_FORMAT;
                length = le(in + i, k);
                i += k;
            }
            if (length >= n - i || length >= want - o) return OK_FORMAT; /* needs length + 1 */
            length += 1;
            memcpy(out + o, in + i, length);
            i += length;
            o += length;
            continue;
        }
        if ((tag & 3) == 1) {
            if (n - i < 1) return OK_FORMAT;
            length = 4 + ((tag >> 2) & 7);
            offset = (size_t)(tag >> 5) << 8 | in[i];
            i += 1;
        } else {
            k = (tag & 3) == 2 ? 2 : 4;
            if (n - i < k) return OK_FORMAT;
            length = (size_t)(tag >> 2) + 1;
            offset = le(in + i, k);
            i += k;
        }
        if (offset == 0 || offset > o || want - o < length) return OK_FORMAT;
        for (k = 0; k < length; k++, o++) out[o] = out[o - offset]; /* may overlap */
    }
    if (o != want) return OK_FORMAT;
    *len = o;
    return OK_OK;
}

size_t ok_snappy_max_encoded(size_t n)
{
    return 5 + n + 3 * ((n + CHUNK - 1) / CHUNK);
}

ok_result ok_snappy_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *len)
{
    size_t o = 0, i = 0, x = n;
    if ((!in && n) || !out || !len) return OK_ARGUMENT;
    if (n > 0xffffffffu || n > (SIZE_MAX - 5) / 2) return OK_RANGE;
    if (cap < ok_snappy_max_encoded(n)) return OK_RANGE;
    do {
        out[o++] = (uint8_t)((x & 0x7f) | (x > 0x7f ? 0x80 : 0));
        x >>= 7;
    } while (x);
    while (i < n) {
        size_t k = n - i < CHUNK ? n - i : CHUNK;
        if (k <= 60) {
            out[o++] = (uint8_t)((k - 1) << 2);
        } else if (k <= 256) {
            out[o++] = 60 << 2;
            out[o++] = (uint8_t)(k - 1);
        } else {
            out[o++] = 61 << 2;
            out[o++] = (uint8_t)(k - 1);
            out[o++] = (uint8_t)((k - 1) >> 8);
        }
        memcpy(out + o, in + i, k);
        o += k;
        i += k;
    }
    *len = o;
    return OK_OK;
}
