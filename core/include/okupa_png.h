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
 * PNG, each pixel scale x scale (1..4). */
ok_result ok_png_drawing(const uint8_t pixels[512], int scale, uint8_t *out, size_t cap, size_t *len);

#endif
