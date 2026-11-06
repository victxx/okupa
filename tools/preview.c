/* Renders the two DSi screens on the host into PNGs (top above bottom), so the
 * art can be looked at without a console. Not evidence of what the ARM runs.
 *   build/okupa-preview <out dir> */
#include "okupa_act.h"
#include "okupa_eth.h"
#include "okupa_font.h"
#include "okupa_ui.h"
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
    return 0;
}
