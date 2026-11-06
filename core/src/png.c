#include "okupa_png.h"
#include "okupa_act.h"
#include <string.h>

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    if (!crc_table[1]) {
        uint32_t k, v;
        int j;
        for (k = 0; k < 256; k++) {
            for (v = k, j = 0; j < 8; j++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
            crc_table[k] = v;
        }
    }
    for (i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

uint32_t ok_png_crc(const uint8_t *p, size_t n) { return crc(0xffffffffu, p, n) ^ 0xffffffffu; }

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Each scanline: a filter byte (0, none) and three bytes a pixel. */
static size_t raw_size(int w, int h) { return (size_t)h * (1 + 3 * (size_t)w); }

size_t ok_png_size(int w, int h)
{
    size_t raw = raw_size(w, h), blocks = (raw + 65534) / 65535;
    /* signature, IHDR, IDAT (zlib header, stored blocks, Adler-32), IEND */
    return 8 + 25 + 12 + 2 + blocks * 5 + raw + 4 + 12;
}

ok_result ok_png_rgb15(const uint16_t *frame, int w, int h, int scale, uint8_t *out, size_t cap, size_t *len)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    uint32_t a = 1, b = 0, c;
    size_t o = 0, raw, left, idat;
    int ow, oh, x, y, dx, dy;
    uint8_t *block = NULL;
    size_t in_block = 0;
    if (!frame || !out || !len || w <= 0 || h <= 0 || scale < 1 || w % scale || h % scale) return OK_ARGUMENT;
    ow = w / scale;
    oh = h / scale;
    if (ok_png_size(ow, oh) > cap) return OK_RANGE;
    raw = raw_size(ow, oh);

    memcpy(out, sig, 8);
    o = 8;
    be32(out + o, 13);
    memcpy(out + o + 4, "IHDR", 4);
    be32(out + o + 8, (uint32_t)ow);
    be32(out + o + 12, (uint32_t)oh);
    out[o + 16] = 8; /* bits per channel */
    out[o + 17] = 2; /* truecolour */
    out[o + 18] = out[o + 19] = out[o + 20] = 0;
    be32(out + o + 21, crc(0xffffffffu, out + o + 4, 17) ^ 0xffffffffu);
    o += 25;

    idat = o;
    o += 8; /* length and type, filled in below */
    out[o++] = 0x78; /* zlib: deflate, 32 KB window */
    out[o++] = 0x01; /* no preset dictionary, fastest; (0x78 << 8 | 0x01) % 31 == 0 */
    left = raw;
    /* The scanlines, written straight into stored blocks of up to 65535 bytes. */
    for (y = 0; y < oh; y++) {
        for (x = -1; x < ow; x++) {
            uint8_t px[3];
            int k, n = x < 0 ? 1 : 3;
            if (x < 0) {
                px[0] = 0; /* filter: none */
            } else {
                uint32_t r = 0, g = 0, bl = 0;
                for (dy = 0; dy < scale; dy++)
                    for (dx = 0; dx < scale; dx++) {
                        uint16_t v = frame[(size_t)(y * scale + dy) * (size_t)w + (size_t)(x * scale + dx)];
                        r += v & 31;
                        g += v >> 5 & 31;
                        bl += v >> 10 & 31;
                    }
                k = scale * scale;
                px[0] = (uint8_t)(r * 255 / (31u * (uint32_t)k));
                px[1] = (uint8_t)(g * 255 / (31u * (uint32_t)k));
                px[2] = (uint8_t)(bl * 255 / (31u * (uint32_t)k));
            }
            for (k = 0; k < n; k++) {
                if (!in_block) {
                    size_t size = left < 65535 ? left : 65535;
                    block = out + o;
                    block[0] = left == size; /* the last block says so */
                    block[1] = (uint8_t)size;
                    block[2] = (uint8_t)(size >> 8);
                    block[3] = (uint8_t)~size;
                    block[4] = (uint8_t)(~size >> 8);
                    o += 5;
                    in_block = size;
                    left -= size;
                }
                out[o++] = px[k];
                a = (a + px[k]) % 65521;
                b = (b + a) % 65521;
                in_block--;
            }
        }
    }
    (void)block;
    be32(out + o, b << 16 | a);
    o += 4;
    be32(out + idat, (uint32_t)(o - idat - 8));
    memcpy(out + idat + 4, "IDAT", 4);
    c = crc(0xffffffffu, out + idat + 4, o - idat - 4) ^ 0xffffffffu;
    be32(out + o, c);
    o += 4;

    be32(out + o, 0);
    memcpy(out + o + 4, "IEND", 4);
    be32(out + o + 8, crc(0xffffffffu, out + o + 4, 4) ^ 0xffffffffu);
    o += 12;
    *len = o;
    return OK_OK;
}

ok_result ok_png_drawing(const uint8_t pixels[512], int scale, uint8_t *out, size_t cap, size_t *len)
{
    static uint16_t frame[256 * 256];
    int x, y, w;
    if (!pixels || scale < 1 || scale > 4) return OK_ARGUMENT;
    w = 64 * scale;
    for (y = 0; y < w; y++)
        for (x = 0; x < w; x++)
            frame[y * w + x] = ok_drawing_get(pixels, (unsigned)(x / scale), (unsigned)(y / scale)) ? 0 : 0x7fff;
    return ok_png_rgb15(frame, w, w, 1, out, cap, len);
}
