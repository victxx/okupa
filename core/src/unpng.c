/* Reading a PNG on the console: inflate (RFC 1951, all three block types),
 * the zlib wrapper (RFC 1950) and PNG's scanline filters, for 8-bit grey,
 * RGB and RGBA pictures without interlacing. Written from the RFCs and the
 * PNG specification; no heap: the caller lends the scratch space. */
#include "okupa_png.h"
#include <string.h>

typedef struct {
    const uint8_t *in;
    size_t n, pos;
    uint32_t buf;
    int count, bad;
    uint8_t *out;
    size_t cap, len;
} inflater;

static int bits(inflater *s, int need)
{
    int v;
    while (s->count < need) {
        if (s->pos >= s->n) {
            s->bad = 1;
            return 0;
        }
        s->buf |= (uint32_t)s->in[s->pos++] << s->count;
        s->count += 8;
    }
    v = (int)(s->buf & ((1u << need) - 1));
    s->buf >>= need;
    s->count -= need;
    return v;
}

/* A canonical Huffman code: how many codes of each length, and the symbols
 * in code order. */
typedef struct {
    uint16_t count[16];
    uint16_t symbol[320];
} huffman;

/* 0 if the lengths make a usable code (complete, or a single code). */
static int build(huffman *h, const uint8_t *length, int n)
{
    uint16_t offs[16];
    int len, sym, left = 1;
    memset(h->count, 0, sizeof h->count);
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0; /* no codes: fine until one is used */
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1; /* over-subscribed */
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
    return 0;
}

static int decode(inflater *s, const huffman *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        int count;
        code |= bits(s, 1);
        if (s->bad) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->bad = 1;
    return -1;
}

static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                   193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(inflater *s, const huffman *lit, const huffman *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->len >= s->cap) return -1;
            s->out[s->len++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            size_t len, d;
            sym -= 257;
            if (sym >= 29) return -1;
            len = lbase[sym] + (size_t)bits(s, lext[sym]);
            sym = decode(s, dist);
            if (sym < 0 || sym >= 30) return -1;
            d = dbase[sym] + (size_t)bits(s, dext[sym]);
            if (s->bad || d > s->len || s->len + len > s->cap) return -1;
            for (; len; len--, s->len++) s->out[s->len] = s->out[s->len - d];
        }
    }
}

static int fixed(inflater *s)
{
    static huffman lit, dist;
    static int ready;
    if (!ready) {
        uint8_t l[288];
        int i;
        for (i = 0; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lit, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dist, l, 30);
        ready = 1;
    }
    return codes(s, &lit, &dist);
}

static int dynamic(inflater *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    static huffman lit, dist, lens;
    uint8_t length[320];
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4, i;
    if (s->bad || nlen > 286 || ndist > 30) return -1;
    memset(length, 0, sizeof length);
    for (i = 0; i < ncode; i++) length[order[i]] = (uint8_t)bits(s, 3);
    if (s->bad || build(&lens, length, 19)) return -1;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lens), rep, v = 0;
        if (sym < 0) return -1;
        if (sym < 16) {
            length[i++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16) {
            if (!i) return -1;
            v = length[i - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (s->bad || i + rep > nlen + ndist) return -1;
        while (rep--) length[i++] = (uint8_t)v;
    }
    if (!length[256]) return -1; /* no end-of-block code */
    if (build(&lit, length, nlen) || build(&dist, length + nlen, ndist)) return -1;
    return codes(s, &lit, &dist);
}

/* zlib stream in, bytes out; the Adler-32 at the end must match. */
static ok_result inflate_zlib(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *len)
{
    inflater s;
    uint32_t a = 1, b = 0, want;
    size_t i;
    int last;
    if (n < 6 || (in[0] & 15) != 8 || (in[0] >> 4) > 7 || ((in[0] << 8) | in[1]) % 31 || (in[1] & 32)) return OK_FORMAT;
    memset(&s, 0, sizeof s);
    s.in = in + 2;
    s.n = n - 2;
    s.out = out;
    s.cap = cap;
    do {
        int type;
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.bad) return OK_FORMAT;
        if (type == 0) { /* stored: from the next byte boundary */
            size_t k;
            s.buf = 0;
            s.count = 0;
            if (s.pos + 4 > s.n) return OK_FORMAT;
            k = (size_t)s.in[s.pos] | (size_t)s.in[s.pos + 1] << 8;
            if ((k ^ ((size_t)s.in[s.pos + 2] | (size_t)s.in[s.pos + 3] << 8)) != 0xffff) return OK_FORMAT;
            s.pos += 4;
            if (s.pos + k > s.n || s.len + k > s.cap) return OK_RANGE;
            memcpy(s.out + s.len, s.in + s.pos, k);
            s.pos += k;
            s.len += k;
        } else if (type == 1) {
            if (fixed(&s)) return s.len >= s.cap ? OK_RANGE : OK_FORMAT;
        } else if (type == 2) {
            if (dynamic(&s)) return s.len >= s.cap ? OK_RANGE : OK_FORMAT;
        } else {
            return OK_FORMAT;
        }
    } while (!last);
    /* The Adler-32 follows, byte aligned. */
    s.buf = 0;
    s.count = 0;
    if (s.pos + 4 > s.n) return OK_FORMAT;
    want = (uint32_t)s.in[s.pos] << 24 | (uint32_t)s.in[s.pos + 1] << 16 | (uint32_t)s.in[s.pos + 2] << 8 | s.in[s.pos + 3];
    for (i = 0; i < s.len; i++) {
        a = (a + out[i]) % 65521;
        b = (b + a) % 65521;
    }
    if ((b << 16 | a) != want) return OK_FORMAT;
    *len = s.len;
    return OK_OK;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

ok_result ok_png_read_rgb15(const uint8_t *png, size_t n, uint16_t *out, size_t max_pixels, int *w, int *h,
                            uint8_t *scratch, size_t scratch_cap)
{
    size_t pos = 8, zlen = 0, raw_len, stride, need, x;
    uint32_t width = 0, height = 0, y;
    int bpp = 0, seen_ihdr = 0, ended = 0;
    uint8_t *z = scratch, *raw;
    ok_result r;
    if (!png || !out || !w || !h || !scratch) return OK_ARGUMENT;
    if (n < 8 || memcmp(png, "\x89PNG\r\n\x1a\n", 8)) return OK_FORMAT;
    while (pos + 12 <= n && !ended) {
        uint32_t len = be32(png + pos), crc;
        const uint8_t *type = png + pos + 4, *data = png + pos + 8;
        if (len > n - pos - 12) return OK_FORMAT;
        crc = be32(data + len);
        if (ok_png_crc(type, len + 4) != crc) return OK_FORMAT;
        if (!memcmp(type, "IHDR", 4)) {
            if (len != 13 || data[8] != 8 || data[10] || data[11] || data[12]) return OK_FORMAT; /* 8-bit, no interlace */
            width = be32(data);
            height = be32(data + 4);
            bpp = data[9] == 0 ? 1 : data[9] == 2 ? 3 : data[9] == 6 ? 4 : 0;
            if (!bpp || !width || !height || width > 1024 || height > 1024) return OK_FORMAT;
            seen_ihdr = 1;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (!seen_ihdr || zlen + len > scratch_cap) return OK_RANGE;
            memcpy(z + zlen, data, len);
            zlen += len;
        } else if (!memcmp(type, "IEND", 4)) {
            ended = 1;
        } else if (!(type[0] & 32)) {
            return OK_FORMAT; /* an unknown critical chunk (PLTE: palettes are not read) */
        }
        pos += 12 + len;
    }
    if (!ended || !zlen) return OK_FORMAT;
    if ((size_t)width * height > max_pixels) return OK_RANGE;
    stride = (size_t)width * (size_t)bpp;
    need = (size_t)height * (stride + 1);
    if (zlen + need > scratch_cap) return OK_RANGE;
    raw = scratch + zlen;
    if ((r = inflate_zlib(z, zlen, raw, need, &raw_len)) != OK_OK) return r;
    if (raw_len != need) return OK_FORMAT;
    /* Undo the filters, row by row, in place. */
    for (y = 0; y < height; y++) {
        uint8_t *row = raw + y * (stride + 1) + 1, *up = y ? row - (stride + 1) : NULL;
        int f = row[-1];
        if (f > 4) return OK_FORMAT;
        for (x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? row[x - (size_t)bpp] : 0, b = up ? up[x] : 0;
            int c = up && x >= (size_t)bpp ? up[x - (size_t)bpp] : 0;
            int v = f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? paeth(a, b, c) : 0;
            row[x] = (uint8_t)(row[x] + v);
        }
        for (x = 0; x < width; x++) {
            const uint8_t *px = row + x * (size_t)bpp;
            int rr = px[0], g = bpp == 1 ? px[0] : px[1], bl = bpp == 1 ? px[0] : px[2];
            out[y * width + x] = (uint16_t)(0x8000 | (rr >> 3) | (g >> 3) << 5 | (bl >> 3) << 10);
        }
    }
    *w = (int)width;
    *h = (int)height;
    return OK_OK;
}
