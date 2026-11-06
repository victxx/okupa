/* Okupa on the Nintendo DSi: the room on the top screen, touch below. The
 * same core and game code as the host; this file only owns the hardware. */
#include "net.h"
#include "okupa_checks.h"
#include "okupa.h"
#include "okupa_font.h"
#include "okupa_save.h"
#include "okupa_ui.h"
#include <fat.h>
#include <nds.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

static uint16_t back[2][OK_UI_W * OK_UI_H];
static int top_bg, bottom_bg;
static ok_ui *shown_ui;
static ok_pet *shown_pet;
static char save_path[40], temp_path[40], wifi_path[40];
static const char *save_status = "Save: no SD card, not saved";

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

/* Redraws while a long task (the network test) runs. */
static void show_now(void)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    ok_ui_render(shown_ui, shown_pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
    present(top_bg, bottom_bg);
}

/* The DSi's own SD slot first, then a flashcart; never the internal memory. */
static int open_storage(void)
{
    static const char *roots[] = {"sd:/", "fat:/"};
    struct stat st;
    unsigned i;
    if (!fatInitDefault()) return 0;
    for (i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        if (stat(roots[i], &st) || !S_ISDIR(st.st_mode)) continue;
        snprintf(save_path, sizeof save_path, "%sokupa", roots[i]);
        mkdir(save_path, 0700);
        snprintf(save_path, sizeof save_path, "%sokupa/save.bin", roots[i]);
        snprintf(temp_path, sizeof temp_path, "%sokupa/save.tmp", roots[i]);
        snprintf(wifi_path, sizeof wifi_path, "%sokupa/wifi.txt", roots[i]);
        return 1;
    }
    return 0;
}

static int load_file(const char *path, ok_pet *p, uint8_t drawing[512])
{
    uint8_t buf[OK_SAVE_SIZE + 1];
    size_t n;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return ok_save_read(buf, n, p, drawing) == OK_OK;
}

/* The Wi-Fi picked in the app: two lines, name and password, in plain text
 * on the SD card (like the console's own settings, it is not secret from
 * whoever holds the card). */
static void load_wifi(void)
{
    char ssid[40] = {0}, password[72] = {0};
    FILE *f = fopen(wifi_path, "r");
    if (!f) return;
    if (fgets(ssid, sizeof ssid, f)) {
        if (!fgets(password, sizeof password, f)) password[0] = 0;
        ssid[strcspn(ssid, "\r\n")] = 0;
        password[strcspn(password, "\r\n")] = 0;
        if (ssid[0]) ok_dsi_wifi_use(ssid, password);
    }
    fclose(f);
}

static void save_wifi(const char *ssid, const char *password)
{
    FILE *f = fopen(wifi_path, "w");
    if (!f) return;
    fprintf(f, "%s\n%s\n", ssid, password);
    fclose(f);
}

/* Write a new file, then swap it in. If the power goes in between, the
 * temporary copy is complete and loading falls back to it. */
static int save(const ok_pet *p, const uint8_t drawing[512])
{
    uint8_t buf[OK_SAVE_SIZE];
    FILE *f;
    int ok;
    ok_save_write(p, drawing, buf);
    if (!(f = fopen(temp_path, "wb"))) return 0;
    ok = fwrite(buf, 1, sizeof buf, f) == sizeof buf;
    ok = fclose(f) == 0 && ok;
    if (!ok) return 0;
    remove(save_path);
    return rename(temp_path, save_path) == 0;
}

int main(void)
{
    int top, bottom, held = 0, redraw = 1, storage, fails;
    time_t now = time(NULL), last = now, saved_at = now;
    uint8_t saved[OK_SAVE_SIZE], current[OK_SAVE_SIZE];
    struct tm *tm;
    touchPosition touch;
    ok_pet pet;
    ok_ui ui;

    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    top_bg = top = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    bottom_bg = bottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    if (isDSiMode()) setCpuClock(true);

    ok_ui_init(&ui);
    ok_pet_init(&pet, (int64_t)now);
    shown_ui = &ui;
    shown_pet = &pet;
    storage = open_storage();
    if (storage) {
        if (load_file(save_path, &pet, ui.drawing) || load_file(temp_path, &pet, ui.drawing)) {
            save_status = "Save: loaded from SD";
            ok_pet_update(&pet, (int64_t)now); /* live the time it was off */
        } else {
            save_status = "Save: new okupa on SD";
        }
        load_wifi();
    }
    ok_save_write(&pet, ui.drawing, saved);

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
                snprintf(ui.dev[OK_UI_DEV_LINES - 1], sizeof ui.dev[0], "%.32s", save_status);
                (void)fails;
            }
            if (r == OK_UI_WIFI_SCAN) {
                ui.ap_count = ok_dsi_wifi_scan(ui.aps, OK_UI_APS, ui.wifi_status, show_now);
            }
            if (r == OK_UI_WIFI_CONNECT && ui.ap_selected >= 0 && ui.ap_selected < ui.ap_count) {
                const char *ssid = ui.aps[ui.ap_selected].ssid;
                if (ok_dsi_wifi_connect(ssid, ui.password, ui.wifi_status, show_now) && storage)
                    save_wifi(ssid, ui.password);
                ok_wipe(ui.password, sizeof ui.password);
                ui.password_len = 0;
            }
            if (r == OK_UI_RUN_NETWORK) {
                cpuStartTiming(0);
                ok_dsi_network_test(ui.dev, OK_UI_DEV_LINES, show_now);
                cpuEndTiming();
            }
            if (r != OK_UI_NOTHING) redraw = 1;
        } else if (held) {
            ok_ui_release(&ui);
            held = 0;
        }
        /* Save what the player changed once the stylus is up, and the passing
         * time every five minutes; never more than once every two seconds. */
        if (storage && !held && now - saved_at >= 2) {
            ok_save_write(&pet, ui.drawing, current);
            if (memcmp(current, saved, 5 + OK_NEEDS + 1) || memcmp(current + OK_SAVE_SIZE - 516, saved + OK_SAVE_SIZE - 516, 512) ||
                now - saved_at >= 300) {
                if (save(&pet, ui.drawing)) {
                    memcpy(saved, current, sizeof saved);
                    save_status = "Save: saved to SD";
                } else {
                    save_status = "Save: FAILED to write SD";
                }
                saved_at = now;
            }
        }
        if (redraw) {
            tm = localtime(&now);
            ok_ui_render(&ui, &pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
            present(top, bottom);
            redraw = 0;
        }
    }
}
