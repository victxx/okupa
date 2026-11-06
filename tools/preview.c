/* Renders the two DSi screens on the host into PNGs (top above bottom), so the
 * art can be looked at without a console. Not evidence of what the ARM runs.
 *   build/okupa-preview <out dir> */
#include "okupa_act.h"
#include "okupa_eth.h"
#include "okupa_font.h"
#include "okupa_ui.h"
#include "okupa_session.h"
#include "okupa_tech.h"
#include <stdio.h>
#include <string.h>

#define W OK_UI_W
#define H (2 * OK_UI_H + 8)

static uint16_t top[OK_UI_W * OK_UI_H], bottom[OK_UI_W * OK_UI_H];
static uint8_t raw[H * (1 + 3 * W)];

static void be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint32_t crc = ok_crc32(ok_crc32(0, type, 4), data, n);
    be32(f, n);
    fwrite(type, 1, 4, f);
    fwrite(data, 1, n, f);
    be32(f, crc);
}

/* PNG with a zlib stream of stored (uncompressed) blocks: simple and exact. */
static void png(const char *path)
{
    static uint8_t z[sizeof raw + sizeof raw / 65535 * 5 + 64];
    uint8_t ihdr[13] = {0, 0, W >> 8, W & 255, 0, 0, H >> 8, H & 255, 8, 2, 0, 0, 0};
    uint32_t a = 1, b = 0;
    size_t i, at = 0, zn = 0;
    int y, x;
    FILE *f;
    for (y = 0; y < H; y++) {
        raw[y * (1 + 3 * W)] = 0;
        for (x = 0; x < W; x++) {
            uint16_t c = y < OK_UI_H ? top[y * W + x] : y >= OK_UI_H + 8 ? bottom[(y - OK_UI_H - 8) * W + x] : 0x8000;
            uint8_t *p = raw + y * (1 + 3 * W) + 1 + 3 * x;
            p[0] = (uint8_t)((c & 31) * 255 / 31);
            p[1] = (uint8_t)((c >> 5 & 31) * 255 / 31);
            p[2] = (uint8_t)((c >> 10 & 31) * 255 / 31);
        }
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    while (at < sizeof raw) {
        size_t n = sizeof raw - at > 65535 ? 65535 : sizeof raw - at;
        z[zn++] = at + n == sizeof raw;
        z[zn++] = (uint8_t)n;
        z[zn++] = (uint8_t)(n >> 8);
        z[zn++] = (uint8_t)~n;
        z[zn++] = (uint8_t)(~n >> 8);
        memcpy(z + zn, raw + at, n);
        zn += n;
        at += n;
    }
    for (i = 0; i < sizeof raw; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    z[zn++] = (uint8_t)(b >> 8);
    z[zn++] = (uint8_t)b;
    z[zn++] = (uint8_t)(a >> 8);
    z[zn++] = (uint8_t)a;
    if (!(f = fopen(path, "wb"))) {
        perror(path);
        return;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    printf("okupa-preview: %s\n", path);
}

static void shot(const char *dir, const char *name, const ok_ui *u, const ok_pet *p, int hour)
{
    char path[512];
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
    p.need[OK_NEED_HUNGER] = 20;
    shot(dir, "home-hungry-evening", &u, &p, 19);
    p.asleep = 1;
    shot(dir, "home-asleep-night", &u, &p, 23);
    p.asleep = 0;
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
    u.scene = OK_UI_WALLET;
    u.wallet_progress = 45;
    shot(dir, "wallet-scribble", &u, &p, 15);
    u.wallet_ready = 1;
    strcpy(u.wallet_address, "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F");
    shot(dir, "wallet", &u, &p, 15);
    tech_shots(dir);
    return 0;
}
