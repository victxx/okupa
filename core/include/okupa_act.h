#ifndef OKUPA_ACT_H
#define OKUPA_ACT_H

#include "okupa.h"

/* Calldata of an Okupa act (docs/PLAN.md): "OKPA" | version 1 | kind | payload.
 * Only the public kinds live here; photos and gifts are encrypted and get their
 * own encoding once the encryption layer exists. */
#define OK_ACT_HEADER 6
#define OK_DRAWING_SIDE 64
#define OK_DRAWING_BYTES (OK_DRAWING_SIDE * OK_DRAWING_SIDE / 8) /* 1 bpp, MSB first, row-major */
#define OK_ACT_MAX (OK_ACT_HEADER + OK_DRAWING_BYTES)

enum { OK_ACT_HELLO = 0, OK_ACT_DRAWING = 1, OK_ACT_LETTER = 2 };
#define OK_LETTER_MAX 140 /* a letter: plain ASCII text, 1..140 bytes */

typedef struct {
    int kind;
    const uint8_t *drawing; /* OK_DRAWING_BYTES, points into the calldata */
    const char *text;       /* a letter: not NUL-terminated, text_len bytes, points into the calldata */
    size_t text_len;
} ok_act;

ok_result ok_act_hello(uint8_t *out, size_t cap, size_t *len);
ok_result ok_act_drawing(const uint8_t pixels[OK_DRAWING_BYTES], uint8_t *out, size_t cap, size_t *len);
/* A letter the okupa writes to the player: printable ASCII only. */
ok_result ok_act_letter(const char *text, uint8_t *out, size_t cap, size_t *len);
/* Exactly one of the known shapes, or OK_FORMAT. */
ok_result ok_act_decode(const uint8_t *data, size_t n, ok_act *act);

/* Pixel helpers: 1 = ink. */
int ok_drawing_get(const uint8_t pixels[OK_DRAWING_BYTES], unsigned x, unsigned y);
void ok_drawing_set(uint8_t pixels[OK_DRAWING_BYTES], unsigned x, unsigned y, int ink);

#endif
