#include "okupa_png.h"
#include "okupa_act.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int main(void)
{
    static uint16_t frame[256 * 192];
    static uint8_t png[160000];
    uint16_t two[4] = {31, 31 << 5, 31 << 10, 0x7fff}; /* red, green, blue, white */
    uint8_t tiny[128];
    size_t n, i;
    FILE *f;

    /* A 2x2 picture, checked byte by byte where it matters. */
    assert(ok_png_rgb15(two, 2, 2, 1, tiny, sizeof tiny, &n) == OK_OK && n == ok_png_size(2, 2));
    assert(!memcmp(tiny, "\x89PNG\r\n\x1a\n", 8) && be32(tiny + 16) == 2 && be32(tiny + 20) == 2 && tiny[24] == 8 &&
           tiny[25] == 2);
    assert(!memcmp(tiny + 37, "IDAT", 4) && tiny[41] == 0x78 && tiny[42] == 0x01);
    assert(tiny[43] == 1 && tiny[44] == 14 && tiny[45] == 0 && tiny[46] == 0xf1 && tiny[47] == 0xff); /* one stored block of 14 */
    assert(!memcmp(tiny + 48, "\0\xff\0\0\0\xff\0\0\0\0\xff\xff\xff\xff", 14));
    assert(!memcmp(tiny + n - 8, "IEND\xae\x42\x60\x82", 8)); /* the well-known IEND CRC */
    assert(ok_png_rgb15(two, 2, 2, 1, tiny, n - 1, &n) == OK_RANGE);
    assert(ok_png_rgb15(two, 3, 2, 2, tiny, sizeof tiny, &n) == OK_ARGUMENT);

    /* A camera frame halved: 128x96 in one stored block (36960 bytes); averaging a red
     * and a blue pixel gives purple. Written for a decoder to check. */
    for (i = 0; i < 256 * 192; i++) frame[i] = i % 2 ? 31 : 31 << 10;
    assert(ok_png_rgb15(frame, 256, 192, 2, png, sizeof png, &n) == OK_OK && n == ok_png_size(128, 96));
    assert(be32(png + 16) == 128 && be32(png + 20) == 96);
    assert(png[43] == 1 && png[48] == 0 && png[49] == 127 && png[50] == 0 && png[51] == 127);
    if ((f = fopen("build/test-png-frame.png", "wb"))) {
        fwrite(png, 1, n, f);
        fclose(f);
    }
    /* Full size: three stored blocks across the 64 KB limit. */
    {
        static uint8_t big[200000];
        assert(ok_png_rgb15(frame, 256, 192, 1, big, sizeof big, &n) == OK_OK && n == ok_png_size(256, 192));
        assert(big[43] == 0 && big[43 + 5 + 65535] == 0 && big[43 + 2 * (5 + 65535)] == 1);
        if ((f = fopen("build/test-png-full.png", "wb"))) {
            fwrite(big, 1, n, f);
            fclose(f);
        }
    }
    /* The sketchbook: a doodle of a house (a square, a roof, a door) at
     * twice the size, ink black on white. */
    {
        static uint8_t doodle[OK_DRAWING_BYTES], pic[60000];
        int x, y;
        for (x = 16; x < 48; x++) ok_drawing_set(doodle, (unsigned)x, 30, 1), ok_drawing_set(doodle, (unsigned)x, 58, 1);
        for (y = 30; y < 59; y++) ok_drawing_set(doodle, 16, (unsigned)y, 1), ok_drawing_set(doodle, 47, (unsigned)y, 1);
        for (x = 0; x < 16; x++) {
            ok_drawing_set(doodle, (unsigned)(16 + x), (unsigned)(30 - x), 1);
            ok_drawing_set(doodle, (unsigned)(47 - x), (unsigned)(30 - x), 1);
        }
        for (y = 44; y < 59; y++) ok_drawing_set(doodle, 28, (unsigned)y, 1), ok_drawing_set(doodle, 35, (unsigned)y, 1);
        for (x = 28; x < 36; x++) ok_drawing_set(doodle, (unsigned)x, 44, 1);
        assert(ok_png_drawing(doodle, 2, pic, sizeof pic, &n) == OK_OK && n == ok_png_size(128, 128));
        assert(pic[48] == 0 && pic[49] == 255 && pic[52 + 3 * (16 * 2) + 1 + 3 * 128 * 0] == 255); /* white paper */
        assert(ok_png_drawing(doodle, 3, pic, sizeof pic, &n) == OK_ARGUMENT);
        { /* the same drawing as 8-bit grey: a third of the bytes, the same picture read back */
            static uint8_t grey[20000], scratch[40000];
            static uint16_t back[128 * 128];
            size_t g;
            int w, h, k;
            assert(ok_png_drawing_grey(doodle, 2, grey, sizeof grey, &g) == OK_OK && g == ok_png_grey_size(128, 128));
            assert(g * 2 < n && grey[25] == 0); /* colour type 0: grey */
            assert(ok_png_drawing_grey(doodle, 2, grey, g - 1, &g) == OK_RANGE);
            assert(ok_png_drawing_grey(doodle, 2, grey, sizeof grey, &g) == OK_OK);
            assert(ok_png_read_rgb15(grey, g, back, 128 * 128, &w, &h, scratch, sizeof scratch) == OK_OK && w == 128 && h == 128);
            for (k = 0; k < 128 * 128; k++)
                assert((back[k] & 0x7fff) == (ok_drawing_get(doodle, (unsigned)(k % 128 / 2), (unsigned)(k / 128 / 2)) ? 0 : 0x7fff));
            if ((f = fopen("build/test-png-doodle-grey.png", "wb"))) { /* for okupa-mind guess= on the host */
                fwrite(grey, 1, g, f);
                fclose(f);
            }
        }
        if ((f = fopen("build/test-png-doodle.png", "wb"))) {
            fwrite(pic, 1, n, f);
            fclose(f);
        }
    }
    puts("png: header, stored blocks, Adler-32 and CRCs; a camera frame halved");
    return 0;
}
