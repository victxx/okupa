#ifndef OKUPA_PNG_H
#define OKUPA_PNG_H

#include "okupa.h"

/* A plain RGB PNG from a camera frame, small enough to write by hand: the
 * image data goes in uncompressed (stored) deflate blocks, so there is no
 * compressor to trust, only CRC-32 and Adler-32. The DSi's camera gives
 * 15-bit pixels (bits 0-4 red, 5-9 green, 10-14 blue); scale averages each
 * scale x scale block, so a 256x192 frame becomes 128x96 with scale 2. */
size_t ok_png_size(int w, int h); /* bytes the PNG of a w x h picture takes */
ok_result ok_png_rgb15(const uint16_t *frame, int w, int h, int scale, uint8_t *out, size_t cap, size_t *len);

/* The 64x64 sketchbook (okupa_act.h pixels, 1 = ink) as a black-on-white
 * PNG, each pixel scale x scale (1 or 2). */
ok_result ok_png_drawing(const uint8_t pixels[512], int scale, uint8_t *out, size_t cap, size_t *len);

/* Reads a PNG back (8-bit grey, RGB or RGBA, not interlaced): any deflate
 * blocks, every scanline filter, CRCs and Adler-32 checked. Pixels become DS
 * colours (BGR555, bit 15 set). scratch must hold the compressed data plus
 * height * (1 + width * channels) bytes. */
ok_result ok_png_read_rgb15(const uint8_t *png, size_t n, uint16_t *out, size_t max_pixels, int *w, int *h,
                            uint8_t *scratch, size_t scratch_cap);
/* PNG's CRC-32 of a chunk's type and data. */
uint32_t ok_png_crc(const uint8_t *p, size_t n);

#endif
