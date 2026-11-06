/* Okupa on the Nintendo DSi: the room on the top screen, touch below. The
 * same core and game code as the host; this file only owns the hardware. */
#include "okupa_checks.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include <nds.h>
#include <stdio.h>
#include <time.h>

static uint16_t back[2][OK_UI_W * OK_UI_H];

/* Timers 0 and 1 run cascaded at the bus clock (33.513982 MHz). */
static uint32_t now_us(void)
{
    return (uint32_t)((uint64_t)cpuGetTiming() * 1000u / 33514u);
}

static void present(int top, int bottom)
{
    DC_FlushRange(back, sizeof back);
    dmaCopyWords(3, back[0], bgGetGfxPtr(top), sizeof back[0]);
    dmaCopyWords(3, back[1], bgGetGfxPtr(bottom), sizeof back[1]);
}

int main(void)
{
    int top, bottom, held = 0, redraw = 1, fails;
    time_t now = time(NULL), last = now;
    struct tm *tm;
    touchPosition touch;
    ok_pet pet;
    ok_ui ui;

    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    top = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    bottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    if (isDSiMode()) setCpuClock(true);

    ok_pet_init(&pet, (int64_t)now);
    ok_ui_init(&ui);
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        now = time(NULL);
        if (now != last) {
            ok_pet_update(&pet, (int64_t)now);
            last = now;
        }
        if (ok_ui_tick(&ui)) redraw = 1;
        if (keysHeld() & KEY_TOUCH) {
            int r;
            touchRead(&touch);
            r = ok_ui_touch(&ui, &pet, touch.px, touch.py, held, (int64_t)now);
            held = 1;
            if (r == OK_UI_RUN_CHECKS) {
                memset(ui.dev, 0, sizeof ui.dev);
                snprintf(ui.dev[0], sizeof ui.dev[0], "Running the core checks...");
                tm = localtime(&now);
                ok_ui_render(&ui, &pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
                present(top, bottom);
                cpuStartTiming(0);
                fails = ok_checks_run(ui.dev, OK_UI_DEV_LINES - 4, now_us);
                cpuEndTiming();
                snprintf(ui.dev[OK_UI_DEV_LINES - 3], sizeof ui.dev[0], "CPU: %s",
                         isDSiMode() ? "DSi mode, 133 MHz" : "DS mode, 67 MHz");
                snprintf(ui.dev[OK_UI_DEV_LINES - 2], sizeof ui.dev[0], "Build: %.20s", OKUPA_BUILD_ID);
                (void)fails;
            }
            if (r != OK_UI_NOTHING) redraw = 1;
        } else if (held) {
            ok_ui_release(&ui);
            held = 0;
        }
        if (redraw) {
            tm = localtime(&now);
            ok_ui_render(&ui, &pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
            present(top, bottom);
            redraw = 0;
        }
    }
}
