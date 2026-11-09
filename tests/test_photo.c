#include "okupa_act.h"
#include "okupa_photo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t frame[OK_PHOTO_FRAME_W * OK_PHOTO_FRAME_H];

static uint16_t grey(unsigned v) { return (uint16_t)(0x8000u | v | v << 5 | v << 10); } /* v: 0..31 */

int main(void)
{
    static uint8_t tones[OK_PHOTO_BYTES], ink[OK_DRAWING_BYTES], act[OK_ACT_MAX + 1];
    unsigned x, y, count[4] = {0, 0, 0, 0}, inked = 0;
    size_t n;
    ok_act a;

    /* A ramp from light (left) to dark (right) inside the 192-wide square: light on the left, dark on the right. */
    for (y = 0; y < OK_PHOTO_FRAME_H; y++)
        for (x = 0; x < OK_PHOTO_FRAME_W; x++) {
            unsigned in = x < 32 ? 0 : x >= 224 ? 191 : x - 32;
            frame[y * OK_PHOTO_FRAME_W + x] = grey(31 - in * 31 / 191);
        }
    assert(ok_photo_from_frame(frame, tones) == OK_OK);
    for (y = 0; y < 64; y++) {
        assert(ok_photo_get(tones, 0, y) == 0 && ok_photo_get(tones, 63, y) == 3);
        for (x = 0; x < 64; x++) count[ok_photo_get(tones, x, y)]++;
    }
    assert(count[0] > 400 && count[1] > 400 && count[2] > 400 && count[3] > 400); /* all four tones used */
    /* Darker to the right, on average, column by column groups. */
    {
        unsigned left = 0, right = 0;
        for (y = 0; y < 64; y++)
            for (x = 0; x < 16; x++) left += ok_photo_get(tones, x, y), right += ok_photo_get(tones, 63 - x, y);
        assert(right > left * 3);
    }
    /* 1 bit for ink-only screens: the dark half has more ink. */
    ok_photo_to_ink(tones, ink);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) inked += ok_drawing_get(ink, x, y) ? (x < 32 ? 0u : 1u) : 0u;
    assert(inked > 1200);
    assert(ok_photo_ink_from_frame(frame, ink) == OK_OK && !ok_drawing_get(ink, 0, 10) && ok_drawing_get(ink, 63, 10));

    /* A flat grey scene keeps its tone instead of being stretched into noise. */
    for (x = 0; x < OK_PHOTO_FRAME_W * OK_PHOTO_FRAME_H; x++) frame[x] = grey(15);
    assert(ok_photo_from_frame(frame, tones) == OK_OK);
    memset(count, 0, sizeof count);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) count[ok_photo_get(tones, x, y)]++;
    assert(count[1] + count[2] == 4096 && count[0] == 0 && count[3] == 0);

    /* The photo as an act: OKPA 01 03 and 1024 bytes, read back exactly. */
    assert(ok_act_photo(tones, act, sizeof act, &n) == OK_OK && n == 6 + 1024 && act[5] == OK_ACT_PHOTO);
    assert(ok_act_decode(act, n, &a) == OK_OK && a.kind == OK_ACT_PHOTO && !memcmp(a.photo, tones, 1024) && !a.drawing);
    assert(ok_act_decode(act, n - 1, &a) == OK_FORMAT);
    assert(ok_act_photo(tones, act, n - 1, &n) == OK_RANGE);
    ok_photo_set(tones, 5, 7, 2);
    assert(ok_photo_get(tones, 5, 7) == 2 && ok_photo_get(tones, 64, 0) == 0);
    puts("photo: a camera frame to 64x64 in 4 tones (or ink), levels stretched, flat scenes kept; photo acts");
    return 0;
}
