#include "okupa_film.h"
#include <string.h>

static uint32_t u16le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32le(const uint8_t *p) { return u16le(p) | u16le(p + 2) << 16; }

ok_result ok_film_open(ok_film *f, const uint8_t *file, size_t n)
{
    size_t table;
    int k;
    if (!f || !file) return OK_ARGUMENT;
    memset(f, 0, sizeof *f);
    if (n < OK_FILM_HEADER || memcmp(file, "OKFM", 4) || file[4] != 1) return OK_FORMAT;
    f->width = (int)u16le(file + 5);
    f->height = (int)u16le(file + 7);
    f->frames = (int)u16le(file + 9);
    f->ticks = file[11];
    f->first_row = file[12];
    f->palette = file + 13;
    if (!f->width || !f->height || !f->frames || !f->ticks || f->width > 256 || f->first_row + f->height > 192)
        return OK_FORMAT;
    table = (size_t)(f->frames + 1) * 4;
    if (n < OK_FILM_HEADER + table) return OK_FORMAT;
    f->offsets = file + OK_FILM_HEADER;
    f->data = f->offsets + table;
    f->data_len = n - OK_FILM_HEADER - table;
    for (k = 0; k < f->frames; k++) /* in order, inside the data, each with its kind byte */
        if (u32le(f->offsets + 4 * k) >= u32le(f->offsets + 4 * (k + 1)) || u32le(f->offsets + 4 * (k + 1)) > f->data_len)
            return OK_FORMAT;
    if (f->data[u32le(f->offsets)] != 0) return OK_FORMAT; /* the first frame is whole */
    return OK_OK;
}

ok_result ok_film_lz_unpack(const uint8_t *in, size_t n, uint8_t *out, size_t want)
{
    size_t i = 0, o = 0;
    while (o < want) {
        unsigned flags, b;
        if (i >= n) return OK_FORMAT;
        flags = in[i++];
        for (b = 0; b < 8 && o < want; b++, flags <<= 1) {
            if (flags & 0x80) {
                unsigned len, dist;
                if (i + 2 > n) return OK_FORMAT;
                len = (unsigned)(in[i] >> 4) + 3;
                dist = ((unsigned)(in[i] & 15) << 8 | in[i + 1]) + 1;
                i += 2;
                if (len == 18) { /* a long one: one more byte of length */
                    if (i >= n) return OK_FORMAT;
                    len += in[i++];
                }
                if (dist > o || o + len > want) return OK_FORMAT;
                while (len--) out[o] = out[o - dist], o++;
            } else {
                if (i >= n) return OK_FORMAT;
                out[o++] = in[i++];
            }
        }
    }
    return i == n ? OK_OK : OK_FORMAT;
}

/* Greedy, looking back along a chain of earlier places with the same three
 * bytes (the computer does this once; the console only unpacks). */
size_t ok_film_lz_pack(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    static int32_t head[1 << 15], prev[1 << 17];
    size_t i = 0, o = 0, flag_at = 0;
    int bit = 8;
    if (n > (1u << 17)) return 0;
    memset(head, 0xff, sizeof head);
    while (i < n) {
        size_t best = 0, best_d = 0;
        if (bit == 8) {
            if (o >= cap) return 0;
            flag_at = o++;
            out[flag_at] = 0;
            bit = 0;
        }
        if (i + 3 <= n) {
            uint32_t h = ((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & ((1 << 15) - 1);
            int32_t j = head[h];
            int tries = 64;
            while (j >= 0 && i - (size_t)j <= 4096 && tries--) {
                size_t l = 0;
                while (l < 273 && i + l < n && in[(size_t)j + l] == in[i + l]) l++;
                if (l > best) best = l, best_d = i - (size_t)j;
                if (l == 273) break;
                j = prev[j];
            }
            prev[i] = head[h];
            head[h] = (int32_t)i;
        }
        if (best >= 3) {
            size_t k;
            if (o + 3 > cap) return 0;
            out[flag_at] |= (uint8_t)(0x80 >> bit);
            out[o++] = (uint8_t)((best >= 18 ? 15 : best - 3) << 4 | (best_d - 1) >> 8);
            out[o++] = (uint8_t)(best_d - 1);
            if (best >= 18) out[o++] = (uint8_t)(best - 18);
            for (k = 1; k < best; k++) { /* keep the chains for the bytes skipped */
                size_t p = i + k;
                if (p + 3 <= n) {
                    uint32_t h = ((uint32_t)in[p] << 10 ^ (uint32_t)in[p + 1] << 5 ^ in[p + 2]) & ((1 << 15) - 1);
                    prev[p] = head[h];
                    head[h] = (int32_t)p;
                }
            }
            i += best;
        } else {
            if (o >= cap) return 0;
            out[o++] = in[i++];
        }
        bit++;
    }
    return o;
}

ok_result ok_film_frame(const ok_film *f, int k, uint8_t *pixels)
{
    static uint8_t diff[256 * 192];
    size_t at, end, n = (size_t)f->width * (size_t)f->height, i;
    ok_result r;
    if (!f || !pixels || k < 0 || k >= f->frames) return OK_ARGUMENT;
    at = u32le(f->offsets + 4 * k);
    end = u32le(f->offsets + 4 * (k + 1));
    if (f->data[at] == 0) return ok_film_lz_unpack(f->data + at + 1, end - at - 1, pixels, n);
    if (f->data[at] != 1) return OK_FORMAT;
    if ((r = ok_film_lz_unpack(f->data + at + 1, end - at - 1, diff, n)) != OK_OK) return r;
    for (i = 0; i < n; i++) pixels[i] ^= diff[i];
    return OK_OK;
}

void ok_film_palette(const ok_film *f, uint16_t to, int k, uint16_t out[256])
{
    int i;
    for (i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)u16le(f->palette + 2 * i);
        int r = (c & 31) + ((to & 31) - (c & 31)) * k / 1000, g = (c >> 5 & 31) + ((to >> 5 & 31) - (c >> 5 & 31)) * k / 1000,
            b = (c >> 10 & 31) + ((to >> 10 & 31) - (c >> 10 & 31)) * k / 1000;
        out[i] = (uint16_t)(0x8000u | r | g << 5 | b << 10);
    }
}
