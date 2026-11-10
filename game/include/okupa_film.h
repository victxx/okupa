#ifndef OKUPA_FILM_H
#define OKUPA_FILM_H

#include "okupa.h"

/* The opening film, rendered once on a computer with the game's own code
 * (tools/filmbake.c) and only played back on the console: decoding a frame is
 * far cheaper than drawing the 3D scene, so it plays at full speed and can
 * never stall.
 *
 * "OKFM" | version 1 | width u16 | height u16 | frames u16 | ticks a frame
 * u8 | first row u8 | palette 256 x u16 (DS colours) | frames + 1 offsets u32
 * (from the start of the frame data) | frames. Little-endian. A frame is a
 * kind byte (0 the pixels, 1 the pixels XOR the frame before) then an LZ
 * stream of the 8-bit palette indices. Fades are not in the file: the player
 * fades the palette, so they stay smooth. */
#define OK_FILM_HEADER (4 + 1 + 2 + 2 + 2 + 1 + 1 + 512)

typedef struct {
    int width, height, frames, ticks, first_row;
    const uint8_t *palette; /* 256 little-endian DS colours, inside the file */
    const uint8_t *offsets, *data;
    size_t data_len;
} ok_film;

/* Reads the header of a whole film file held in memory. */
ok_result ok_film_open(ok_film *f, const uint8_t *file, size_t n);
/* Frame k decoded into pixels (width x height indices). pixels must hold
 * frame k - 1 when that frame is a difference: play them in order (frame 0
 * is always whole). */
ok_result ok_film_frame(const ok_film *f, int k, uint8_t *pixels);
/* The palette faded k/1000 of the way to the colour to. */
void ok_film_palette(const ok_film *f, uint16_t to, int k, uint16_t out[256]);

/* LZ as used here: a flag byte for each 8 items, bit set = a match of two
 * bytes (length 3..17 in the top 4 bits, or 15 there and a third byte adding
 * to 18 for up to 273; distance 1..4096 below), clear = a literal byte. */
size_t ok_film_lz_pack(const uint8_t *in, size_t n, uint8_t *out, size_t cap);
ok_result ok_film_lz_unpack(const uint8_t *in, size_t n, uint8_t *out, size_t want);

#endif
