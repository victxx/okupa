#ifndef OKUPA_FONT_H
#define OKUPA_FONT_H

#include <stdint.h>

/* The 8x8 public-domain font (third_party/font8x8): 128 glyphs, bit 0 = left. */
const uint8_t (*ok_font(void))[8];

#endif
