#ifndef OKUPA_UI_H
#define OKUPA_UI_H

#include "okupa_pet.h"
#include <stdint.h>

/* The two screens, drawn into 256x192 frames of DS colours (BGR555 with bit 15
 * set) by plain C, so the DSi, the host preview and later a phone share it. */
#define OK_UI_W 256
#define OK_UI_H 192
enum { OK_UI_HOME, OK_UI_SKETCH, OK_UI_DEV };
enum { OK_UI_NOTHING, OK_UI_CHANGED, OK_UI_RUN_CHECKS, OK_UI_RUN_NETWORK };
#define OK_UI_DEV_LINES 14

typedef struct {
    int scene;
    const char *say;    /* the okupa's line, NULL = its mood line */
    int say_ticks;      /* frames left before it goes back to its mood line */
    uint32_t ticks;     /* frames since start, for small animations */
    uint8_t drawing[64 * 64 / 8];
    int eraser;
    int last_x, last_y; /* previous canvas point while the stylus is down */
    char dev[OK_UI_DEV_LINES][33];
} ok_ui;

void ok_ui_init(ok_ui *u);
/* One frame passed; returns OK_UI_CHANGED when the screens need redrawing. */
int ok_ui_tick(ok_ui *u);
/* Stylus at x, y on the bottom screen; held = it was already down last frame. */
int ok_ui_touch(ok_ui *u, ok_pet *p, int x, int y, int held, int64_t now);
void ok_ui_release(ok_ui *u);
/* font: 128 glyphs of 8 rows, bit 0 = leftmost pixel. */
void ok_ui_render(const ok_ui *u, const ok_pet *p, int hour, int minute, const uint8_t (*font)[8],
                  uint16_t top[OK_UI_W * OK_UI_H], uint16_t bottom[OK_UI_W * OK_UI_H]);

#endif
