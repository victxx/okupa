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
    for (i = 0; i < 180; i++) ok_ui_tick(&u);
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
