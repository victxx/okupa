/* Renders the two DSi screens on the host into PNGs (top above bottom), so the
 * art can be looked at without a console. Not evidence of what the ARM runs.
 *   build/okupa-preview <out dir> */
#include "okupa_act.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include "png.h"
#include "okupa_session.h"
#include "okupa_tech.h"
#include <stdio.h>
#include <string.h>

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

/* The technical screens, filled with values from a host run against mainnet
 * (run/watch-mainnet-20261106): a preview of the layout, not console data. */
static void tech_shots(const char *dir)
{
    static ok_facts fa;
    static const uint8_t mainnet_genesis[32] = {0xd4, 0xe5, 0x67, 0x40, 0xf8, 0x76, 0xae, 0xf8, 0xc0, 0x10, 0xb8,
                                                0x6a, 0x40, 0xd5, 0xf5, 0x67, 0x45, 0xa1, 0x18, 0xd0, 0x90, 0x6a,
                                                0x34, 0xe6, 0x9a, 0xec, 0x8c, 0x0d, 0xb1, 0xcb, 0x8f, 0xa3};
    ok_tech t;
    char path[512];
    int i;
    memset(&fa, 0, sizeof fa);
    strcpy(fa.network, "mainnet");
    fa.chain_id = 1;
    memcpy(fa.genesis, mainnet_genesis, 32);
    fa.fork_hash[0] = 0x07, fa.fork_hash[1] = 0xc9, fa.fork_hash[2] = 0x46, fa.fork_hash[3] = 0x2e;
    strcpy(fa.dns_tree, "all.mainnet.ethdisco.net");
    fa.dns_seq = 1, fa.dns_verified = 1, fa.dns_records = 31, fa.dns_nodes = 24;
    fa.v4_sent = 1441, fa.v4_received = 1597, fa.v4_nodes = 384, fa.candidates = 141, fa.tried = 141;
    fa.reached_status = 2, fa.full = 96;
    fa.peer_count = 3;
    fa.peers[0].ip[0] = 50, fa.peers[0].ip[1] = 21, fa.peers[0].ip[2] = 173, fa.peers[0].ip[3] = 102;
    fa.peers[0].port = 30303, fa.peers[0].stage = OK_STAGE_HELD, fa.peers[0].disconnect = -1;
    strcpy(fa.peers[0].client, "Nethermind/v2.0.0+bec830cd-h/linux-x64/dotnet10");
    fa.peers[0].latest = 26357123;
    fa.peers[1].ip[0] = 51, fa.peers[1].ip[1] = 210, fa.peers[1].ip[2] = 24, fa.peers[1].ip[3] = 225;
    fa.peers[1].port = 30303, fa.peers[1].stage = OK_STAGE_STATUS, fa.peers[1].disconnect = 4;
    strcpy(fa.peers[1].client, "reth/v2.10.0-575a300/x86_64-unknown-linux-gnu");
    fa.peers[2].ip[0] = 162, fa.peers[2].ip[1] = 19, fa.peers[2].ip[2] = 103, fa.peers[2].ip[3] = 123;
    fa.peers[2].port = 30400, fa.peers[2].stage = OK_STAGE_ACK, fa.peers[2].disconnect = 4;
    fa.best_head = 26357125;
    fa.blocks_checked = 2, fa.incl_state = 1;
    strcpy(fa.build, "preview");
    fa.dsi_mode = 1;
    for (t.page = 0, ok_tech_init(&t); t.page < OK_TECH_PAGES; t.page++) {
        t.ticks = 20;
        ok_tech_render(&t, &fa, ok_font(), top, bottom);
        snprintf(path, sizeof path, "%s/tech-%d.png", dir, t.page);
        png(path);
    }
    ok_tech_init(&t);
    for (i = 0; i < OK_TECH_STEPS; i++) {
        t.step = i;
        t.ticks = 25;
        ok_tech_render(&t, &fa, ok_font(), top, bottom);
        snprintf(path, sizeof path, "%s/how-%d.png", dir, i + 1);
        png(path);
    }
}

/* Six little pictures as a player might draw them, for the wall. */
static void doodle(uint8_t *d, int kind)
{
    int x, y;
    memset(d, 0, OK_DRAWING_BYTES);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int dx = x - 32, dy = y - 32, r2 = dx * dx + dy * dy, on = 0;
            switch (kind) {
            case 0: on = (r2 < 22 * 22 && r2 > 19 * 19) || (r2 < 9 && (dx == -8 || dx == 8 || 1) && dy == -6 && (dx < -5 || dx > 5)) ||
                         (dy > 4 && dy < 8 && dx * dx < 120 && r2 > 100); break; /* a smiley */
            case 1: on = (y > 30 && y < 56 && x > 14 && x < 50 && (x == 15 || x == 49 || y == 55 || y == 31)) ||
                         (y <= 30 && y > 8 && (y - 8 == 32 - x || y - 8 == x - 32)) || (x > 27 && x < 36 && y > 42 && y < 56 && (x == 28 || x == 35 || y == 43)); break; /* a house */
            case 2: on = ((y + (x / 6 % 2 ? x % 6 : 6 - x % 6)) % 14) < 2; break; /* waves */
            case 3: on = r2 < 12 * 12 || ((dx == 0 || dy == 0 || dx == dy || dx == -dy) && r2 > 16 * 16 && r2 < 27 * 27); break; /* a sun */
            case 4: on = (y > 50 && y < 53) || (x > 30 && x < 34 && y > 20 && y < 52) || ((x - 32) * (x - 32) + (y - 18) * (y - 18) * 2 < 120); break; /* a tree */
            default: on = ((dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy) < 24 && (dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy) > 20) || r2 < 16; break; /* a gem */
            }
            if (on) ok_drawing_set(d, (unsigned)x, (unsigned)y, 1);
        }
}

/* The wall full of pictures seen in blocks, and a new one being hung. */
static void wall_shots(const char *dir, ok_ui *u, const ok_pet *p)
{
    char path[512];
    int k, i;
    static const int at[3] = {10, 34, 70};
    u->scene = OK_UI_HOME;
    u->ready = 0;
    u->wall_known = 0;
    u->wall_count = 5;
    for (k = 0; k < 6; k++) doodle(u->wall[k], k);
    for (i = 0; i < 30; i++) ok_ui_tick(u, p);
    u->wall_count = 6; /* the sixth one is seen in a block */
    for (k = 0; k < 3; k++) {
        while (u->hang_t < at[k] && i++ < 400) ok_ui_tick(u, p);
        ok_ui_render(u, p, 11, 20, ok_font(), top, bottom);
        snprintf(path, sizeof path, "%s/home-hang-%d.png", dir, k + 1);
        png(path);
    }
    while (u->hang_t) ok_ui_tick(u, p);
    shot(dir, "home-wall", u, p, 11);
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
    {
        static ok_ui w;
        ok_ui_init(&w);
        wall_shots(dir, &w, &p);
        for (i = 0; i < 12; i++) ok_house_frame(i, NULL);
    }
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
    u.scene = OK_UI_WALLET;
    u.wallet_progress = 45;
    shot(dir, "wallet-scribble", &u, &p, 15);
    u.wallet_ready = 1;
    strcpy(u.wallet_address, "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F");
    shot(dir, "wallet", &u, &p, 15);
    tech_shots(dir);
    return 0;
}
