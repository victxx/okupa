/* Renders the two DSi screens on the host into PNGs (top above bottom), so the
 * art can be looked at without a console. Not evidence of what the ARM runs.
 *   build/okupa-preview <out dir> */
#include "okupa_act.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include "png.h"
#include "okupa_facts.h"
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
    p.need[OK_NEED_ENERGY] = 55;
    u.say = NULL;
    shot(dir, "home-hungry-evening", &u, &p, 19);
    /* As on the DSi with its mind: hearing, camera, Pictionary; a verified decision in the strip. */
    ok_facts_now.og_features = 1 | 2 | 4 | 8;
    snprintf(ok_facts_now.og_state, sizeof ok_facts_now.og_state, "decided");
    snprintf(ok_facts_now.og_said, sizeof ok_facts_now.og_said, "Is there anything to eat?");
    ok_facts_now.og_verified = 1;
    ok_facts_now.og_decisions = 12;
    shot(dir, "home-wired", &u, &p, 19);
    u.page = 1; /* the second menu */
    shot(dir, "home-more", &u, &p, 19);
    u.page = 0;
    {   /* the gallery, two pictures hung; posting a third */
        ok_ui g = u;
        int k;
        memset(g.wall, 0, sizeof g.wall);
        for (k = 0; k < 64 * 64 / 8; k++) g.wall[0][k] = (uint8_t)(k % 8 == 3 ? 0x18 : 0), g.wall[1][k] = (uint8_t)(k / 64 % 2 ? 0x3c : 0);
        g.wall_count = 2;
        g.scene = OK_UI_GALLERY;
        shot(dir, "gallery", &g, &p, 15);
        g.scene = OK_UI_CLOSET;
        shot(dir, "closet-locked", &g, &p, 15);
        g.scene = OK_UI_HOME;
        g.drawing_sent = 1;
        ok_facts_now.candidates = 31;
        ok_facts_now.reached_status = 2;
        ok_facts_now.tx_handed = 2;
        ok_facts_now.blocks_checked = 3;
        for (k = 0; k < 400; k++) ok_ui_tick(&g, &p);
        shot(dir, "home-posting", &g, &p, 15);
        g.drawing_sent = 0;
        snprintf(ok_facts_now.og_state, sizeof ok_facts_now.og_state, "no answer");
        shot(dir, "home-no-answer", &g, &p, 15);
        memset(&ok_facts_now, 0, sizeof ok_facts_now);
    }
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
        /* Its inner life: a thought, a question, listening to the answer. */
        ok_ui_thought(&w, "I wonder what the clouds taste like.");
        shot(dir, "home-thought", &w, &p, 11);
        ok_ui_ask(&w, "What did you have for lunch today?");
        shot(dir, "home-ask", &w, &p, 11);
        ok_ui_listening(&w, 1);
        shot(dir, "home-listening", &w, &p, 11);
        ok_ui_listening(&w, 2);
        shot(dir, "home-thinking", &w, &p, 11);
        /* The player's words as 0G heard them; the cloud waits for its answer. */
        ok_ui_heard(&w, "hola okupa, que tal? hoy he dibujado una casa con un arbol");
        ok_ui_busy(&w, "Thinking what to answer");
        shot(dir, "home-heard", &w, &p, 11);
        ok_ui_heard(&w, NULL);
        ok_ui_busy(&w, NULL);
        ok_ui_listening(&w, 0);
        ok_ui_ask(&w, NULL);
        {   /* What the camera saw, in 0G's words (a made-up photo: this is a layout render, not the camera). */
            static uint16_t photo[128 * 96];
            int x, y;
            for (y = 0; y < 96; y++)
                for (x = 0; x < 128; x++) {
                    int dx = x - 64, dy = y - 44, face = dx * dx + dy * dy < 900;
                    photo[y * 128 + x] = (uint16_t)(0x8000 | (face ? 26 | 20 << 5 | 16 << 10 : (x / 8) | (y / 6) << 5 | 12 << 10));
                }
            ok_ui_card(&w, "I see...", "A smiling person with glasses holding a red mug, a bookshelf behind them", photo, 128,
                       96, 1, "#2  4.3 s  (tap: stop)");
            shot(dir, "home-card-photo", &w, &p, 11);
            ok_ui_card(&w, "My eyes failed", "camera: no frame", NULL, 0, 0, 0, "nothing made up");
            shot(dir, "home-card-failed", &w, &p, 11);
            ok_ui_card(&w, NULL, NULL, NULL, 0, 0, 0, NULL);
        }
        {   /* Pictionary: its own page, and 0G's guess as a question. */
            int k;
            w.scene = OK_UI_SKETCH;
            w.pictionary = 1;
            for (k = 10; k < 54; k++) ok_drawing_set(w.doodle, (unsigned)k, 40, 1), ok_drawing_set(w.doodle, 32, (unsigned)(k - 4), 1);
            ok_ui_ask_hint(&w, "Is it a tree?", "A yes   B no");
            shot(dir, "pictionary", &w, &p, 11);
            ok_ui_ask(&w, NULL);
            w.pictionary = 0;
            w.scene = OK_UI_HOME;
        }
        /* What its mind makes: a thing on the floor, its own drawing, a dream. */
        {
            static const uint8_t chair[5][7] = {{0, 0, 0, 2, 6, 2, 11}, {8, 0, 0, 2, 6, 2, 11}, {0, 0, 8, 2, 6, 2, 11},
                                                {8, 0, 8, 2, 6, 2, 11}, {0, 6, 0, 10, 2, 10, 3}};
            static uint16_t dream[64 * 64];
            uint8_t own[512];
            int x, y;
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++) dream[y * 64 + x] = (uint16_t)(0x8000 | (x / 3) | (y / 3) << 5 | 20 << 10);
            doodle(own, 3);
            ok_ui_drop_object(&w, chair, 5, "a little stool");
            ok_ui_okupa_drawing(&w, own);
            ok_ui_dream(&w, dream, 64, 64);
            ok_ui_body(&w, 70, 60, OK_GAZE_WINDOW, 80);
            shot(dir, "home-mind", &w, &p, 11);
            ok_ui_dream(&w, NULL, 0, 0);
        }
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
    strcpy(u.wallet_balance, "0.000026 ETH");
    strcpy(u.wallet_balance_note, "block 21450590, 12:03");
    u.wallet_low = 1;
    shot(dir, "wallet", &u, &p, 15);
    u.wallet_qr = 1;
    shot(dir, "wallet-qr", &u, &p, 15);
    { /* playing with a friend: waiting, the DS draws, the friend draws */
        static ok_friend g;
        int x;
        u.wallet_qr = 0;
        ok_friend_start(&g, 4821);
        u.friend = &g;
        strcpy(u.friend_where, "192.168.1.23:8080");
        u.scene = OK_UI_FRIENDS;
        shot(dir, "friend-waiting", &u, &p, 15);
        strcpy(g.name, "Alex");
        ok_friend_round(&g, OK_FRIEND_DS_DRAWS);
        for (x = 10; x < 54; x++) ok_drawing_set(u.doodle, (unsigned)x, (unsigned)(20 + (x - 10) / 3), 1);
        strcpy(g.guesses[0], "a mountain");
        strcpy(g.guesses[1], "a hill");
        g.guess_count = 2;
        shot(dir, "friend-ds-draws", &u, &p, 15);
        ok_friend_round(&g, OK_FRIEND_THEY_DRAW);
        for (x = 8; x < 56; x++) ok_drawing_set(g.their_drawing, (unsigned)x, (unsigned)x, 1), ok_drawing_set(g.their_drawing, (unsigned)x, (unsigned)(63 - x), 1);
        ok_friend_guess(&g, "a kiss");
        g.ds_points = 2, g.friend_points = 2;
        shot(dir, "friend-they-draw", &u, &p, 15);
        u.friend = NULL;
    }
    { /* the small games */
        int k;
        u.scene = OK_UI_GAMES_MENU;
        shot(dir, "games", &u, &p, 15);
        u.scene = OK_UI_PLAY;
        u.game = 1;
        ok_catch_start(&u.catching, 5);
        for (k = 0; k < 400; k++) ok_catch_tick(&u.catching);
        u.catching.caught = 7;
        shot(dir, "game-banana", &u, &p, 15);
        u.game = 2;
        ok_simon_start(&u.copying, 9);
        u.copying.rounds = 3;
        u.copying.lit = 2;
        shot(dir, "game-copy", &u, &p, 15);
        u.game = 0;
    }
    tech_shots(dir);
    return 0;
}
