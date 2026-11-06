#include "okupa_png.h"
#include "png_vectors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static uint16_t pic[256 * 256], frame[64 * 48];
    static uint8_t scratch[300000], png[20000], bad[4096];
    size_t i, n;
    int w, h, k;

    /* Pictures made by zlib: dynamic, fixed and stored blocks, all five
     * filters, grey, RGB and RGBA, split over two IDAT chunks. */
    for (k = 0; k < (int)(sizeof png_cases / sizeof png_cases[0]); k++) {
        assert(ok_png_read_rgb15(png_cases[k].png, png_cases[k].n, pic, sizeof pic / 2, &w, &h, scratch, sizeof scratch) ==
               OK_OK);
        assert(w == png_cases[k].w && h == png_cases[k].h);
        assert(!memcmp(pic, png_cases[k].px, (size_t)(w * h) * 2));
    }

    /* Our own writer, read back. */
    for (i = 0; i < 64 * 48; i++) frame[i] = (uint16_t)(i * 37 & 0x7fff);
    assert(ok_png_rgb15(frame, 64, 48, 1, png, sizeof png, &n) == OK_OK);
    assert(ok_png_read_rgb15(png, n, pic, sizeof pic / 2, &w, &h, scratch, sizeof scratch) == OK_OK && w == 64 && h == 48);
    for (i = 0; i < 64 * 48; i++) assert((pic[i] & 0x7fff) == frame[i]);

    /* Refused: a flipped bit (CRC), a picture too big for the caller, a
     * truncated file, scratch too small. */
    memcpy(bad, png_cases[0].png, png_cases[0].n);
    bad[png_cases[0].n / 2] ^= 1;
    assert(ok_png_read_rgb15(bad, png_cases[0].n, pic, sizeof pic / 2, &w, &h, scratch, sizeof scratch) == OK_FORMAT);
    assert(ok_png_read_rgb15(png_cases[0].png, png_cases[0].n, pic, 100, &w, &h, scratch, sizeof scratch) == OK_RANGE);
    assert(ok_png_read_rgb15(png_cases[0].png, png_cases[0].n - 20, pic, sizeof pic / 2, &w, &h, scratch, sizeof scratch) !=
           OK_OK);
    assert(ok_png_read_rgb15(png_cases[0].png, png_cases[0].n, pic, sizeof pic / 2, &w, &h, scratch, 64) == OK_RANGE);
    puts("unpng: zlib-made PNGs (stored, fixed, dynamic; all filters; grey, RGB, RGBA) and our own, read back");
    return 0;
}
