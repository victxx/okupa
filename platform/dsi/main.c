/* Okupa on the Nintendo DSi: the room on the top screen, touch below. The
 * same core and game code as the host; this file only owns the hardware. */
#include "account.h"
#include "audio.h"
#include "net.h"
#include "okupa_music.h"
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
static char save_path[40], temp_path[40], net_dir[40], key_path[40], look_path[40];
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
    ok_dsi_audio_pump();
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
        snprintf(net_dir, sizeof net_dir, "%sokupa/", roots[i]);
        snprintf(key_path, sizeof key_path, "%sokupa/account.key", roots[i]);
        snprintf(look_path, sizeof look_path, "%sokupa/look.txt", roots[i]);
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

/* What the okupa wears: three small numbers in a text file of its own. */
static void load_look(ok_look *look)
{
    unsigned w, t, c;
    FILE *f = fopen(look_path, "r");
    if (!f) return;
    if (fscanf(f, "%u %u %u", &w, &t, &c) == 3 && w < OK_WEARS && t < OK_TOPS && c < OK_COLOURS) {
        look->wear = (uint8_t)w;
        look->top = (uint8_t)t;
        look->colour = (uint8_t)c;
    }
    fclose(f);
}

static int save_look(const ok_look *look)
{
    FILE *f = fopen(look_path, "w");
    int ok;
    if (!f) return 0;
    ok = fprintf(f, "%u %u %u\n", look->wear, look->top, look->colour) > 0;
    return fclose(f) == 0 && ok;
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
    int top, bottom, held = 0, redraw = 1, storage, fails, show_net = 0;
    uint32_t frame_at, late = 0, draw_us = 0;
    ok_look worn;
    time_t now = time(NULL), last = now, saved_at = now;
    uint8_t saved[OK_SAVE_SIZE], current[OK_SAVE_SIZE];
    struct tm *tm;
    touchPosition touch;
    ok_pet pet;
    ok_ui ui;

    defaultExceptionHandler(); /* a crash shows the registers and the address, not just "Data abort" */
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
        load_look(&ui.look);
        ok_dsi_net_files(net_dir);
    }
    cpuStartTiming(0); /* runs for good: the checks, the network's entropy and the music read it */
    ok_dsi_audio_start();
    ok_dsi_net_start();
    ok_save_write(&pet, ui.drawing, saved);
    worn = ui.look;
    frame_at = cpuGetTiming();

    for (;;) {
        int ticks = 0;
        uint32_t t;
        cothread_yield_irq(IRQ_VBLANK); /* not swiWaitForVBlank: Wi-Fi runs in another thread */
        scanKeys();
        ok_dsi_audio_pump();
        now = time(NULL);
        if (now != last) {
            ok_pet_update(&pet, (int64_t)now);
            last = now;
            tm = localtime(&now);
            ok_dsi_audio_tune(pet.asleep ? OK_TUNE_SLEEP : tm->tm_hour >= 21 || tm->tm_hour < 7 ? OK_TUNE_NIGHT : OK_TUNE_DAY);
        }
        /* The okupa lives in real time: one tick per 1/60 s that passed, even
         * when drawing a frame took longer than that. */
        t = cpuGetTiming(); /* bus clock: wraps every 128 s, the difference does not care */
        late += t - frame_at;
        frame_at = t;
        while (late >= 560190 && ticks < 8) { /* one DS frame, 1/59.83 s, in bus cycles */
            late -= 560190;
            ticks++;
            if (ok_ui_tick(&ui, &pet)) redraw = 1;
        }
        if (ticks == 8) late = 0;
        if (ui.sound) {
            ok_dsi_audio_sfx(ui.sound - 1);
            ui.sound = 0;
        }
        if (redraw) {
            snprintf(ui.net_status, sizeof ui.net_status, "%s", ok_dsi_net_summary());
            if (show_net && ui.scene == OK_UI_DEV) memcpy(ui.dev, ok_dsi_net_lines(), sizeof ui.dev);
            else if (ui.scene == OK_UI_DEV && ui.dev[OK_UI_DEV_LINES - 1][0])
                snprintf(ui.dev[OK_UI_DEV_LINES - 4], sizeof ui.dev[0], "Frame: %lu.%lu ms to draw",
                         (unsigned long)(draw_us / 1000), (unsigned long)(draw_us / 100 % 10));
            {
                uint8_t hash[32];
                if (ok_dsi_net_pending(hash))
                    snprintf(ui.wallet_status, sizeof ui.wallet_status, "Hello %02x%02x%02x%02x: %d nodes", hash[0], hash[1],
                             hash[2], hash[3], ok_dsi_net_handed());
            }
        }
        if (keysHeld() & KEY_TOUCH) {
            int r;
            touchRead(&touch);
            r = ok_ui_touch(&ui, &pet, touch.px, touch.py, held, (int64_t)now);
            held = 1;
            if (r == OK_UI_RUN_CHECKS) {
                memset(ui.dev, 0, sizeof ui.dev);
                snprintf(ui.dev[0], sizeof ui.dev[0], "Running the core checks...");
                show_net = 0;
                tm = localtime(&now);
                ok_ui_render(&ui, &pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
                present(top, bottom);
                fails = ok_checks_run(ui.dev, OK_UI_DEV_LINES - 4, now_us);
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
                ok_dsi_wifi_connect(ssid, ui.password, ui.wifi_status, show_now);
                ok_wipe(ui.password, sizeof ui.password);
                ui.password_len = 0;
            }
            if (r == OK_UI_WALLET_OPEN) {
                snprintf(ui.wallet_network, sizeof ui.wallet_network, "%s", ok_dsi_net_name());
                ok_dsi_account_open(storage ? key_path : NULL, &ui);
            }
            if (r == OK_UI_WALLET_SCRIBBLE) ok_dsi_account_scribble(touch.px, touch.py, &ui);
            if (r == OK_UI_WALLET_HELLO && storage) {
                /* One hello at a time: while one is waiting, pressing again
                 * just keeps handing out the same bytes. */
                uint8_t tx[512], hash[32];
                size_t len;
                if (!ok_dsi_net_pending(hash)) {
                    uint64_t nonce = ok_dsi_net_take_nonce();
                    if (ok_dsi_account_hello(ok_dsi_net_chain_id(), nonce, tx, sizeof tx, &len, hash))
                        ok_dsi_net_send(tx, len, hash);
                }
            }
            if (r == OK_UI_RUN_NETWORK) show_net = 1; /* the postman is always running: just show it */
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
            if (memcmp(&worn, &ui.look, sizeof worn) && save_look(&ui.look)) worn = ui.look;
        }
        if (redraw) {
            uint32_t start = now_us();
            tm = localtime(&now);
            ok_ui_render(&ui, &pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
            present(top, bottom);
            draw_us = (draw_us * 7 + (now_us() - start)) / 8;
            redraw = 0;
        }
    }
}
