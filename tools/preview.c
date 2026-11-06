/* Renders the two DSi screens on the host into PNGs (top above bottom), so the
 * art can be looked at without a console. Not evidence of what the ARM runs.
 *   build/okupa-preview <out dir> */
#include "okupa_act.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include "png.h"

#define W OK_UI_W
#define H (2 * OK_UI_H + 8)

static uint16_t top[OK_UI_W * OK_UI_H], bottom[OK_UI_W * OK_UI_H];

/* Top screen above the bottom one, an 8-pixel gap between them. */
static void png(const char *path)
{
    static uint16_t canvas[W * H];
    int i;
    for (i = 0; i < W * H; i++) canvas[i] = 0x8000;
    memcpy(canvas, top, sizeof top);
    memcpy(canvas + W * (OK_UI_H + 8), bottom, sizeof bottom);
    png_write(path, canvas, W, H, 1);
}

/* Lets the okupa live a little (the pet stays as it is), then takes the picture. */
static void shot(const char *dir, const char *name, ok_ui *u, const ok_pet *p, int hour)
{
    char path[512];
    int i;
    for (i = 0; i < 90; i++) ok_ui_tick(u, p);
    ok_ui_render(u, p, hour, 42, ok_font(), top, bottom);
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    png(path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/preview";
    int64_t t = 1800000000;
    ok_ui u;
    ok_pet p;
    int i;

    ok_pet_init(&p, t);
    ok_ui_init(&u);
    shot(dir, "home-morning", &u, &p, 10);
    ok_ui_touch(&u, &p, 159, 60, 0, t); /* Pet */
    shot(dir, "home-petted", &u, &p, 10);
    p.need[OK_NEED_HUNGER] = 20;
    u.say = NULL;
    shot(dir, "home-hungry-evening", &u, &p, 19);
    p.asleep = 1;
    u.ready = 0;
    shot(dir, "home-asleep-night", &u, &p, 23);
    p.asleep = 0;
    u.ready = 0;
    p.need[OK_NEED_HUNGER] = 70;
    u.scene = OK_UI_CLOSET;
    u.look.top = OK_TOP_STRIPES;
    u.look.colour = 3;
    shot(dir, "closet", &u, &p, 12);
    u.scene = OK_UI_SKETCH;
    for (i = 0; i < 64; i++) {
        ok_drawing_set(u.drawing, (unsigned)i, (unsigned)(32 + (i % 16 < 8 ? i % 8 : 8 - i % 8)), 1);
        ok_drawing_set(u.drawing, 32, (unsigned)i, i > 10 && i < 50);
    }
    shot(dir, "sketchbook", &u, &p, 15);
    u.scene = OK_UI_DEV;
    snprintf(u.dev[0], sizeof u.dev[0], "keccak256(abc)      ok  0 ms");
    snprintf(u.dev[1], sizeof u.dev[1], "sign tx (eth-acct)  ok 41 ms");
    shot(dir, "dev", &u, &p, 15);
    u.scene = OK_UI_WIFI;
    u.ap_count = 3;
    strcpy(u.aps[0].ssid, "iPhone de Okupa");
    u.aps[0].secure = 1;
    u.aps[0].strength = 3;
    strcpy(u.aps[1].ssid, "MOVISTAR_5G_2.4");
    u.aps[1].secure = 1;
    u.aps[1].strength = 1;
    strcpy(u.aps[2].ssid, "Cafe libre");
    u.aps[2].strength = 2;
    strcpy(u.wifi_status, "Tap a network");
    shot(dir, "wifi", &u, &p, 15);
    u.scene = OK_UI_PASSWORD;
    u.ap_selected = 0;
    strcpy(u.password, "okupa123");
    u.password_len = 8;
    shot(dir, "wifi-password", &u, &p, 15);
    return 0;
}
