/* Writes a canvas of DS colours (BGR555) as a PNG, each pixel blown up to a
 * zoom x zoom square. A zlib stream of stored blocks: simple and exact.
 * Host tools only. */
#ifndef OKUPA_TOOLS_PNG_H
#define OKUPA_TOOLS_PNG_H

#include "okupa_eth.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void png_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint32_t crc = ok_crc32(ok_crc32(0, type, 4), data, n);
    png_be32(f, n);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    png_be32(f, crc);
}

static int png_write(const char *path, const uint16_t *pixels, int w, int h, int zoom)
{
    int W = w * zoom, H = h * zoom, x, y;
    size_t row = 1 + 3 * (size_t)W, n = row * H, at = 0, zn = 0, i;
    uint8_t *raw = malloc(n), *z = malloc(n + n / 65535 * 5 + 64);
    uint8_t ihdr[13] = {(uint8_t)(W >> 24), (uint8_t)(W >> 16), (uint8_t)(W >> 8), (uint8_t)W,
                        (uint8_t)(H >> 24), (uint8_t)(H >> 16), (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
    uint32_t a = 1, b = 0;
    FILE *f;
    if (!raw || !z) return 0;
    for (y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (x = 0; x < W; x++) {
            uint16_t c = pixels[(y / zoom) * w + x / zoom];
            uint8_t *p = raw + y * row + 1 + 3 * x;
            p[0] = (uint8_t)((c & 31) * 255 / 31);
            p[1] = (uint8_t)((c >> 5 & 31) * 255 / 31);
            p[2] = (uint8_t)((c >> 10 & 31) * 255 / 31);
        }
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    while (at < n) {
        size_t k = n - at > 65535 ? 65535 : n - at;
        z[zn++] = at + k == n;
        z[zn++] = (uint8_t)k;
        z[zn++] = (uint8_t)(k >> 8);
        z[zn++] = (uint8_t)~k;
        z[zn++] = (uint8_t)(~k >> 8);
        memcpy(z + zn, raw + at, k);
        zn += k;
        at += k;
    }
    for (i = 0; i < n; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    z[zn++] = (uint8_t)(b >> 8);
    z[zn++] = (uint8_t)b;
    z[zn++] = (uint8_t)(a >> 8);
    z[zn++] = (uint8_t)a;
    f = fopen(path, "wb");
    if (f) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", z, (uint32_t)zn);
        png_chunk(f, "IEND", NULL, 0);
        fclose(f);
        printf("okupa-preview: %s\n", path);
    } else {
        perror(path);
    }
    free(raw);
    free(z);
    return f != NULL;
}

#endif
