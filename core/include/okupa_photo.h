#ifndef OKUPA_PHOTO_H
#define OKUPA_PHOTO_H

#include "okupa.h"
#include "okupa_act.h"

/* A camera frame (256x192, 15-bit BGR as the DSi gives it, bit 15 ignored)
 * as a 64x64 picture: the middle 192x192 square, each 3x3 block's luminance,
 * levels stretched from the 2nd to the 98th percentile (a flat scene keeps
 * its tone), then serpentine Floyd-Steinberg to 4 tones (OK_PHOTO_BYTES,
 * 0 light .. 3 dark) or to 1 bit (a drawing: 1 = ink). */
#define OK_PHOTO_FRAME_W 256
#define OK_PHOTO_FRAME_H 192

ok_result ok_photo_from_frame(const uint16_t *frame, uint8_t tones[OK_PHOTO_BYTES]);
ok_result ok_photo_ink_from_frame(const uint16_t *frame, uint8_t pixels[OK_DRAWING_BYTES]);
/* 4 tones down to 1 bit for screens that only show ink (2x2 ordered dither:
 * 0 none, 1 a quarter, 2 half, 3 all). */
void ok_photo_to_ink(const uint8_t tones[OK_PHOTO_BYTES], uint8_t pixels[OK_DRAWING_BYTES]);

#endif
