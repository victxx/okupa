#include "okupa_photo.h"
#include <string.h>

#define SIDE OK_DRAWING_SIDE
#define BLOCK 3
#define CROP_X ((OK_PHOTO_FRAME_W - OK_PHOTO_FRAME_H) / 2)
/* 9 pixels x weights summing to 256 x the 5-bit maximum 31. */
#define BLOCK_MAX (BLOCK * BLOCK * 256u * 31u)

static uint8_t block_luma(const uint16_t *frame, unsigned bx, unsigned by)
{
    uint32_t sum = 0;
    unsigned x, y;
    for (y = 0; y < BLOCK; y++) {
        const uint16_t *row = frame + (by * BLOCK + y) * OK_PHOTO_FRAME_W + CROP_X + bx * BLOCK;
        for (x = 0; x < BLOCK; x++) {
            uint32_t c = row[x];
            sum += 77u * (c & 31u) + 150u * ((c >> 5) & 31u) + 29u * ((c >> 10) & 31u);
        }
    }
    return (uint8_t)((sum * 255u + BLOCK_MAX / 2u) / BLOCK_MAX);
}

/* The 2nd..98th percentile of the 4096 blocks; a flat scene is left as it is. */
static void levels(const uint8_t *luma, unsigned *lo, unsigned *hi)
{
    uint16_t hist[256];
    unsigned i, count;
    memset(hist, 0, sizeof hist);
    *lo = 0;
    *hi = 255;
    for (i = 0; i < SIDE * SIDE; i++) hist[luma[i]]++;
    for (count = 0, i = 0; i < 256; i++) {
        count += hist[i];
        if (count > 82u) {
            *lo = i;
            break;
        }
    }
    for (count = 0, i = 256; i-- > 0;) {
        count += hist[i];
        if (count > 82u) {
            *hi = i;
            break;
        }
    }
    if (*hi < *lo + 32u) *lo = 0, *hi = 255;
}

typedef void (*put_fn)(uint8_t *out, unsigned x, unsigned y, unsigned dark);

static void put_tone(uint8_t *out, unsigned x, unsigned y, unsigned dark) { ok_photo_set(out, x, y, dark); }
static void put_ink(uint8_t *out, unsigned x, unsigned y, unsigned dark) { ok_drawing_set(out, x, y, dark != 0); }

static void diffuse(const uint16_t *frame, unsigned steps, put_fn put, uint8_t *out)
{
    static uint8_t luma[SIDE * SIDE];
    int16_t err[2][SIDE + 2];
    unsigned i, x, y, lo, hi, k;
    int span = 255 / (int)(steps - 1u);
    for (y = 0; y < SIDE; y++)
        for (x = 0; x < SIDE; x++) luma[y * SIDE + x] = block_luma(frame, x, y);
    levels(luma, &lo, &hi);
    memset(err, 0, sizeof err);
    for (y = 0; y < SIDE; y++) {
        int16_t *cur = err[y & 1u], *next = err[(y + 1u) & 1u];
        int left = (y & 1u) != 0;
        for (i = 0; i < SIDE + 2; i++) next[i] = 0;
        for (k = 0; k < SIDE; k++) {
            int dir = left ? -1 : 1, l, v, level, e;
            unsigned xx = left ? SIDE - 1u - k : k;
            l = luma[y * SIDE + xx];
            l = l < (int)lo ? 0 : l > (int)hi ? 255 : (l - (int)lo) * 255 / ((int)hi - (int)lo);
            v = l + cur[xx + 1];
            level = (v + span / 2) / span;
            if (level < 0) level = 0;
            if (level > (int)steps - 1) level = (int)steps - 1;
            e = v - level * span;
            put(out, xx, y, steps - 1u - (unsigned)level); /* bright: 0, dark: the most */
            /* 7/16 ahead, 3/16 behind below, 5/16 below, 1/16 ahead below. */
            cur[(int)xx + 1 + dir] += (int16_t)(e * 7 / 16);
            next[(int)xx + 1 - dir] += (int16_t)(e * 3 / 16);
            next[xx + 1] += (int16_t)(e * 5 / 16);
            next[(int)xx + 1 + dir] += (int16_t)(e / 16);
        }
    }
}

ok_result ok_photo_from_frame(const uint16_t *frame, uint8_t tones[OK_PHOTO_BYTES])
{
    if (!frame || !tones) return OK_ARGUMENT;
    memset(tones, 0, OK_PHOTO_BYTES);
    diffuse(frame, 4, put_tone, tones);
    return OK_OK;
}

ok_result ok_photo_ink_from_frame(const uint16_t *frame, uint8_t pixels[OK_DRAWING_BYTES])
{
    if (!frame || !pixels) return OK_ARGUMENT;
    memset(pixels, 0, OK_DRAWING_BYTES);
    diffuse(frame, 2, put_ink, pixels);
    return OK_OK;
}

void ok_photo_to_ink(const uint8_t tones[OK_PHOTO_BYTES], uint8_t pixels[OK_DRAWING_BYTES])
{
    static const uint8_t order[2][2] = {{0, 2}, {3, 1}}; /* the order the four spots of a 2x2 ink in */
    static const uint8_t inked[4] = {0, 1, 2, 4};          /* how many of them each tone inks */
    unsigned x, y;
    memset(pixels, 0, OK_DRAWING_BYTES);
    for (y = 0; y < SIDE; y++)
        for (x = 0; x < SIDE; x++) ok_drawing_set(pixels, x, y, order[y & 1][x & 1] < inked[ok_photo_get(tones, x, y)]);
}
