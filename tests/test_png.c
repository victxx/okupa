#include "okupa_png.h"
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
    puts("png: header, stored blocks, Adler-32 and CRCs; a camera frame halved");
    return 0;
}
