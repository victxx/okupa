#include "okupa_film.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FW 64
#define FH 48

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff), put16(p + 2, v >> 16); }

int main(void)
{
    static uint8_t in[FW * FH * 4], packed[FW * FH * 8], back[FW * FH * 4], file[65536], px[FW * FH];
    static uint8_t frames[3][FW * FH];
    uint32_t s = 7;
    size_t n, i, at;
    int k;
    ok_film f;
    uint16_t pal[256];

    /* LZ: noise, long flat runs and repeats come back exactly; damage is refused. */
    for (i = 0; i < sizeof in; i++) {
        s = s * 1103515245u + 12345u;
        in[i] = i < 3000 ? (uint8_t)(s >> 24) : i < 6000 ? 9 : in[i - 700];
    }
    n = ok_film_lz_pack(in, sizeof in, packed, sizeof packed);
    assert(n && n < sizeof in / 2);
    assert(ok_film_lz_unpack(packed, n, back, sizeof in) == OK_OK && !memcmp(in, back, sizeof in));
    assert(ok_film_lz_unpack(packed, n - 1, back, sizeof in) == OK_FORMAT);
    assert(ok_film_lz_unpack(packed, n, back, sizeof in - 1) == OK_FORMAT); /* bytes left over */
    packed[1] ^= 0xff; /* a distance before the start is caught, never read */
    {
        uint8_t first[3] = {0x80, 0x0f, 0xff};
        assert(ok_film_lz_unpack(first, 3, back, 10) == OK_FORMAT);
    }
    assert(ok_film_lz_pack(in, sizeof in, packed, 10) == 0); /* too small: none */

    /* A small film: a whole frame, then two differences; played in order. */
    for (k = 0; k < 3; k++)
        for (i = 0; i < FW * FH; i++) frames[k][i] = (uint8_t)((i / FW + k * (i % FW > 30)) % 7);
    memset(file, 0, sizeof file);
    memcpy(file, "OKFM\1", 5);
    put16(file + 5, FW), put16(file + 7, FH), put16(file + 9, 3);
    file[11] = 2, file[12] = 20;
    for (i = 0; i < 256; i++) put16(file + 13 + 2 * i, (unsigned)(0x8000 | i));
    at = 0;
    {
        uint8_t *data = file + OK_FILM_HEADER + 16;
        for (k = 0; k < 3; k++) {
            uint8_t diff[FW * FH];
            const uint8_t *src = frames[k];
            put32(file + OK_FILM_HEADER + 4 * k, (uint32_t)at);
            if (k) {
                for (i = 0; i < FW * FH; i++) diff[i] = frames[k][i] ^ frames[k - 1][i];
                src = diff;
            }
            data[at++] = k ? 1 : 0;
            at += ok_film_lz_pack(src, FW * FH, data + at, sizeof file - 600 - at);
        }
        put32(file + OK_FILM_HEADER + 12, (uint32_t)at);
    }
    n = OK_FILM_HEADER + 16 + at;
    assert(ok_film_open(&f, file, n) == OK_OK && f.width == FW && f.height == FH && f.frames == 3 && f.first_row == 20);
    for (k = 0; k < 3; k++) assert(ok_film_frame(&f, k, px) == OK_OK && !memcmp(px, frames[k], sizeof px));
    assert(ok_film_frame(&f, 3, px) == OK_ARGUMENT);
    /* Fades on the palette: none, half way to white, all the way. */
    ok_film_palette(&f, 0xffff, 0, pal);
    assert(pal[5] == (0x8000 | 5));
    ok_film_palette(&f, 0xffff, 1000, pal);
    assert(pal[5] == 0xffff);
    ok_film_palette(&f, 0x8000, 500, pal);
    assert((pal[31] & 31) == 16); /* 31 halfway to 0 */
    /* A header that does not add up is refused. */
    assert(ok_film_open(&f, file, OK_FILM_HEADER + 15) == OK_FORMAT);
    file[4] = 2;
    assert(ok_film_open(&f, file, n) == OK_FORMAT);
    file[4] = 1;
    file[OK_FILM_HEADER + 16] = 1; /* the first frame must be whole */
    assert(ok_film_open(&f, file, n) == OK_FORMAT);
    puts("film: LZ round-trips and refuses damage; frames whole and as differences; palette fades");
    return 0;
}
