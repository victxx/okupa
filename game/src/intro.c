#include "okupa_intro.h"
#include "okupa_house.h"
#include "okupa_r3d.h"
#include "okupa_text.h"
#include <stdio.h>
#include <string.h>

#define W 256
#define H 192
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define PAPER_R 30
#define PAPER_G 28
#define PAPER_B 23

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* 0..1000 over [a, b] of t, eased in and out. */
static int ease(uint32_t t, int a, int b)
{
    int u;
    if ((int)t <= a) return 0;
    if ((int)t >= b) return 1000;
    u = ((int)t - a) * 1000 / (b - a);
    return u * u / 1000 * (3000 - 2 * u) / 1000;
}

static uint16_t mix(uint16_t c, uint16_t d, int k) /* k/1000 of d over c */
{
    int r = (c & 31) + ((d & 31) - (c & 31)) * k / 1000, g = (c >> 5 & 31) + ((d >> 5 & 31) - (c >> 5 & 31)) * k / 1000,
        b = (c >> 10 & 31) + ((d >> 10 & 31) - (c >> 10 & 31)) * k / 1000;
    return RGB(r, g, b);
}

static void fill(uint16_t *f, uint16_t c)
{
    int i;
    for (i = 0; i < W * H; i++) f[i] = c;
}

static void fade(uint16_t *f, uint16_t to, int k)
{
    int i;
    if (k <= 0) return;
    for (i = 0; i < W * H; i++) f[i] = mix(f[i], to, k > 1000 ? 1000 : k);
}

static void disc(uint16_t *f, int cx, int cy, int r2x, uint16_t c) /* r2x: radius in half pixels */
{
    int x, y, r = r2x / 2 + 1;
    for (y = cy - r; y <= cy + r; y++)
        for (x = cx - r; x <= cx + r; x++)
            if (x >= 0 && y >= 0 && x < W && y < H && ((x - cx) * (x - cx) + (y - cy) * (y - cy)) * 4 <= r2x * r2x) f[y * W + x] = c;
}

/* Text at an integer scale; dotted draws only every third diagonal, like a
 * coarse print screen. reveal: rows of the glyphs shown so far (0..13*scale). */
static void big_text(uint16_t *f, const ok_font_sans *ft, int cx, int y, const char *s, int scale, uint16_t c, int dotted, int reveal)
{
    int x = cx - ok_text_width(ft, s) * scale / 2, r, b, i, j;
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch >= 127) ch = '?';
        for (r = 0; r < ft->height; r++) {
            uint16_t bits = ft->rows[(ch - 32) * ft->height + r];
            for (b = 0; b < 16; b++)
                if (bits >> b & 1)
                    for (j = 0; j < scale; j++)
                        for (i = 0; i < scale; i++) {
                            int px = x + b * scale + i, py = y + r * scale + j;
                            if (px < 0 || py < 0 || px >= W || py >= H || r * scale + j >= reveal) continue;
                            if (dotted && (px + py) % 3) continue;
                            f[py * W + px] = c;
                        }
        }
        x += ft->advance[ch - 32] * scale;
    }
}

/* ---------------------------------------------------------------- splash */

void ok_intro_splash(uint16_t *top, uint16_t *bottom, uint32_t t)
{
    const uint16_t paper = RGB(PAPER_R, PAPER_G, PAPER_B), red = RGB(26, 6, 6), ink = RGB(4, 3, 3);
    int x, y, grow = ease(t, 20, 90), wipe = ease(t, 70, 112), shine = ease(t, 180, 222);
    char by[16];
    int letters = clamp(((int)t - 118) / 4, 0, 12);
    fill(top, paper);
    fill(bottom, paper);
    /* A sun of halftone dots behind the name, its rays turning slowly. */
    for (y = 3; y < H; y += 6)
        for (x = 3; x < W; x += 6) {
            int dx = x - 128, dy = y - 82, d2 = dx * dx + dy * dy, R = 84 * grow / 1000, k;
            if (!R || d2 >= R * R) continue;
            k = 1000 - d2 * 1000 / (R * R);
            /* twelve rays, every other one paler, turning */
            if ((((ok_r3d_atan2(dy, dx) + (int)t * 6) & (OK_TURN - 1)) * 12 / OK_TURN) % 2) k = k * 55 / 100;
            disc(top, x, y, clamp(k * 6 / 1000, 0, 6), red);
        }
    /* The name, stamped down from the top, with a dotted shadow. */
    big_text(top, &ok_sans_bold, 131, 58, "OKUPA", 4, red, 1, 13 * 4 * wipe / 1000);
    big_text(top, &ok_sans_bold, 128, 55, "OKUPA", 4, ink, 0, 13 * 4 * wipe / 1000);
    /* A shine crossing it. */
    if (shine > 0 && shine < 1000)
        for (y = 55; y < 55 + 52; y++)
            for (x = 0; x < W; x++) {
                int band = (x + (y - 55) / 2) - (shine * 300 / 1000 - 30);
                if (band >= 0 && band < 7 && top[y * W + x] == ink) top[y * W + x] = RGB(31, 31, 30);
            }
    /* by victorxva, typed in. */
    snprintf(by, sizeof by, "%.*s", letters, "by victorxva");
    big_text(top, &ok_sans_bold, 128, 126, by, 1, ink, 0, 99);
    if (t > 118 && letters < 12 && (t / 8) % 2) big_text(top, &ok_sans_bold, 128 + ok_text_width(&ok_sans_bold, by) / 2 + 4, 126, "_", 1, ink, 0, 99);
    /* Below: a halftone falling into the bottom, and a line to tap. */
    for (y = 3; y < H; y += 6)
        for (x = 3; x < W; x += 6) {
            int k = y * 1000 / H;
            disc(bottom, x, y, clamp(k * k / 1000 * 6 / 1000, 0, 6), RGB(24, 21, 16));
        }
    big_text(bottom, &ok_sans_regular, 128, 150, "(c) 2026 victorxva", 1, RGB(12, 10, 8), 0, 99);
    if (t > 200 && (t / 30) % 2) big_text(bottom, &ok_sans_bold, 128, 84, "Touch the screen", 1, ink, 0, 99);
    /* In from black, out to black. */
    fade(top, 0x8000, 1000 - ease(t, 0, 26));
    fade(bottom, 0x8000, 1000 - ease(t, 0, 26));
    fade(top, 0x8000, ease(t, 280, OK_SPLASH_TICKS));
    fade(bottom, 0x8000, ease(t, 280, OK_SPLASH_TICKS));
}

/* ------------------------------------------------------------------ film */

typedef struct {
    int t;
    int eye[3], at[3]; /* hundredths */
} key;

static const key keys[] = {
    {0, {6800, 800, 9800}, {300, -500, 0}},
    {260, {-3600, 1900, 6200}, {-500, 400, 0}},
    {520, {-700, 1050, 2400}, {-700, 790, 580}},
    {700, {-700, 785, 940}, {-700, 778, 580}},
    {OK_FILM_TICKS, {-700, 780, 720}, {-700, 778, 580}},
};

static const struct {
    int from, to;
    const char *line;
} captions[] = {
    {30, 230, "Somewhere, past the edge of the map,"},
    {290, 490, "an empty attic kept its light on..."},
    {540, 690, "...waiting for a tiny squatter."},
};

void ok_intro_film(uint16_t *top, uint16_t *bottom, uint32_t t)
{
    int k = 0, i;
    ok_v3 eye, at;
    while (k + 2 < (int)(sizeof keys / sizeof keys[0]) && (int)t >= keys[k + 1].t) k++;
    {
        int e = ease(t, keys[k].t, keys[k + 1].t), c[6];
        for (i = 0; i < 3; i++) {
            c[i] = keys[k].eye[i] + (keys[k + 1].eye[i] - keys[k].eye[i]) * e / 1000;
            c[3 + i] = keys[k].at[i] + (keys[k + 1].at[i] - keys[k].at[i]) * e / 1000;
        }
        eye = ok_v3_make(c[0] * 4096 / 100, c[1] * 4096 / 100, c[2] * 4096 / 100);
        at = ok_v3_make(c[3] * 4096 / 100, c[4] * 4096 / 100, c[5] * 4096 / 100);
    }
    ok_house_outside(top, eye, at, 300, 20, 40, t);
    /* Cinema bars, and the warm glow of the window filling the screen at the end. */
    for (i = 0; i < W * 20; i++) top[i] = top[(H - 20) * W + i] = 0x8000;
    fade(top, 0x8000, 1000 - ease(t, 0, 50));
    fade(top, RGB(31, 26, 16), ease(t, 690, OK_FILM_TICKS - 20));
    fill(bottom, RGB(2, 2, 3));
    for (i = 0; i < (int)(sizeof captions / sizeof captions[0]); i++) {
        int in = ease(t, captions[i].from, captions[i].from + 40), out = ease(t, captions[i].to - 40, captions[i].to);
        int a = in - out;
        if (a > 0) big_text(bottom, &ok_sans_regular, 128, 86, captions[i].line, 1, mix(RGB(2, 2, 3), RGB(28, 27, 24), a), 0, 99);
    }
    if (t > 700) big_text(bottom, &ok_sans_bold, 128, 70, "OKUPA", 3, mix(RGB(2, 2, 3), RGB(31, 26, 3), ease(t, 700, 760)), 0, 99);
    big_text(bottom, &ok_sans_regular, 128, 176, "Touch to skip", 1, RGB(9, 9, 10), 0, 99);
}
