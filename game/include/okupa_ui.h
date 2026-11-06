#ifndef OKUPA_UI_H
#define OKUPA_UI_H

#include "okupa_house.h"
#include "okupa_pet.h"
#include <stdint.h>

/* The two screens, drawn into 256x192 frames of DS colours (BGR555 with bit 15
 * set) by plain C, so the DSi, the host preview and later a phone share it. */
#define OK_UI_W 256
#define OK_UI_H 192
enum { OK_UI_HOME, OK_UI_SKETCH, OK_UI_DEV, OK_UI_WIFI, OK_UI_PASSWORD, OK_UI_CLOSET };
enum { OK_UI_NOTHING, OK_UI_CHANGED, OK_UI_RUN_CHECKS, OK_UI_RUN_NETWORK, OK_UI_WIFI_SCAN, OK_UI_WIFI_CONNECT };
#define OK_UI_DEV_LINES 14
#define OK_UI_APS 5

/* A Wi-Fi network found by the platform's scan. */
typedef struct {
    char ssid[33];
    int secure;   /* needs a password */
    int strength; /* 0..3 bars */
} ok_ui_ap;

typedef struct {
    int scene;
    const char *say;    /* the okupa's line, NULL = its mood line */
    int say_ticks;      /* frames left before it goes back to its mood line */
    uint32_t ticks;     /* frames since start, for small animations */
    uint8_t drawing[64 * 64 / 8];
    int eraser;
    int last_x, last_y; /* previous canvas point while the stylus is down */
    char dev[OK_UI_DEV_LINES][33];
    /* Wi-Fi set-up: the platform fills aps and status; the player picks one
     * and types its password, then the platform gets OK_UI_WIFI_CONNECT. */
    ok_ui_ap aps[OK_UI_APS];
    int ap_count, ap_selected;
    char password[64];
    int password_len, keys; /* keys: 0 lower case, 1 upper case, 2 symbols */
    char wifi_status[33];
    char net_status[33]; /* the platform's one-line network state, shown at home */
    /* The okupa on the top screen: how it moves, what it wears, the drawing
     * hung on the wall (the sketch as it was at Done). */
    ok_monkey_anim anim;
    ok_look look;
    uint8_t wall[64 * 64 / 8];
    int wall_set, ready, happy_ticks;
    int pressed, pressed_ticks; /* the button just tapped (1..8, 100 = Wear), for its pressed look */
} ok_ui;

void ok_ui_init(ok_ui *u);
/* One frame (1/60 s) passed; returns OK_UI_CHANGED when the screens need
 * redrawing (30 times a second: the okupa is always moving a little). */
int ok_ui_tick(ok_ui *u, const ok_pet *p);
/* Stylus at x, y on the bottom screen; held = it was already down last frame. */
int ok_ui_touch(ok_ui *u, ok_pet *p, int x, int y, int held, int64_t now);
void ok_ui_release(ok_ui *u);
/* font: 128 glyphs of 8 rows, bit 0 = leftmost pixel. */
void ok_ui_render(const ok_ui *u, const ok_pet *p, int hour, int minute, const uint8_t (*font)[8],
                  uint16_t top[OK_UI_W * OK_UI_H], uint16_t bottom[OK_UI_W * OK_UI_H]);

#endif
