#include "okupa_act.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t top[OK_UI_W * OK_UI_H], bottom[OK_UI_W * OK_UI_H];

int main(void)
{
    int64_t t = 1800000000;
    ok_ui u;
    ok_pet p;
    int i, inked = 0;

    ok_pet_init(&p, t);
    ok_ui_init(&u);

    /* Buttons (button centres from the layout: columns at 8 + 62 i, rows at 36 and 102). */
    assert(ok_ui_touch(&u, &p, 35, 60, 0, t) == OK_UI_CHANGED && p.need[OK_NEED_HUNGER] == 100);
    assert(!strcmp(u.say, "Yum! Thank you."));
    assert(ok_ui_touch(&u, &p, 35, 60, 1, t) == OK_UI_NOTHING); /* holding is not a second tap */
    assert(ok_ui_touch(&u, &p, 97, 60, 0, t) == OK_UI_CHANGED && p.need[OK_NEED_FUN] == 95);
    assert(ok_ui_touch(&u, &p, 159, 60, 0, t) == OK_UI_CHANGED && p.need[OK_NEED_LOVE] == 90);
    p.need[OK_NEED_ENERGY] = 90;
    assert(ok_ui_touch(&u, &p, 221, 60, 0, t) == OK_UI_CHANGED && !p.asleep); /* not sleepy */
    p.need[OK_NEED_ENERGY] = 30;
    assert(ok_ui_touch(&u, &p, 221, 60, 0, t) == OK_UI_CHANGED && p.asleep);
    assert(ok_ui_touch(&u, &p, 35, 60, 0, t) == OK_UI_CHANGED && !strcmp(u.say, "Zzz... (it's sleeping)"));
    assert(ok_ui_touch(&u, &p, 221, 60, 0, t) == OK_UI_CHANGED && !p.asleep);
    assert(ok_ui_touch(&u, &p, 159, 130, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME); /* Mail: soon */
    assert(ok_ui_touch(&u, &p, 4, 4, 0, t) == OK_UI_NOTHING);

    /* The line goes back to the mood line after three seconds. */
    for (i = 0; i < 180; i++) ok_ui_tick(&u, &p);
    assert(u.say == NULL);

    /* Sketchbook: a stroke leaves no gaps, the eraser removes it, Done goes home. */
    assert(ok_ui_touch(&u, &p, 35, 130, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_SKETCH);
    assert(ok_ui_touch(&u, &p, 8, 100, 0, t) == OK_UI_CHANGED);
    assert(ok_ui_touch(&u, &p, 134, 100, 1, t) == OK_UI_CHANGED);
    ok_ui_release(&u);
    for (i = 0; i < 64; i++) inked += ok_drawing_get(u.drawing, (unsigned)i, 30);
    assert(inked == 64);
    assert(ok_ui_touch(&u, &p, 198, 85, 0, t) == OK_UI_CHANGED && u.eraser);
    assert(ok_ui_touch(&u, &p, 8, 100, 0, t) == OK_UI_CHANGED && ok_drawing_get(u.drawing, 0, 30) == 0);
    ok_ui_release(&u);
    assert(ok_ui_touch(&u, &p, 198, 120, 0, t) == OK_UI_CHANGED);
    for (i = 0; i < (int)sizeof u.drawing; i++) assert(u.drawing[i] == 0);
    assert(ok_ui_touch(&u, &p, 198, 150, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME);

    /* Dev asks the platform to run the checks; Back returns. */
    assert(ok_ui_touch(&u, &p, 221, 130, 0, t) == OK_UI_RUN_CHECKS && u.scene == OK_UI_DEV);
    assert(ok_ui_touch(&u, &p, 50, 176, 0, t) == OK_UI_RUN_NETWORK && u.scene == OK_UI_DEV);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME);

    /* Wi-Fi from Dev: search, pick a secured network, type its password. */
    assert(ok_ui_touch(&u, &p, 221, 130, 0, t) == OK_UI_RUN_CHECKS);
    assert(ok_ui_touch(&u, &p, 130, 176, 0, t) == OK_UI_WIFI_SCAN && u.scene == OK_UI_WIFI);
    u.ap_count = 2;
    strcpy(u.aps[0].ssid, "iPhone de Okupa");
    u.aps[0].secure = 1;
    u.aps[0].strength = 3;
    strcpy(u.aps[1].ssid, "Cafe libre");
    assert(ok_ui_touch(&u, &p, 50, 176, 0, t) == OK_UI_WIFI_SCAN);
    assert(ok_ui_touch(&u, &p, 100, 30, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_PASSWORD && u.ap_selected == 0);
    assert(ok_ui_touch(&u, &p, 8 + 24 * 2 + 5, 36 + 26 + 5, 0, t) == OK_UI_CHANGED);  /* e */
    assert(ok_ui_touch(&u, &p, 10, 145, 0, t) == OK_UI_CHANGED && u.keys == 1);      /* Caps */
    assert(ok_ui_touch(&u, &p, 8 + 24 * 0 + 5, 36 + 26 * 2 + 5, 0, t) == OK_UI_CHANGED); /* A */
    assert(ok_ui_touch(&u, &p, 70, 145, 0, t) == OK_UI_CHANGED && u.keys == 2);      /* symbols */
    assert(ok_ui_touch(&u, &p, 8 + 5, 36 + 26 + 5, 0, t) == OK_UI_CHANGED);          /* ! */
    assert(ok_ui_touch(&u, &p, 8 + 24 * 9 + 5, 36 + 5, 0, t) == OK_UI_CHANGED);      /* 0 */
    assert(ok_ui_touch(&u, &p, 130, 145, 0, t) == OK_UI_CHANGED);                    /* space */
    assert(!strcmp(u.password, "eA!0 ") && u.password_len == 5);
    assert(ok_ui_touch(&u, &p, 200, 145, 0, t) == OK_UI_CHANGED && !strcmp(u.password, "eA!0"));
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    assert(ok_ui_touch(&u, &p, 190, 176, 0, t) == OK_UI_WIFI_CONNECT && u.scene == OK_UI_WIFI);
    assert(!strcmp(u.password, "eA!0"));
    assert(ok_ui_touch(&u, &p, 100, 55, 0, t) == OK_UI_WIFI_CONNECT && u.ap_selected == 1 && !u.password_len);
    assert(ok_ui_touch(&u, &p, 100, 30, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_PASSWORD);
    assert(ok_ui_touch(&u, &p, 60, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_WIFI); /* Cancel */
    ok_ui_render(&u, &p, 12, 0, ok_font(), top, bottom);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_DEV);
    assert(ok_ui_touch(&u, &p, 200, 176, 0, t) == OK_UI_CHANGED && u.scene == OK_UI_HOME);

    /* Every scene renders, with and without a font. */
    ok_ui_render(&u, &p, 10, 5, ok_font(), top, bottom);
    assert(top[0] & 0x8000 && bottom[OK_UI_W * OK_UI_H - 1] & 0x8000);
    u.scene = OK_UI_SKETCH;
    ok_ui_render(&u, &p, 23, 59, NULL, top, bottom);
    u.scene = OK_UI_DEV;
    p.asleep = 1;
    ok_ui_render(&u, &p, 3, 0, ok_font(), top, bottom);
    puts("ui: buttons drive the okupa, sketchbook strokes and eraser, dev and back, all scenes render");
    return 0;
}
