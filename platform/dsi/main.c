/* Okupa on the Nintendo DSi: the room on the top screen, touch below. The
 * same core and game code as the host; this file only owns the hardware. */
#include "account.h"
#include "audio.h"
#include "mind.h"
#include "senses.h"
#include "net.h"
#include "okupa_music.h"
#include "okupa_checks.h"
#include "okupa.h"
#include "okupa_font.h"
#include "okupa_house.h"
#include "okupa_monkey.h"
#include "okupa_save.h"
#include "okupa_facts.h"
#include "okupa_tech.h"
#include "okupa_keccak.h"
#include "okupa_r3d.h"
#include "okupa_ui.h"
#include "okupa_icons.h"
#include "okupa_text.h"
#include <fat.h>
#include <malloc.h>
#include <nds.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

static uint16_t back[2][OK_UI_W * OK_UI_H];
static int top_bg, bottom_bg;
static ok_ui *shown_ui;
static ok_pet *shown_pet;
static char save_path[40], temp_path[40], net_dir[40], key_path[40], look_path[40], wall_path[40], sent_path[40];
static char letters_path[40]; /* the letters the okupa sent: "YYYY-MM-DD hash text" a line */
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
    ok_r3d_swap();
    present(top_bg, bottom_bg);
}

/* The renderer's divisions on the DS's hardware divider: tens of cycles
 * instead of hundreds for a 64-bit division in software. Interrupts are
 * held off for the few cycles it takes, so nothing else can use the
 * divider half way through. */
static int64_t hw_divide(int64_t n, int64_t d)
{
    int ime = enterCriticalSection();
    int64_t q;
    REG_DIVCNT = DIV_64_64;
    REG_DIV_NUMER = n;
    REG_DIV_DENOM = d;
    while (REG_DIVCNT & DIV_BUSY) {}
    q = REG_DIV_RESULT;
    leaveCriticalSection(ime);
    return q;
}

/* The sky behind the 3D: the mood's 512x256 panorama and its palette, uploaded when the mood changes; each
 * frame only the turn and pitch move it. */
static int sky_bg, sky_mood = -1;
static void sky_layer(int cam_az, int pitch, int focal, int mood, int next, int k)
{
    int m = k >= 128 ? next : mood;
    int32_t pa, pd, cx, cy;
    if (m != sky_mood) {
        uint16_t *dst = bgGetGfxPtr(sky_bg);
        const uint8_t *src = ok_sky_art[m];
        int x, y;
        for (y = 0; y < 256; y++)
            for (x = 0; x < 512; x += 2) dst[(y * 512 + x) / 2] = (uint16_t)(src[y * 512 + x] | src[y * 512 + x + 1] << 8);
        for (x = 0; x < 256; x++) BG_PALETTE[x] = ok_sky_palette[m][x];
        sky_mood = m;
    }
    if (pitch > OK_TURN / 2) pitch -= OK_TURN;
    /* Texels per screen pixel (8 fraction bits): 512 a turn across, 256 rows
     * for 120 degrees down; the screen's centre on where the camera looks. */
    pa = 20861 / focal; /* 256 * 512 / (2 pi focal) */
    pd = 31291 / focal; /* 256 * 256 / (2.094 focal) */
    cx = (((cam_az + OK_TURN / 4) & (OK_TURN - 1)) * 512 / OK_TURN) << 8;
    cy = ((80 * OK_TURN / 360 - pitch) * 256 / (120 * OK_TURN / 360)) << 8;
    REG_BG2PA = (int16_t)pa;
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2PD = (int16_t)pd;
    REG_BG2X = cx - 128 * pa;
    REG_BG2Y = cy - 96 * pd;
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
        snprintf(wall_path, sizeof wall_path, "%sokupa/wall.bin", roots[i]);
        snprintf(sent_path, sizeof sent_path, "%sokupa/drawing-sent.bin", roots[i]);
        snprintf(letters_path, sizeof letters_path, "%sokupa/letters.txt", roots[i]);
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

/* ---- The wall: drawings hung once the console saw them in a block ------------- */

/* The drawing on its way: its pixels and its transaction hash, kept on the SD
 * card so it still gets hung after a restart. */
static uint8_t sent_pixels[512], sent_hash[32];
static int sent_waiting, hang_waiting;

static void load_wall(ok_ui *u)
{
    FILE *f;
    if ((f = fopen(wall_path, "rb"))) {
        uint8_t n = 0;
        if (fread(&n, 1, 1, f) == 1 && n <= OK_UI_WALL && fread(u->wall, 512, n, f) == n) u->wall_count = n;
        fclose(f);
    }
    if ((f = fopen(sent_path, "rb"))) {
        sent_waiting = fread(sent_hash, 1, 32, f) == 32 && fread(sent_pixels, 1, 512, f) == 512;
        fclose(f);
        u->drawing_sent = sent_waiting;
        if (sent_waiting) memcpy(u->drawing, sent_pixels, 512);
    }
}

static void save_wall(const ok_ui *u)
{
    FILE *f = fopen(wall_path, "wb");
    uint8_t n = (uint8_t)u->wall_count;
    if (!f) return;
    fwrite(&n, 1, 1, f);
    fwrite(u->wall, 512, n, f);
    fclose(f);
}

static void hang(ok_ui *u)
{
    if (u->wall_count == OK_UI_WALL) { /* full: the oldest comes down */
        memmove(u->wall[0], u->wall[1], 512 * (OK_UI_WALL - 1));
        u->wall_count--;
    }
    memcpy(u->wall[u->wall_count++], sent_pixels, 512);
    save_wall(u);
    sent_waiting = 0;
    remove(sent_path);
    u->drawing_sent = 0;
    memset(u->drawing, 0, sizeof u->drawing); /* a fresh page */
}

/* The machine's own facts, and the core's timed checks for the DSi page. */
static void machine_facts(int checks)
{
    struct mallinfo mi = mallinfo();
    ok_facts_now.dsi_mode = isDSiMode();
    ok_facts_now.heap_used = (uint32_t)mi.uordblks;
    snprintf(ok_facts_now.build, sizeof ok_facts_now.build, "%.20s", OKUPA_BUILD_ID);
    if (checks && !ok_facts_now.checks_n) {
        int k;
        ok_checks_run(ok_facts_now.checks, 10, now_us);
        for (k = 0; k < 10 && ok_facts_now.checks[k][0]; k++) {}
        ok_facts_now.checks_n = k;
    }
}

/* A verified decision of the okupa's mind becomes what the 3D okupa does:
 * its move, a mood while it is fresh and the line it says. The mind only
 * suggests going to bed; the pet decides whether it is tired enough. */
static void obey(ok_ui *u, ok_pet *p, const ok_mind *d, int64_t now)
{
    static const int moves[OK_ACTIVITIES] = {OK_MOVE_LOOK,   OK_MOVE_SCRATCH, OK_MOVE_STRETCH, OK_MOVE_HOP,
                                             OK_MOVE_PEEK,   OK_MOVE_WANDER,  OK_MOVE_SIGH,    OK_MOVE_WAVE,
                                             OK_MOVE_EAT,    OK_MOVE_PLAY,    OK_MOVE_IDLE,    OK_MOVE_TO_DOOR,
                                             OK_MOVE_TO_DOOR};
    static const int poses[OK_FEELINGS] = {OK_POSE_HAPPY, OK_POSE_HUNGRY, OK_POSE_SLEEPY,
                                           OK_POSE_BORED, OK_POSE_LONELY, OK_POSE_IDLE};
    /* When the mind stays quiet, the bubble shows what it decided to do, so
     * every decision from 0G is seen; the body's own idle glances and small
     * moves between decisions are local animation, never shown as said. */
    static const char *const doing[OK_ACTIVITIES] = {"*looks around*", "*scratches*", "*stretches*", "*hops*",
                                                     "*peeks*",        "*wanders*",   "*sighs*",     "*waves*",
                                                     "*munches*",      "*plays*",     "*yawns*",     "*heads out*",
                                                     "*heads in*"};
    static int last_activity = -1;
    int move = moves[d->activity], where = OK_PLACE_ATTIC, here = ok_house_where();
    /* Its line (or what it does, if it says nothing); its thought in a
     * cloud; its question waits in its own bubble for the player to answer
     * by holding L. All three come from the verified decision. */
    const char *said = d->line[0] ? d->line : d->thought[0] ? NULL : doing[d->activity];
    /* It carries on with the same thing, saying nothing: let the body keep
     * going instead of restarting it, and keep the bubble for news. */
    if (d->activity == last_activity && !d->line[0] && !d->thought[0] && !d->ask[0]) return;
    if (d->activity == last_activity && !d->line[0]) said = NULL;
    last_activity = d->activity;
    if (d->activity == OK_MIND_GO_OUTSIDE) where = OK_PLACE_GARDEN;
    if ((d->activity == OK_MIND_GO_OUTSIDE && here == OK_PLACE_GARDEN) ||
        (d->activity == OK_MIND_GO_INSIDE && here == OK_PLACE_ATTIC))
        move = OK_MOVE_LOOK; /* already there: it just looks around */
    if (move == OK_MOVE_PEEK && u->look.wear != OK_WEAR_SMILEY) move = OK_MOVE_LOOK; /* peeking needs its mask */
    /* The body first: where it goes, what it looks at, how lively; then the
     * move it makes there. -1 leaves each to its instinct. */
    ok_ui_body(u, d->x, d->z, d->gaze >= 0 && d->gaze < OK_GAZES ? d->gaze : -1, d->energy);
    ok_ui_direct(u, p, move, where, poses[d->mood], said);
    ok_ui_listening(u, 0);
    if (d->thought[0]) ok_ui_thought(u, d->thought);
    if (d->ask[0]) ok_ui_ask(u, d->ask);
    if (d->activity == OK_MIND_SLEEP && !p->asleep) ok_pet_do(p, OK_DO_SLEEP, now);
}

/* ---- Watchdog: if the game loop stops for 2 s, the vertical blank
 * interrupt (which still runs) writes on the bottom screen where the
 * network and the mind threads are, so a freeze explains itself. ----------- */
static volatile uint32_t heartbeat;
static void watch_text(uint16_t *fb, int x, int y, const char *s)
{
    const uint8_t (*font)[8] = ok_font();
    int row, col;
    for (; *s && x < 248; s++, x += 8)
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                fb[(y + row) * 256 + x + col] = (font[(unsigned char)*s & 127][row] >> col & 1) ? 0xffff : 0x801f;
}

static void watchdog(void)
{
    static uint32_t seen, still;
    if (heartbeat != seen) {
        seen = heartbeat;
        still = 0;
        return;
    }
    if (++still == 120 || (still > 120 && still % 60 == 0)) {
        uint16_t *fb = bgGetGfxPtr(bottom_bg);
        char t[40];
        snprintf(t, sizeof t, "STUCK %lus", (unsigned long)(still / 60));
        watch_text(fb, 0, 0, t);
        snprintf(t, sizeof t, "net: %.24s", ok_dsi_net_where ? ok_dsi_net_where : "-");
        watch_text(fb, 0, 10, t);
        snprintf(t, sizeof t, "mind: %.23s", ok_dsi_mind_where ? ok_dsi_mind_where : "-");
        watch_text(fb, 0, 20, t);
    }
}

/* The day of the last letter the okupa sent, from letters.txt. */
static void last_letter_day(char out[11])
{
    char line[256];
    FILE *f;
    out[0] = 0;
    if (!letters_path[0] || !(f = fopen(letters_path, "r"))) return;
    while (fgets(line, sizeof line, f))
        if (strlen(line) > 10) snprintf(out, 11, "%.10s", line);
    fclose(f);
}

/* A letter its mind wrote, signed and handed to the network with capped
 * fees (account.c: at most 40 000 gas x 2 gwei); one a day. */
static int send_letter(const char *text, char sent_day[11], const char *today)
{
    uint8_t tx[400], hash[32];
    size_t len;
    FILE *f;
    if (ok_dsi_net_pending(hash)) return 0; /* one at a time */
    if (!ok_dsi_account_letter(ok_dsi_net_chain_id(), ok_dsi_net_take_nonce(), text, tx, sizeof tx, &len, hash)) return 0;
    ok_dsi_net_send(tx, len, hash);
    ok_facts_log(ok_dsi_uptime(), 'E', "signed letter %02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
    if ((f = fopen(letters_path, "a"))) {
        fprintf(f, "%s %02x%02x%02x%02x%02x%02x%02x%02x %s\n", today, hash[0], hash[1], hash[2], hash[3], hash[4], hash[5],
                hash[6], hash[7], text);
        fclose(f);
    }
    snprintf(sent_day, 11, "%s", today);
    return 1;
}

/* The Mail button: the letters the okupa sent (letters.txt: "date hash
 * text" a line), the newest one in the bubble. */
static void show_mail(ok_ui *u, const char *dir)
{
    static char said[200];
    char path[64], line[256], last[256] = "", on_path[64];
    int count = 0, on = 0;
    FILE *f;
    snprintf(on_path, sizeof on_path, "%.40sletters-on.txt", dir);
    if ((f = fopen(on_path, "r"))) {
        on = 1;
        fclose(f);
    }
    snprintf(path, sizeof path, "%.40sletters.txt", dir);
    if ((f = fopen(path, "r"))) {
        while (fgets(line, sizeof line, f))
            if (strlen(line) > 28) {
                count++;
                snprintf(last, sizeof last, "%s", line);
            }
        fclose(f);
    }
    last[strcspn(last, "\r\n")] = 0;
    if (count)
        snprintf(said, sizeof said, "%d letter%s on Ethereum. Last, %.10s: %.120s", count, count > 1 ? "s" : "", last,
                 last + 28);
    else
        snprintf(said, sizeof said, on ? "No letters yet. I'll write you one when something matters."
                                       : "No letters yet. Letters are off (letters-on.txt on the SD).");
    u->say = said;
    u->say_ticks = 60 * 8;
}

int main(void)
{
    int top, bottom, held = 0, redraw = 1, storage, fails, show_net = 0, tech_on = 0, show_mind = 0, threads_on = 0;
    uint32_t frame_at, late = 0, draw_us = 0, loop_us = 0, loop_at = 0;
    ok_look worn;
    ok_tech tech;
    time_t now = time(NULL), last = now, saved_at = now, last_input = now;
    /* The big state lives outside the stack: the main stack is the DSi's
     * 16 KB of DTCM, shared with the interrupt and system stacks, and the
     * game's state (the wall alone is 3 KB) plus a deep render could reach
     * past it. */
    static ok_mind thought; /* the mind's latest decision */
    int listening = 0; /* a clip is with 0G */
    int menu_talk = 0, menu_look = 0, menu_games = 0; /* the menu's Talk (1 start, 2 held, 3 let go), Photo, Games */
    int game_waiting = 0, game_asked = 0, game_okupa = 0, game_you = 0; /* Pictionary */
    char game_guess[OK_MIND_TEXT + 1] = "";
    static uint8_t saved[OK_SAVE_SIZE], current[OK_SAVE_SIZE];
    struct tm *tm;
    touchPosition touch;
    static ok_pet pet;
    static ok_ui ui;

    defaultExceptionHandler(); /* a crash shows the registers and the address, not just "Data abort" */
    /* Top screen: the 3D hardware (BG0), the game's 2D layer over it (BG3,
     * see-through where a pixel's bit 15 is clear) and the sky behind it
     * (BG2, a 512x256 256-colour panorama the hardware turns and scales). */
    videoSetMode(MODE_5_3D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankD(VRAM_D_TEXTURE);
    top_bg = top = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    sky_bg = bgInit(2, BgType_Bmp8, BgSize_B8_512x256, 8, 0);
    bgWrapOn(sky_bg);
    bgSetPriority(top, 0);
    bgSetPriority(sky_bg, 3);
    bottom_bg = bottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    glInit();
    glEnable(GL_TEXTURE_2D | GL_OUTLINE | GL_BLEND);
    REG_BG0CNT = (REG_BG0CNT & ~3u) | 1u; /* the 3D between the 2D layer and the sky */
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    {
        int i;
        for (i = 0; i < 8; i++) glSetOutlineColor(i, RGB15(3, 2, 2));
    }
    ok_house_sky_layer = sky_layer;
    ok_icons_build(); /* drawn once in software, before the 3D goes to the hardware */
    ok_r3d_hardware(1);
    if (isDSiMode()) setCpuClock(true);

    ok_ui_init(&ui);
    ok_tech_init(&tech);
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
        load_wall(&ui);
    }
    machine_facts(0);
    irqSet(IRQ_VBLANK, watchdog);
    irqEnable(IRQ_VBLANK);
    cpuStartTiming(0); /* runs for good: the checks, the network's entropy and the music read it */
#ifndef OKUPA_SOFT_DIV
    ok_r3d_divider(hw_divide);
#else
    (void)hw_divide;
#endif
    ok_house_clock = now_us;
    ok_dsi_audio_start();
    ok_ui_start_intro(&ui);
    ok_dsi_audio_tune(OK_TUNE_INTRO);
    ok_dsi_mind_observe(&pet, 0, 0, "turned the console on");
    /* The network and the mind start once the intro is over: their crypto
     * shares this one CPU and the film needs all of it. */
    ok_save_write(&pet, ui.drawing, saved);
    worn = ui.look;
    frame_at = cpuGetTiming();

    for (;;) {
        int ticks = 0;
        uint32_t t;
        cothread_yield_irq(IRQ_VBLANK); /* not swiWaitForVBlank: Wi-Fi runs in another thread */
        heartbeat++;
        scanKeys();
        ok_dsi_audio_pump();
        now = time(NULL);
        if (!threads_on && ui.scene != OK_UI_SPLASH && ui.scene != OK_UI_FILM) {
            threads_on = 1;
            /* OKUPA_NO_NET / NO_POSTMAN / NO_MIND: builds for measuring and
             * for finding which thread holds the CPU. */
#if !defined(OKUPA_NO_NET) && !defined(OKUPA_NO_POSTMAN)
            ok_dsi_net_start();
#endif
#if !defined(OKUPA_NO_NET) && !defined(OKUPA_NO_MIND)
            ok_dsi_mind_start(storage ? net_dir : NULL);
#endif
        }
        /* SELECT: the technical side, over the same running game. */
        if (keysDown() & KEY_SELECT) {
            tech_on = !tech_on;
            if (tech_on) {
                show_now();
                machine_facts(1);
            }
            redraw = 1;
        }
        if (tech_on) {
            int k = keysDown();
            if (k & KEY_R) ok_tech_page(&tech, 1);
            if (k & KEY_L) ok_tech_page(&tech, -1);
            if (k & (KEY_RIGHT | KEY_A)) ok_tech_step(&tech, 1);
            if (k & (KEY_LEFT | KEY_B)) ok_tech_step(&tech, -1);
            if (k) redraw = 1;
        }
        {
            uint8_t inc_hash[32];
            uint64_t inc_block;
            if (ok_dsi_net_take_included(inc_hash, &inc_block)) {
                static char said[48];
                if (sent_waiting && !memcmp(inc_hash, sent_hash, 32)) {
                    hang_waiting = 1; /* hung below, in front of the player (not behind SELECT) */
                    snprintf(said, sizeof said, "Block %lu has my drawing. On the wall!", (unsigned long)inc_block);
                } else {
                    snprintf(said, sizeof said, "My hello is in block %lu!", (unsigned long)inc_block);
                }
                ui.say = said;
                ui.say_ticks = 600;
                snprintf(ui.wallet_status, sizeof ui.wallet_status, "In block %lu", (unsigned long)inc_block);
                redraw = 1;
            }
        }
        if (hang_waiting && !tech_on) {
            hang(&ui);
            hang_waiting = 0;
        }
        if (now != last) {
            ok_pet_update(&pet, (int64_t)now);
            last = now;
            tm = localtime(&now);
            ok_dsi_audio_tune(ui.scene == OK_UI_SPLASH || ui.scene == OK_UI_FILM ? OK_TUNE_INTRO
                              : pet.asleep ? OK_TUNE_SLEEP : tm->tm_hour >= 21 || tm->tm_hour < 7 ? OK_TUNE_NIGHT : OK_TUNE_DAY);
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
        { /* A safety net: each pass waits for a VBlank, so twenty passes
           * without one 1/60 s on the bus clock mean that clock has stopped
           * or jumped back. The okupa goes on a tick a pass, and it is logged. */
            static int still, said;
            if (ticks) {
                still = 0;
            } else if (++still > 20) {
                still = 0;
                late = 0;
                frame_at = cpuGetTiming();
                if (ok_ui_tick(&ui, &pet)) redraw = 1;
                redraw = 1;
                if (!said) ok_facts_log(ok_dsi_uptime(), 'D', "frame clock stuck at %08x, ticking anyway", (unsigned)frame_at);
                said = 1;
            }
        }
        if (ui.sound) {
            ok_dsi_audio_sfx(ui.sound - 1);
            ui.sound = 0;
        }
        if (redraw) {
            snprintf(ui.net_status, sizeof ui.net_status, "%s", ok_dsi_net_summary());
            if (show_net && ui.scene == OK_UI_DEV) memcpy(ui.dev, ok_dsi_net_lines(), sizeof ui.dev);
            else if (show_mind && ui.scene == OK_UI_DEV) memcpy(ui.dev, ok_dsi_mind_lines(), sizeof ui.dev);
            else if (ui.scene == OK_UI_DEV && ui.dev[OK_UI_DEV_LINES - 1][0])
                snprintf(ui.dev[OK_UI_DEV_LINES - 4], sizeof ui.dev[0], "Frame: draw %u.%u, loop %u ms",
                         (unsigned)(draw_us / 1000 % 1000), (unsigned)(draw_us / 100 % 10), (unsigned)(loop_us / 1000 % 10000));
            {
                uint8_t hash[32];
                if (ok_dsi_net_pending(hash))
                    snprintf(ui.wallet_status, sizeof ui.wallet_status, "%s %02x%02x%02x%02x: %d nodes",
                             sent_waiting ? "Drawing" : "Hello", hash[0], hash[1], hash[2], hash[3], ok_dsi_net_handed());
            }
        }
        /* The okupa's mind on 0G lives in its own thread: tell it what the
         * okupa notices, take what it decided. Y on the Dev screen shows it
         * and pauses or resumes it. */
        if (keysDown() || (keysHeld() & KEY_TOUCH)) last_input = now;
        ok_dsi_mind_observe(&pet, ok_house_where() == OK_PLACE_GARDEN, (uint32_t)(now - last_input),
                            !tech_on && (keysDown() & KEY_TOUCH) ? "touched the screen" : NULL);
        /* Talk to it: hold L and speak, let go, and 0G turns it into words it
         * answers. R: it looks at you through the camera. (On the SELECT
         * screens L and R turn pages instead.) */
        if (!tech_on && ((keysDown() & KEY_L) || menu_talk == 1) && !ok_dsi_mind_ear_busy()) {
            ok_dsi_ear_start();
            ok_ui_ask(&ui, NULL); /* it is being answered */
            ok_ui_listening(&ui, 1);
            if (menu_talk == 1) menu_talk = 2; /* held on the Talk button */
        } else if (menu_talk == 1) {
            menu_talk = 0;
        }
        if ((((keysUp() & KEY_L) && menu_talk != 2) || menu_talk == 3) && ok_dsi_ear_recording()) {
            menu_talk = 0;
            size_t len;
            const uint8_t *wav = ok_dsi_ear_stop(&len);
            if (wav) {
                ok_dsi_mind_hear(wav, len);
                ok_ui_listening(&ui, 2); /* "Thinking..." until 0G has turned it into words */
            } else {
                ok_ui_listening(&ui, 0);
                ui.say = "Hold L while you talk!";
                ui.say_ticks = 60 * 3;
            }
        }
        if (listening && !ok_dsi_mind_ear_busy() && !ok_dsi_ear_recording()) ok_ui_listening(&ui, 0);
        listening = ok_dsi_mind_ear_busy();
        /* Pictionary against the okupa's mind: X and it guesses the drawing
         * in the sketchbook (0G's vision model, checked here); A says it is
         * right, B wrong. */
        if (menu_talk == 3) menu_talk = 0;
        if (!tech_on && ((keysDown() & KEY_X) || menu_games) && !game_waiting) {
            int k;
            for (k = 0; k < (int)sizeof ui.drawing && !ui.drawing[k]; k++) {}
            if (k == (int)sizeof ui.drawing) {
                ui.say = "Draw something first, then press X!";
                ui.say_ticks = 60 * 3;
            } else {
                ok_dsi_mind_guess(ui.drawing);
                game_waiting = 1;
                ok_ui_listening(&ui, 2);
            }
        }
        {
            int g = ok_dsi_mind_take_guess(game_guess, sizeof game_guess);
            if (g) {
                ok_ui_listening(&ui, 0);
                game_waiting = 0;
            }
            if (g > 0) {
                static char q[64];
                snprintf(q, sizeof q, "Is it a %.30s? A yes, B no", game_guess);
                ok_ui_ask(&ui, q);
                game_asked = 1;
            } else if (g < 0) {
                ui.say = "My head is foggy... no guess this time.";
                ui.say_ticks = 60 * 3;
            }
        }
        if (!tech_on && game_asked && (keysDown() & (KEY_A | KEY_B))) {
            static char said[64], did[96];
            int right = (keysDown() & KEY_A) != 0;
            if (right) game_okupa++;
            else game_you++;
            snprintf(said, sizeof said, right ? "Yes! Okupa %d - You %d" : "Nooo! Okupa %d - You %d", game_okupa,
                     game_you);
            snprintf(did, sizeof did, "said your Pictionary guess '%.30s' was %s", game_guess, right ? "right" : "wrong");
            ok_ui_ask(&ui, NULL);
            ui.say = said;
            ui.say_ticks = 60 * 4;
            ok_dsi_mind_observe(&pet, ok_house_where() == OK_PLACE_GARDEN, 0, did);
            ok_facts_log(ok_dsi_uptime(), 'G', "game: %s, Okupa %d - You %d", right ? "right" : "wrong", game_okupa,
                         game_you);
            game_asked = 0;
        }
        menu_games = 0;
        if (!tech_on && ((keysDown() & KEY_R) || menu_look)) {
            menu_look = 0;
            ok_dsi_mind_look();
            ui.say = "Let me look at you...";
            ui.say_ticks = 60 * 3;
        }
        if (!tech_on && ui.scene == OK_UI_DEV && (keysDown() & KEY_Y)) {
            if (show_mind) ok_dsi_mind_toggle();
            show_mind = 1;
            show_net = 0;
        }
        if (ok_dsi_mind_take(&thought)) obey(&ui, &pet, &thought, (int64_t)now);
        {
            static char comment[OK_MIND_TEXT + 1];
            if (ok_dsi_mind_take_comment(comment, sizeof comment) > 0) /* its verified reaction to the drawing */
                ok_ui_direct(&ui, &pet, OK_MOVE_WAVE, OK_PLACE_ATTIC, OK_POSE_HAPPY, comment);
        }
        /* What its mind made, drew, dreamed or wrote. */
        {
            static char said[80], letter_day[11];
            static int letter_day_read;
            char today[11], text[141], name[21];
            uint8_t pixels[512], hash[32];
            const char *about;
            ok_object obj;
            int w, h;
            strftime(today, sizeof today, "%Y-%m-%d", localtime(&now));
            static int letters_allowed;
            if (!letter_day_read && storage) {
                char on_path[48];
                FILE *f;
                last_letter_day(letter_day);
                letter_day_read = 1;
                /* The mind may post a letter to mainnet by itself (real gas)
                 * only if the player opted in: sd:/okupa/letters-on.txt. */
                snprintf(on_path, sizeof on_path, "%.30sletters-on.txt", net_dir);
                if ((f = fopen(on_path, "r"))) {
                    letters_allowed = 1;
                    ok_facts_now.og_features |= 64;
                    fclose(f);
                }
            }
            ok_dsi_mind_letters(letters_allowed && storage && strcmp(letter_day, today) && !ok_dsi_net_pending(hash));
            if (ok_dsi_mind_take_object(&obj)) {
                /* Its colour names onto the house's palette. */
                static const uint8_t to_palette[OK_MAKE_COLOURS] = {2, 3, 4, 5, 5, 6, 7, 7, 8, 9, 10, 11, 15, 1, 13, 0};
                static uint8_t boxes[OK_MAKE_BOXES][7];
                int b, c;
                for (b = 0; b < obj.count; b++) {
                    for (c = 0; c < 6; c++) boxes[b][c] = obj.box[b][c];
                    boxes[b][6] = to_palette[obj.box[b][6] % OK_MAKE_COLOURS];
                }
                ok_ui_drop_object(&ui, (const uint8_t(*)[7])boxes, obj.count, obj.name); /* it says "I made ...!" */
            }
            if (ok_dsi_mind_take_drawing(pixels, name)) {
                ok_ui_okupa_drawing(&ui, pixels); /* in a gilt frame on the wall */
                snprintf(said, sizeof said, "I drew %.20s!", name);
                ui.say = said;
                ui.say_ticks = 60 * 5;
            }
            {
                const uint16_t *dream = ok_dsi_mind_take_dream(&w, &h, &about);
                if (dream) {
                    ok_ui_dream(&ui, dream, w, h); /* its cloud on the top screen */
                    snprintf(said, sizeof said, "I dreamt of %.50s", about);
                    ui.say = said;
                    ui.say_ticks = 60 * 8;
                }
            }
            if (ok_dsi_mind_take_letter(text)) {
                if (send_letter(text, letter_day, today)) {
                    ui.say = "I wrote you a letter, on Ethereum for good!";
                } else {
                    ui.say = "I wrote you a letter... but could not post it.";
                }
                ui.say_ticks = 60 * 5;
            }
        }
        if (tech_on) {
            if ((keysDown() & KEY_TOUCH)) {
                touchRead(&touch);
                if (ok_tech_touch(&tech, touch.px, touch.py)) redraw = 1;
            }
            if (ui.ticks % 4 == 0) redraw = 1; /* the diagrams move */
            if (redraw) {
                tech.ticks = ui.ticks;
                machine_facts(0);
                ok_tech_render(&tech, &ok_facts_now, ok_font(), back[0], back[1]);
                present(top, bottom);
                redraw = 0;
            }
            continue;
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
                show_mind = 0;
                tm = localtime(&now);
                ok_ui_render(&ui, &pet, tm->tm_hour, tm->tm_min, ok_font(), back[0], back[1]);
                ok_r3d_swap();
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
                static uint8_t tx[512];
                uint8_t hash[32];
                size_t len;
                if (!ok_dsi_net_pending(hash)) {
                    uint64_t nonce = ok_dsi_net_take_nonce();
                    if (ok_dsi_account_hello(ok_dsi_net_chain_id(), nonce, tx, sizeof tx, &len, hash)) {
                        ok_dsi_net_send(tx, len, hash);
                        ok_facts_log(ok_dsi_uptime(), 'E', "signed hello %02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
                    }
                }
            }
            if (r == OK_UI_SEND_DRAWING && storage) {
                static uint8_t tx[700];
                uint8_t hash[32];
                size_t len;
                FILE *f;
                if (!ui.wallet_ready) {
                    ui.say = "I need my key first: Dev > Wallet.";
                    ui.say_ticks = 300;
                } else if (ok_dsi_net_pending(hash)) {
                    ui.say = "One letter at a time: the last one is still on its way.";
                    ui.say_ticks = 300;
                } else if (ok_dsi_account_act(ok_dsi_net_chain_id(), ok_dsi_net_take_nonce(), ui.drawing, tx, sizeof tx,
                                              &len, hash)) {
                    ok_dsi_net_send(tx, len, hash);
                    ok_facts_log(ok_dsi_uptime(), 'E', "signed drawing %02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
                    memcpy(sent_pixels, ui.drawing, 512);
                    memcpy(sent_hash, hash, 32);
                    sent_waiting = 1;
                    ui.drawing_sent = 1;
                    ok_dsi_mind_comment(ui.drawing); /* its mind on 0G looks at the drawing and says something */
                    if ((f = fopen(sent_path, "wb"))) {
                        fwrite(sent_hash, 1, 32, f);
                        fwrite(sent_pixels, 1, 512, f);
                        fclose(f);
                    }
                }
            }
            if (r == OK_UI_RUN_NETWORK) show_net = 1, show_mind = 0; /* the postman is always running: just show it */
            /* The menu's mind buttons do what the keys do: Photo is R, Games
             * is X, Talk is L (held on the button), Mail shows the letters. */
            if (r == OK_UI_PHOTO) menu_look = 1;
            if (r == OK_UI_GAMES) menu_games = 1;
            if (r == OK_UI_TALK && !held) menu_talk = 1;
            if (r == OK_UI_MAIL && storage) show_mail(&ui, net_dir);
            if (r != OK_UI_NOTHING) redraw = 1;
        } else if (held) {
            ok_ui_release(&ui);
            held = 0;
            if (menu_talk == 2) menu_talk = 3; /* let go of Talk: the clip goes to 0G */
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
#ifdef OKUPA_PERF_ALWAYS
            if (1) {
#else
            if (keysHeld() & KEY_R) { /* hold R: how fast it really runs */
#endif
                char perf[64];
                int i;
                const uint32_t *pr = ok_house_prof;
                snprintf(perf, sizeof perf, "%u fps  draw %u  loop %u ms", (unsigned)(loop_us ? 1000000 / loop_us : 0),
                         (unsigned)(draw_us / 1000 % 10000), (unsigned)(loop_us / 1000 % 10000));
                for (i = 0; i < OK_UI_W * 24; i++) back[0][(OK_UI_H - 24) * OK_UI_W + i] = 0x8000;
                ok_text_draw(back[0], &ok_sans_regular, 2, OK_UI_H - 23, perf, 0xffff);
                {
                    int bad, ntex = ok_r3d_textures(&bad);
                    snprintf(perf, sizeof perf, "gpu %u/%u kept %u live %u tex %d/%d", (unsigned)(GFX_POLYGON_RAM_USAGE & 0xfff),
                             (unsigned)(GFX_VERTEX_RAM_USAGE & 0x1fff), (unsigned)(ok_r3d_kept_triangles() % 10000),
                             (unsigned)(ok_r3d_triangles() % 10000), ntex % 1000, bad % 1000);
                }
                (void)pr;
                ok_text_draw(back[0], &ok_sans_regular, 2, OK_UI_H - 11, perf, 0xffff);
            }
            ok_r3d_swap();
            present(top, bottom);
            draw_us = (draw_us * 7 + (now_us() - start)) / 8;
            /* From one drawn frame to the next: drawing, the game, and the
             * network's threads, which run whenever this one waits. */
            if (loop_at) loop_us = (loop_us * 7 + (start - loop_at)) / 8;
            loop_at = start;
            redraw = 0;
        }
    }
}
