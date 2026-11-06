#ifndef OKUPA_TEXT_H
#define OKUPA_TEXT_H

#include <stdint.h>

/* A proportional sans-serif pixel font (Pixel Operator, CC0), in a regular
 * and a bold cut: printable ASCII, 13 rows, baseline at row `base`. */
typedef struct {
    uint8_t height, base;
    const uint8_t *advance;  /* 95 widths, ' ' first */
    const uint16_t *rows;    /* 95 x height rows, bit 0 = leftmost pixel */
} ok_font_sans;

extern const ok_font_sans ok_sans_regular, ok_sans_bold;

/* Width in pixels of s in a font. */
int ok_text_width(const ok_font_sans *f, const char *s);
/* Draws s with its top-left at x, y into a 256x192 frame, clipped. */
void ok_text_draw(uint16_t *frame, const ok_font_sans *f, int x, int y, const char *s, uint16_t colour);

#endif
