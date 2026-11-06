#include "okupa_ui.h"
#include "okupa_act.h"
#include "okupa_text.h"
#include "okupa_icons.h"
#include "okupa_intro.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <string.h>

/* The top screen is the attic with the okupa in 3D (okupa_house.h); here
 * are the labels over it and the bottom screen's buttons, drawn in plain C. */
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define INK RGB(6, 5, 5)
#define PAPER RGB(31, 30, 27)
#define MUTED RGB(17, 16, 15)

typedef uint16_t *frame;

static void rect(frame f, int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = y < 0 ? 0 : y; j < y + h && j < OK_UI_H; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < OK_UI_W; i++) f[j * OK_UI_W + i] = c;
}

static void disc(frame f, int cx, int cy, int r, uint16_t c)
{
    int i, j;
    for (j = -r; j <= r; j++)
        for (i = -r; i <= r; i++)
            if (i * i + j * j <= r * r + r) rect(f, cx + i, cy + j, 1, 1, c);
}

static void frame_box(frame f, int x, int y, int w, int h, uint16_t fill, uint16_t edge)
{
    rect(f, x + 1, y, w - 2, h, edge);
    rect(f, x, y + 1, w, h - 2, edge);
    rect(f, x + 2, y + 1, w - 4, h - 2, fill);
    rect(f, x + 1, y + 2, w - 2, h - 4, fill);
}

/* Monospace 8x8, for the Dev screen's columns of numbers. */
static void mono(frame f, const uint8_t (*font)[8], int x, int y, const char *s, uint16_t c, int scale)
{
    int row, col;
    if (!font) return;
    for (; *s; s++, x += 8 * scale) {
        unsigned ch = (unsigned char)*s & 127;
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (font[ch][row] >> col & 1) rect(f, x + col * scale, y + row * scale, scale, scale, c);
    }
}

/* Everything else in the sans: weight 1 regular, 2 bold. y is where an 8x8
 * line would start, so the capitals sit in the same place. */
static void text(frame f, const uint8_t (*font)[8], int x, int y, const char *s, uint16_t c, int weight)
{
    (void)font;
    ok_text_draw(f, weight >= 2 ? &ok_sans_bold : &ok_sans_regular, x, y - 2, s, c);
}

static int width(const char *s, int weight)
{
    return ok_text_width(weight >= 2 ? &ok_sans_bold : &ok_sans_regular, s);
}

static void centred(frame f, const uint8_t (*font)[8], int cx, int y, const char *s, uint16_t c, int weight)
{
    text(f, font, cx - width(s, weight) / 2, y, s, c, weight);
}

/* ------------------------------------------------------------ drawing kit */

static uint16_t half(uint16_t c)
{
    return (uint16_t)(0x8000u | (c >> 1 & 0x3def));
}

/* A box with corners rounded to radius r. */
static void round_box(frame f, int x, int y, int w, int h, int r, uint16_t c)
{
    int j;
    for (j = 0; j < h; j++) {
        int k = j < r ? j : j >= h - r ? h - 1 - j : r, in = 0;
        if (k < r) {
            int dy = 2 * r - 2 * k - 1, e = 0;
            while ((e + 1) * (e + 1) <= 4 * r * r - dy * dy) e++;
            in = r - e / 2;
        }
        rect(f, x + in, y + j, w - 2 * in, 1, c);
    }
}

/* Darkens what is behind a rounded box: the glass under the top screen's labels. */
static void glass(frame f, int x, int y, int w, int h)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            int corner = (j == y || j == y + h - 1) && (i == x || i == x + w - 1);
            if (i < 0 || j < 0 || i >= OK_UI_W || j >= OK_UI_H || corner) continue;
            f[j * OK_UI_W + i] = half(half(f[j * OK_UI_W + i])) + (uint16_t)RGB(2, 2, 3) - 0x8000u;
        }
}

/* Pixel art: one character per pixel, '.' clear. */
static uint16_t ink_of(char k)
{
    switch (k) {
    case 'y': return RGB(31, 27, 8);
    case 'Y': return RGB(25, 18, 3);
    case 'k': return RGB(5, 4, 4);
    case 'w': return RGB(31, 31, 30);
    case 'r': return RGB(29, 8, 9);
    case 'R': return RGB(20, 4, 6);
    case 'p': return RGB(31, 19, 22);
    case 't': return RGB(28, 22, 14);
    case 'g': return RGB(17, 18, 20);
    case 'G': return RGB(24, 25, 27);
    case 'b': return RGB(8, 14, 28);
    case 'M': return RGB(30, 28, 16);
    case 'n': return RGB(12, 8, 5);
    case 'e': return RGB(10, 22, 12);
    default: return RGB(31, 0, 31);
    }
}

static void pixels(frame f, int x, int y, const char *const *rows, int n, int scale)
{
    int r, c;
    for (r = 0; r < n; r++)
        for (c = 0; rows[r][c]; c++)
            if (rows[r][c] != '.') rect(f, x + c * scale, y + r * scale, scale, scale, ink_of(rows[r][c]));
}

/* ------------------------------------------------------------ top screen */

static void icons_ready(void)
{
    static int done;
    if (!done) ok_icons_build();
    done = 1;
}

/* The four needs along the bottom: an icon and a bar each, on glass. */
static void needs(frame f, const ok_pet *p)
{
    static const int icon[OK_NEEDS] = {OK_ICON_FEED, OK_ICON_SLEEP, OK_ICON_PLAY, OK_ICON_PET};
    int i;
    for (i = 0; i < OK_NEEDS; i++) {
        int x = 5 + i * 62, y = 173, v = p->need[i] * 34 / OK_NEED_MAX;
        uint16_t c = p->need[i] < 35 ? RGB(30, 9, 7) : p->need[i] < 65 ? RGB(30, 23, 6) : RGB(10, 25, 12);
        glass(f, x, y, 60, 16);
        ok_icons_draw(icon[i], f, x + 10, y + 8, 16);
        rect(f, x + 21, y + 6, 36, 5, RGB(3, 3, 4));
        rect(f, x + 22, y + 7, v, 3, c);
        rect(f, x + 22, y + 7, v, 1, RGB(31, 31, 28)); /* a glint along the top */
        rect(f, x + 22, y + 7, v, 1, c | RGB(8, 8, 8));
    }
}

static void clock_pill(frame f, const uint8_t (*font)[8], int hour, int minute)
{
    static const char *const sun[] = {"...y...", ".y.y.y.", "..yyy..", "yyyYyyy", "..yyy..", ".y.y.y.", "...y..."};
    static const char *const moon[] = {"..MMM..", ".MM....", "MM.....", "MM.....", "MM.....", ".MM..M.", "..MMM.."};
    char clock[8];
    snprintf(clock, sizeof clock, "%02d:%02d", hour % 24, minute % 60);
    glass(f, 4, 4, 56, 13);
    pixels(f, 8, 7, hour >= 7 && hour < 20 ? sun : moon, 7, 1);
    text(f, font, 18, 7, clock, RGB(30, 29, 27), 1);
}

/* Bubbles above the okupa's head: what it says (a speech bubble with a
 * tail), what it thinks (a cloud trailing little circles), what it asks you
 * (a speech bubble with a hint to answer), and the ear it lends you while
 * you answer. Text is up to two lines of at most 200 pixels, broken at spaces. */
enum { SAY, THINK, ASK };

static int wrap(const char *s, char line[2][64])
{
    size_t n = strlen(s), cut = n;
    memset(line, 0, 2 * 64);
    while (cut > 0 && cut < n + 1) {
        char head[64];
        snprintf(head, sizeof head, "%.*s", (int)cut, s);
        if (width(head, 1) <= 200 || cut == 1) break;
        for (cut--; cut > 0 && s[cut] != ' '; cut--) {}
    }
    if (cut == 0) cut = n > 40 ? 40 : n;
    snprintf(line[0], 64, "%.*s", (int)cut, s);
    if (cut < n) snprintf(line[1], 64, "%s", s + cut + (s[cut] == ' '));
    return line[1][0] ? 2 : 1;
}

static void bubble_as(frame f, const uint8_t (*font)[8], const char *s, int hx, int hy, int kind, uint32_t ticks)
{
    char line[2][64];
    int lines = wrap(s, line), w, h, x, y, tx, k, hint = kind == ASK;
    uint16_t edge = kind == ASK ? RGB(26, 17, 3) : INK;
    w = (width(line[0], 1) > width(line[1], 1) ? width(line[0], 1) : width(line[1], 1)) + 16;
    if (hint && w < width("Hold L to answer", 1) + 16) w = width("Hold L to answer", 1) + 16;
    h = lines * 11 + 9 + hint * 10;
    if (hx < -50) hx = 128, hy = 110;
    x = hx - w / 2;
    x = x < 6 ? 6 : x + w > OK_UI_W - 6 ? OK_UI_W - 6 - w : x;
    y = hy - h - (kind == THINK ? 20 : 12);
    if (y < 22) y = 22;
    tx = hx < x + 12 ? x + 12 : hx > x + w - 12 ? x + w - 12 : hx;
    if (kind == THINK) { /* a cloud: puffs all round its edge, then little circles down to the head */
        for (k = 0; k < 2; k++) {
            int b, r = 6, grow = k ? 0 : 1;
            uint16_t c = k ? PAPER : INK;
            round_box(f, x - grow, y - grow, w + 2 * grow, h + 2 * grow, 6, c);
            for (b = x + 5; b <= x + w - 5; b += 11) {
                disc(f, b, y + 1, r + grow, c);
                disc(f, b + 5, y + h - 1, r + grow, c);
            }
            disc(f, x + 1, y + h / 2, r + grow, c);
            disc(f, x + w - 1, y + h / 2, r + grow, c);
            disc(f, tx + (hx - tx) / 3, y + h + 7, 3 + grow, c);
            disc(f, tx + (hx - tx) * 2 / 3, y + h + 13, 2 + grow, c);
        }
    } else {
        round_box(f, x - 1, y + 1, w + 2, h + 2, 6, RGB(2, 2, 3)); /* a soft drop shadow */
        round_box(f, x - 1 - hint, y - 1 - hint, w + 2 + 2 * hint, h + 2 + 2 * hint, 6, edge);
        round_box(f, x, y, w, h, 5, PAPER);
        for (k = 0; k < 6; k++) { /* the tail, leaning towards the head */
            int lean = (hx - tx) * k / 24;
            rect(f, tx - 4 + k / 2 + lean, y + h + k, 8 - k, 1, PAPER);
            rect(f, tx - 5 + k / 2 + lean, y + h + k, 1, 1, edge);
            rect(f, tx + 4 - k / 2 + lean, y + h + k, 1, 1, edge);
        }
    }
    if (kind == ASK) { /* a little question mark badge on its corner */
        disc(f, x + 2, y + 2, 6, edge);
        disc(f, x + 2, y + 2, 5, RGB(31, 26, 8));
        text(f, font, x, y - 2, "?", INK, 1);
    }
    centred(f, font, x + w / 2, y + 6, line[0], INK, 1);
    if (lines == 2) centred(f, font, x + w / 2, y + 17, line[1], INK, 1);
    if (hint) centred(f, font, x + w / 2, y + 6 + lines * 11, "Hold L to answer", MUTED, 1);
    (void)ticks;
}

static void bubble(frame f, const uint8_t (*font)[8], const char *s, int hx, int hy)
{
    bubble_as(f, font, s, hx, hy, SAY, 0);
}

/* While you answer: a round badge by its head with a microphone and rings
 * going out (recording), or a cloud of dots (the answer is being thought over). */
static void listening(frame f, const uint8_t (*font)[8], const ok_ui *u, int hx, int hy)
{
    static const char *const mic[] = {"..kkk..", ".kkkkk.", ".kkwkk.", ".kkkkk.", ".kkkkk.", "k.kkk.k", ".k...k.", "..kkk..", "...k...", ".kkkkk."};
    if (hx < -50) hx = 128, hy = 110;
    if (u->listening == 2) {
        static const char *const dots[] = {"Thinking", "Thinking.", "Thinking..", "Thinking..."};
        bubble_as(f, font, dots[u->ticks / 20 % 4], hx, hy, THINK, u->ticks);
        return;
    }
    {
        int x = hx + 30 > OK_UI_W - 16 ? hx - 30 : hx + 30, y = hy + 4, k, ring = (int)(u->ticks / 6 % 12);
        for (k = 0; k < 2; k++) { /* rings going out from the badge */
            int r = 13 + (ring + k * 6) % 12, a;
            for (a = 0; a < 64; a++) {
                int px = x + (int)((int64_t)ok_r3d_cos(a * 64) * r / 4096), py = y + (int)((int64_t)ok_r3d_sin(a * 64) * r / 4096);
                if ((a / 4) % 2 == 0) rect(f, px, py, 1, 1, RGB(31, 26, 8));
            }
        }
        disc(f, x, y, 11, INK);
        disc(f, x, y, 10, RGB(31, 26, 8));
        pixels(f, x - 3, y - 5, mic, 10, 1);
    }
}

static uint16_t skin[OK_SKIN * OK_SKIN];
static ok_look skin_look = {255, 255, 255};

/* The wall as a number, to notice a new picture. */
static uint32_t wall_sum(const ok_ui *u)
{
    uint32_t s = (uint32_t)u->wall_count + 1;
    int k, i;
    for (k = 0; k < u->wall_count && k < OK_UI_WALL; k++)
        for (i = 0; i < (int)sizeof u->wall[k]; i++) s = s * 31 + u->wall[k][i];
    return s;
}

/* The wall's pictures in the attic's six frames and, the same, on the
 * terrace. One not yet hung waits off the wall for its show. */
static void hang_pictures(const ok_ui *u)
{
    int k, newest = u->wall_count - 1, waiting = u->wall_known && !u->hang_t && wall_sum(u) != u->wall_seen;
    for (k = 0; k < 6; k++) {
        const uint8_t *pic = k < u->wall_count && k < OK_UI_WALL ? u->wall[k] : NULL;
        ok_house_frame(k, waiting && k == newest ? NULL : pic);
        ok_house_frame(6 + k, pic);
    }
    if (u->own_set) { /* its own drawing: the next free frame, else the terrace's last easel */
        int slot = u->wall_count < 6 ? u->wall_count + (waiting ? 0 : 0) : 11;
        if (slot < 6 && waiting && slot == newest) slot++;
        if (slot >= 6 && slot < 11) slot = 11;
        ok_house_frame(slot, u->own);
        if (slot < 6) ok_house_frame(6 + slot, u->own);
        ok_house_own_frame(slot);
    } else {
        ok_house_own_frame(-1);
    }
    ok_house_hanging(u->hang_t ? u->hang_slot : -1, u->hang_t);
}

static void top_screen(frame f, const uint8_t (*font)[8], const ok_ui *u, const ok_pet *p, int hour, int minute,
                       const char *line)
{
    ok_monkey_face face;
    hang_pictures(u);
    if (memcmp(&skin_look, &u->look, sizeof skin_look)) {
        ok_monkey_skin(&u->look, skin);
        ok_r3d_texture_changed(skin);
        skin_look = u->look;
    }
    ok_house_draw(f, hour, minute, p->asleep, NULL, &u->look, skin, &u->anim, &face); /* frames fill as drawings are sent */
    clock_pill(f, font, hour, minute);
    needs(f, p);
    /* One bubble at a time: answering, a question, something just said, a thought, then its mood. */
    if (u->listening) listening(f, font, u, face.top_x, face.top_y);
    else if (u->ask_ticks) bubble_as(f, font, u->ask, face.top_x, face.top_y, ASK, u->ticks);
    else if (u->say) bubble(f, font, line, face.top_x, face.top_y);
    else if (u->thought_ticks) bubble_as(f, font, u->thought, face.top_x, face.top_y, THINK, u->ticks);
    else if (line && !(p->asleep && line == ok_pet_line(p, hour))) bubble(f, font, line, face.top_x, face.top_y);
    if (u->dream_ticks && u->dream_w > 0 && u->dream_h > 0) { /* last night's dream, in a cloud at the top left */
        int big = u->dream_w > u->dream_h ? u->dream_w : u->dream_h, w = u->dream_w * 72 / big, h = u->dream_h * 72 / big;
        int x0 = 10, y0 = 34, x, y;
        round_box(f, x0 - 5, y0 - 15, w + 10, h + 20, 8, INK);
        round_box(f, x0 - 4, y0 - 14, w + 8, h + 18, 7, RGB(27, 26, 31));
        text(f, font, x0, y0 - 11, "I dreamt...", INK, 1);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) f[(y0 + y) * OK_UI_W + x0 + x] = u->dream[(y * u->dream_h / h) * u->dream_w + x * u->dream_w / w] | 0x8000;
        disc(f, x0 + w + 6, y0 + h + 4, 4, INK);
        disc(f, x0 + w + 6, y0 + h + 4, 3, RGB(27, 26, 31));
        disc(f, x0 + w + 14, y0 + h + 12, 3, INK);
        disc(f, x0 + w + 14, y0 + h + 12, 2, RGB(27, 26, 31));
    }
}

/* --------------------------------------------------------- bottom screen */

/* Buttons: four columns, two rows. */
enum { B_FEED, B_PLAY, B_PET, B_SLEEP, B_DRAW, B_PHOTO, B_MAIL, B_DEV, BUTTONS };
static const char *labels[BUTTONS] = {"Feed", "Play", "Pet", "Sleep", "Draw", "Photo", "Mail", "Dev"};
static const int soon[BUTTONS] = {0, 0, 0, 0, 0, 1, 1, 0};
static const uint16_t accent[BUTTONS] = {RGB(31, 26, 14), RGB(18, 24, 31), RGB(31, 20, 21), RGB(20, 19, 30),
                                         RGB(30, 24, 16), RGB(24, 24, 24), RGB(24, 24, 24), RGB(21, 27, 22)};
#define BX(i) (8 + ((i) % 4) * 62)
#define BY(i) (36 + ((i) / 4) * 66)
#define BW 54
#define BH 58
/* Out to the terrace and back, beside the closet. */
#define OUT_X 140
#define OUT_Y 8
#define OUT_W 42
#define OUT_H 20
/* The closet, top right, clear of the buttons. */
#define CLOSET_X 186
#define CLOSET_Y 8
#define CLOSET_W 62
#define CLOSET_H 20

static void button(frame f, const uint8_t (*font)[8], int i, const char *label, int pressed, const ok_pet *p)
{
    static const int icon[BUTTONS] = {OK_ICON_FEED, OK_ICON_PLAY, OK_ICON_PET, OK_ICON_SLEEP,
                                      OK_ICON_DRAW, OK_ICON_PHOTO, OK_ICON_MAIL, OK_ICON_DEV};
    int x = BX(i), y = BY(i) + (pressed ? 2 : 0);
    uint16_t card = soon[i] ? RGB(19, 18, 17) : PAPER;
    if (!pressed) round_box(f, x, BY(i) + 3, BW, BH, 6, RGB(3, 3, 3)); /* shadow */
    round_box(f, x, y, BW, BH, 6, pressed ? half(card) + (uint16_t)RGB(6, 6, 6) - 0x8000u : card);
    round_box(f, x + 3, y + 3, BW - 6, 34, 5, soon[i] ? RGB(16, 15, 15) : accent[i]);
    ok_icons_draw(i == B_SLEEP && p->asleep ? OK_ICON_WAKE : icon[i], f, x + BW / 2, y + 20 - (pressed ? 0 : 1), 32);
    centred(f, font, x + BW / 2, y + 44, label, soon[i] ? RGB(11, 10, 10) : INK, 2);
    if (soon[i]) {
        round_box(f, x + BW - 30, y - 4, 32, 11, 4, RGB(29, 11, 8));
        centred(f, font, x + BW - 14, y - 2, "soon", PAPER, 1);
    }
}

/* A chip saying how it feels, with a coloured dot. */
static void mood_chip(frame f, const uint8_t (*font)[8], const ok_pet *p, int x, int y)
{
    static const char *word[] = {"Happy", "Hungry", "Sleepy", "Bored", "Missed you", "Asleep"};
    static const uint16_t dot[] = {RGB(10, 25, 12), RGB(30, 12, 6), RGB(16, 16, 30), RGB(24, 22, 18), RGB(30, 16, 22),
                                   RGB(12, 14, 26)};
    int m = ok_pet_mood(p), w;
    m = m < 0 || m > 5 ? 0 : m;
    w = width(word[m], 1) + 20;
    round_box(f, x, y, w, 16, 7, RGB(14, 13, 12));
    disc(f, x + 8, y + 8, 3, dot[m]);
    text(f, font, x + 14, y + 5, word[m], RGB(29, 28, 26), 1);
}

static void home_buttons(frame f, const uint8_t (*font)[8], const ok_ui *u, const ok_pet *p)
{
    int i;
    text(f, font, 11, 12, "OKUPA", RGB(4, 3, 2), 2); /* a shadow under the name */
    text(f, font, 10, 11, "OKUPA", RGB(31, 26, 3), 2);
    mood_chip(f, font, p, 62, 9);
    for (i = 0; i < BUTTONS; i++)
        button(f, font, i, i == B_SLEEP && p->asleep ? "Wake" : labels[i], u->pressed == i + 1 && u->pressed_ticks > 0, p);
    round_box(f, CLOSET_X, CLOSET_Y + (u->pressed == 100 && u->pressed_ticks ? 1 : 0), CLOSET_W, CLOSET_H, 7,
              u->pressed == 100 && u->pressed_ticks ? RGB(22, 20, 12) : RGB(31, 26, 3));
    ok_icons_draw(OK_ICON_WEAR, f, CLOSET_X + 13, CLOSET_Y + 10, 16);
    text(f, font, CLOSET_X + 25, CLOSET_Y + 7, "Wear", INK, 2);
    round_box(f, OUT_X, OUT_Y + (u->pressed == 101 && u->pressed_ticks ? 1 : 0), OUT_W, OUT_H, 7,
              u->pressed == 101 && u->pressed_ticks ? RGB(12, 18, 12) : RGB(17, 26, 15));
    ok_icons_draw(OK_ICON_HOME, f, OUT_X + 11, OUT_Y + 10, 16);
    text(f, font, OUT_X + 21, OUT_Y + 7, ok_house_where() == OK_PLACE_GARDEN ? "In" : "Out", INK, 2);
    /* Every few seconds, a pointer to the technical side for anyone watching. */
    centred(f, font, 128, 178,
            u->ticks / 300 % 3 == 2 ? "SELECT: how it all works"
            : u->net_status[0]      ? u->net_status
                                    : "Tap to look after your okupa",
            RGB(19, 17, 15), 1);
}

/* The closet: tops, colours, things to wear. */
#define TOP_Y 40
#define SWATCH_Y 74
#define WEAR_Y 102
#define WEAR_X(i) (8 + ((i) % 3) * 82)
#define WEAR_ROW(i) (WEAR_Y + ((i) / 3) * 22)
#define DONE_X 176
#define DONE_Y 168

static void closet(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    int i;
    text(f, font, 10, 10, "Closet", RGB(31, 26, 3), 2);
    for (i = 0; i < OK_TOPS; i++) {
        int on = u->look.top == i;
        round_box(f, 8 + i * 61, TOP_Y, 57, 24, 5, on ? RGB(31, 26, 3) : PAPER);
        centred(f, font, 8 + i * 61 + 28, TOP_Y + 8, ok_top_name(i), INK, 1);
    }
    for (i = 0; i < OK_COLOURS; i++) {
        int x = 8 + i * 31, on = u->look.colour == i;
        round_box(f, x, SWATCH_Y, 26, 22, 5, on ? PAPER : RGB(4, 4, 4));
        round_box(f, x + 3, SWATCH_Y + 3, 20, 16, 4, ok_top_colour(i));
    }
    for (i = 0; i < OK_WEARS; i++) {
        int on = u->look.wear == i;
        round_box(f, WEAR_X(i), WEAR_ROW(i), 76, 19, 5, on ? RGB(31, 26, 3) : PAPER);
        centred(f, font, WEAR_X(i) + 38, WEAR_ROW(i) + 6, ok_wear_name(i), INK, 1);
    }
    round_box(f, DONE_X, DONE_Y, 72, 20, 5, RGB(10, 25, 12));
    centred(f, font, DONE_X + 36, DONE_Y + 6, "Done", INK, 1);
}

/* Sketchbook: the 64x64 drawing at twice the size, tools on the right. */
#define CX 8
#define CY 40
enum { T_PEN, T_ERASER, T_CLEAR, T_SEND, T_DONE, TOOLS };
static const char *tools[TOOLS] = {"Pen", "Eraser", "Clear", "Send", "Done"};
#define TX 148
#define TY(i) (40 + (i) * 29)
#define TW 100
#define TH 25

static void sketch(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    int x, y, i;
    centred(f, font, 128, 12, "Sketchbook", PAPER, 2);
    rect(f, CX - 2, CY - 2, 132, 132, INK);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) rect(f, CX + 2 * x, CY + 2 * y, 2, 2, ok_drawing_get(u->drawing, x, y) ? INK : PAPER);
    for (i = 0; i < TOOLS; i++) {
        int on = (i == T_PEN && !u->eraser) || (i == T_ERASER && u->eraser);
        frame_box(f, TX, TY(i), TW, TH, on ? RGB(29, 22, 6) : PAPER, INK);
        centred(f, font, TX + TW / 2, TY(i) + 9, i == T_SEND && u->drawing_sent ? "On its way" : tools[i], INK, 1);
    }
}

static void dev(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    int i;
    centred(f, font, 128, 6, "Dev: the core on this console", PAPER, 1);
    for (i = 0; i < OK_UI_DEV_LINES; i++) mono(f, font, 6, 20 + i * 10, u->dev[i], PAPER, 1);
    frame_box(f, 4, 166, 60, 22, PAPER, INK);
    centred(f, font, 34, 173, "Net", INK, 1);
    frame_box(f, 66, 166, 58, 22, PAPER, INK);
    centred(f, font, 95, 173, "Wi-Fi", INK, 1);
    frame_box(f, 126, 166, 62, 22, PAPER, INK);
    centred(f, font, 157, 173, "Wallet", INK, 1);
    frame_box(f, 190, 166, 62, 22, PAPER, INK);
    centred(f, font, 221, 173, "Back", INK, 1);
}

/* The console's wallet: scribble to create the key, then its address. */
#define SCRIBBLE_X 8
#define SCRIBBLE_Y 30
#define SCRIBBLE_W 240
#define SCRIBBLE_H 100
static void wallet(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    char head[33];
    snprintf(head, sizeof head, "Wallet (%.10s)", u->wallet_network[0] ? u->wallet_network : "mainnet");
    centred(f, font, 128, 6, head, PAPER, 1);
    if (!u->wallet_ready) {
        frame_box(f, SCRIBBLE_X, SCRIBBLE_Y, SCRIBBLE_W, SCRIBBLE_H, PAPER, INK);
        centred(f, font, 128, SCRIBBLE_Y + 40, "Scribble here", MUTED, 1);
        centred(f, font, 128, SCRIBBLE_Y + 54, "to make the key", MUTED, 1);
        frame_box(f, 8, 138, 240, 12, RGB(24, 23, 21), INK);
        rect(f, 10, 140, 236 * (u->wallet_progress > 100 ? 100 : u->wallet_progress) / 100, 8, RGB(9, 22, 10));
        centred(f, font, 128, 154, "Your strokes are its randomness", PAPER, 1);
    } else {
        /* An EIP-681 link as a QR code, so a phone wallet can just scan it. */
        static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
        char a[23], b[21], link[64];
        int size, x, y, scale, left, topq;
        snprintf(a, sizeof a, "%.22s", u->wallet_address);
        snprintf(b, sizeof b, "%.20s", u->wallet_address + 22);
        centred(f, font, 128, 18, a, PAPER, 1);
        centred(f, font, 128, 28, b, PAPER, 1);
        snprintf(link, sizeof link, "ethereum:%.42s", u->wallet_address);
        if (qrcodegen_encodeText(link, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 6, qrcodegen_Mask_AUTO, true)) {
            size = qrcodegen_getSize(qr);
            scale = 112 / (size + 4);
            left = 128 - (size + 4) * scale / 2;
            topq = 38;
            rect(f, left, topq, (size + 4) * scale, (size + 4) * scale, RGB(31, 31, 31));
            for (y = 0; y < size; y++)
                for (x = 0; x < size; x++)
                    if (qrcodegen_getModule(qr, x, y)) rect(f, left + (x + 2) * scale, topq + (y + 2) * scale, scale, scale, RGB(0, 0, 0));
        }
        centred(f, font, 128, 152, u->wallet_status[0] ? u->wallet_status : "Scan to send ETH", PAPER, 1);
        frame_box(f, 4, 166, 120, 22, PAPER, INK);
        centred(f, font, 64, 173, "Send hello", INK, 1);
    }
    frame_box(f, 190, 166, 62, 22, PAPER, INK);
    centred(f, font, 221, 173, "Back", INK, 1);
}

/* Wi-Fi list: one row per network, strongest first. */
#define AP_Y(i) (24 + (i) * 25)
static void wifi(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    int i, b;
    centred(f, font, 128, 6, "Wi-Fi networks", PAPER, 1);
    for (i = 0; i < u->ap_count && i < OK_UI_APS; i++) {
        char name[23];
        snprintf(name, sizeof name, "%.22s", u->aps[i].ssid);
        frame_box(f, 6, AP_Y(i), 244, 22, PAPER, INK);
        text(f, font, 12, AP_Y(i) + 7, name, INK, 1);
        text(f, font, 196, AP_Y(i) + 7, u->aps[i].secure ? "*" : " ", INK, 1);
        for (b = 0; b < 3; b++) rect(f, 212 + b * 9, AP_Y(i) + 16 - b * 4, 6, 3 + b * 4, b < u->aps[i].strength ? INK : MUTED);
    }
    if (!u->ap_count) centred(f, font, 128, 60, "No networks yet", PAPER, 1);
    centred(f, font, 128, 152, u->wifi_status, PAPER, 1);
    frame_box(f, 6, 166, 100, 22, PAPER, INK);
    centred(f, font, 56, 173, "Search", INK, 1);
    frame_box(f, 176, 166, 74, 22, PAPER, INK);
    centred(f, font, 213, 173, "Back", INK, 1);
}

/* On-screen keyboard: digits on top, letters or symbols below. */
static const char *key_rows[3][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._@"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._@"},
    {"1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\|"},
};
#define KEY_X(c) (8 + (c) * 24)
#define KEY_Y(r) (36 + (r) * 26)

static void password(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    char head[33], shown[29];
    int r, c, n = u->password_len;
    snprintf(head, sizeof head, "Password: %.22s", u->ap_selected >= 0 ? u->aps[u->ap_selected].ssid : "");
    text(f, font, 6, 4, head, PAPER, 1);
    frame_box(f, 6, 14, 244, 18, PAPER, INK);
    snprintf(shown, sizeof shown, "%.28s", u->password + (n > 28 ? n - 28 : 0));
    text(f, font, 10, 19, shown, INK, 1);
    for (r = 0; r < 4; r++)
        for (c = 0; c < 10; c++) {
            char k[2] = {key_rows[u->keys][r][c], 0};
            frame_box(f, KEY_X(c), KEY_Y(r), 22, 24, PAPER, INK);
            text(f, font, KEY_X(c) + 7, KEY_Y(r) + 8, k, INK, 1);
        }
    frame_box(f, 8, 140, 58, 22, u->keys == 1 ? RGB(29, 22, 6) : PAPER, INK);
    centred(f, font, 37, 147, "Caps", INK, 1);
    frame_box(f, 68, 140, 58, 22, u->keys == 2 ? RGB(29, 22, 6) : PAPER, INK);
    centred(f, font, 97, 147, "#+=", INK, 1);
    frame_box(f, 128, 140, 58, 22, PAPER, INK);
    centred(f, font, 157, 147, "Space", INK, 1);
    frame_box(f, 188, 140, 60, 22, PAPER, INK);
    centred(f, font, 218, 147, "Del", INK, 1);
    frame_box(f, 8, 166, 110, 22, PAPER, INK);
    centred(f, font, 63, 173, "Cancel", INK, 1);
    frame_box(f, 138, 166, 110, 22, RGB(29, 22, 6), INK);
    centred(f, font, 193, 173, "Connect", INK, 1);
}

/* The mood the okupa shows: the pet's, or happy for a while after a treat. */
static int pose(const ok_ui *u, const ok_pet *p)
{
    switch (ok_pet_mood(p)) {
    case OK_MOOD_ASLEEP: return OK_POSE_ASLEEP;
    case OK_MOOD_HUNGRY: return OK_POSE_HUNGRY;
    case OK_MOOD_SLEEPY: return OK_POSE_SLEEPY;
    case OK_MOOD_BORED: return OK_POSE_BORED;
    case OK_MOOD_LONELY: return OK_POSE_LONELY;
    }
    if (u->mind_ticks && u->mind_mood) return u->mind_mood - 1; /* only while the pet itself is content */
    return u->happy_ticks ? OK_POSE_HAPPY : OK_POSE_IDLE;
}

void ok_ui_render(const ok_ui *u, const ok_pet *p, int hour, int minute, const uint8_t (*font)[8],
                  uint16_t top[OK_UI_W * OK_UI_H], uint16_t bottom[OK_UI_W * OK_UI_H])
{
    int x, y;
    const char *line = u->say ? u->say : ok_pet_line(p, hour);
    if (u->scene == OK_UI_SPLASH) {
        ok_intro_splash(top, bottom, u->intro_t);
        return;
    }
    if (u->scene == OK_UI_FILM) {
        ok_intro_film(top, bottom, u->intro_t);
        return;
    }
    icons_ready();
    {
        /* The bottom screen only when it may have changed, or ten times a
         * second: redrawing it every frame costs the DS a lot. */
        static const uint16_t *last_buf;
        static int last_scene = -1, last_pressed;
        static uint32_t last_tick;
        int draw = bottom != last_buf || u->scene != last_scene || u->pressed != last_pressed || u->pressed_ticks ||
                   u->scene == OK_UI_SKETCH || u->scene == OK_UI_WALLET || u->ticks - last_tick >= 6 || u->ticks < last_tick;
        if (draw) {
            last_buf = bottom;
            last_scene = u->scene;
            last_pressed = u->pressed;
            last_tick = u->ticks;
            rect(bottom, 0, 0, OK_UI_W, OK_UI_H, RGB(8, 7, 7));
            for (y = 4; y < OK_UI_H; y += 8)
                for (x = 4; x < OK_UI_W; x += 8) rect(bottom, x, y, 1, 1, RGB(11, 10, 10));
        }
        if (u->scene == OK_UI_SKETCH) {
            if (draw) sketch(bottom, font, u);
            line = "Draw me something for the house!";
        } else if (u->scene == OK_UI_DEV) {
            if (draw) dev(bottom, font, u);
        } else if (u->scene == OK_UI_WIFI) {
            if (draw) wifi(bottom, font, u);
            line = u->wifi_status[0] ? u->wifi_status : "Which Wi-Fi do we use?";
        } else if (u->scene == OK_UI_PASSWORD) {
            if (draw) password(bottom, font, u);
            line = "Psst. I won't peek.";
        } else if (u->scene == OK_UI_WALLET) {
            if (draw) wallet(bottom, font, u);
            line = u->wallet_ready ? "My very own address!" : "Scribble! Wilder! More!";
        } else if (u->scene == OK_UI_CLOSET) {
            if (draw) closet(bottom, font, u);
            if (!u->say) line = "How do I look?";
        } else if (draw) {
            home_buttons(bottom, font, u, p);
        }
    }
    top_screen(top, font, u, p, hour, minute, line);
}

void ok_ui_init(ok_ui *u)
{
    ok_monkey_room room;
    memset(u, 0, sizeof *u);
    u->last_x = u->last_y = -1;
    u->look.wear = OK_WEAR_NONE; /* the smiley mask lies on the floor until it is worn */
    u->look.top = OK_TOP_NONE;   /* and it wears nothing but its fur, for now */
    ok_house_room(&room);
    ok_monkey_init(&u->anim, &room, 0, 0x6f6b7570u);
}

void ok_ui_start_intro(ok_ui *u)
{
    u->scene = OK_UI_SPLASH;
    u->intro_t = 0;
}

static void say(ok_ui *u, const char *line);

void ok_ui_direct(ok_ui *u, const ok_pet *p, int move, int where, int mood, const char *line)
{
    if (u->scene == OK_UI_SPLASH || u->scene == OK_UI_FILM || p->asleep) return;
    if (move == OK_MOVE_TO_DOOR) {
        if (where == ok_house_where() || (where != OK_PLACE_ATTIC && where != OK_PLACE_GARDEN)) return;
        u->going = where + 1;
    }
    ok_monkey_direct(&u->anim, move);
    if (mood >= 0 && mood < OK_POSES && mood != OK_POSE_ASLEEP) {
        u->mind_mood = mood + 1;
        u->mind_ticks = 600;
    }
    if (line && line[0]) {
        snprintf(u->mind_line, sizeof u->mind_line, "%s", line);
        say(u, u->mind_line);
    }
}

/* Copies up to 60 printable ASCII characters, the rest as '?'. */
static void copy_ascii(char *out, const char *in)
{
    int i;
    for (i = 0; i < 60 && in[i]; i++) out[i] = in[i] >= 32 && in[i] < 127 ? in[i] : '?';
    out[i] = 0;
}

void ok_ui_thought(ok_ui *u, const char *text)
{
    if (!text || !text[0]) {
        u->thought_ticks = 0;
        return;
    }
    copy_ascii(u->thought, text);
    u->thought_ticks = 360;
}

void ok_ui_ask(ok_ui *u, const char *question)
{
    if (!question || !question[0]) {
        u->ask_ticks = 0;
        return;
    }
    copy_ascii(u->ask, question);
    u->ask_ticks = 1800;
}

void ok_ui_listening(ok_ui *u, int state)
{
    state = state < 0 || state > 2 ? 0 : state;
    if (state == 1 && u->listening != 1) ok_monkey_direct(&u->anim, OK_MOVE_LOOK); /* all ears */
    u->listening = state;
}

/* The floor of where it is, from its places, a little inside the walls. */
static void floor_box(ok_monkey_room *r, int32_t *x0, int32_t *x1, int32_t *z0, int32_t *z1)
{
    int i;
    if (ok_house_where() == OK_PLACE_GARDEN) ok_house_garden(r);
    else ok_house_room(r);
    *x0 = *x1 = r->home_x, *z0 = *z1 = r->home_z;
    for (i = 0; i < 4; i++) {
        int32_t px = i < 3 ? r->spot_x[i] : r->door_x, pz = i < 3 ? r->spot_z[i] : r->door_z;
        if (px < *x0) *x0 = px;
        if (px > *x1) *x1 = px;
        if (pz < *z0) *z0 = pz;
        if (pz > *z1) *z1 = pz;
    }
}

void ok_ui_body(ok_ui *u, int x, int z, int gaze, int energy)
{
    ok_monkey_room r;
    int32_t x0, x1, z0, z1, at[3] = {0, 0, 0}, tx = u->anim.x, tz = u->anim.z;
    int go = x >= 0 && z >= 0, flip;
    floor_box(&r, &x0, &x1, &z0, &z1);
    flip = r.face_yaw == 180; /* the terrace is seen from the other side */
    if (go) {
        int fx = x > 100 ? 100 : x, fz = z > 100 ? 100 : z;
        if (flip) fx = 100 - fx, fz = 100 - fz;
        tx = x0 + (x1 - x0) * fx / 100;
        tz = z0 + (z1 - z0) * fz / 100;
    }
    if (gaze == OK_GAZE_FLOOR) at[0] = tx, at[1] = 0, at[2] = tz + (flip ? -350 : 350);
    else if (gaze >= 0 && gaze < OK_GAZES) ok_house_gaze_point(gaze, at);
    ok_monkey_body(&u->anim, go, tx, tz, gaze, at, energy < 0 ? 50 : energy);
}

void ok_ui_drop_object(ok_ui *u, const uint8_t boxes[][7], int n, const char *name)
{
    ok_monkey_room r;
    int32_t x0, x1, z0, z1, x, z, at[3];
    if (n <= 0 || !boxes) {
        ok_house_object(NULL, 0, -1, 0, 0);
        u->thing_name[0] = 0;
        return;
    }
    floor_box(&r, &x0, &x1, &z0, &z1);
    x = u->anim.x + (u->anim.x > (x0 + x1) / 2 ? -260 : 260); /* beside it, towards the middle */
    z = u->anim.z + (r.face_yaw == 180 ? -120 : 120);
    x = x < x0 ? x0 : x > x1 ? x1 : x;
    ok_house_object(boxes, n, ok_house_where(), x, z);
    copy_ascii(u->thing_name, name ? name : "");
    if (u->thing_name[0] && strlen(u->thing_name) > 20) u->thing_name[20] = 0;
    snprintf(u->thing_line, sizeof u->thing_line, "I made %s!", u->thing_name[0] ? u->thing_name : "something");
    say(u, u->thing_line);
    at[0] = x, at[1] = 0, at[2] = z;
    ok_monkey_body(&u->anim, 0, 0, 0, OK_GAZE_FLOOR, at, u->anim.energy);
    ok_monkey_direct(&u->anim, OK_MOVE_HOP);
}

void ok_ui_okupa_drawing(ok_ui *u, const uint8_t pixels[512])
{
    if (!pixels) {
        u->own_set = 0;
        return;
    }
    memcpy(u->own, pixels, sizeof u->own);
    u->own_set = 1;
}

void ok_ui_dream(ok_ui *u, const uint16_t *rgb15, int w, int h)
{
    int y;
    if (!rgb15 || w <= 0 || h <= 0) {
        u->dream_ticks = 0;
        return;
    }
    if (w > 128) w = 128;
    if (h > 128) h = 128;
    for (y = 0; y < h; y++) memcpy(u->dream + y * w, rgb15 + y * w, (size_t)w * 2);
    u->dream_w = w;
    u->dream_h = h;
    u->dream_ticks = 1200;
}

/* Out to the terrace or back in: the okupa is placed in the new spot. */
static void go(ok_ui *u, const ok_pet *p, int where)
{
    ok_monkey_room room;
    ok_house_place(where);
    if (where == OK_PLACE_GARDEN) ok_house_garden(&room);
    else ok_house_room(&room);
    ok_monkey_init(&u->anim, &room, p->asleep, u->anim.seed);
    u->ready = 1;
}

int ok_ui_tick(ok_ui *u, const ok_pet *p)
{
    if (u->scene == OK_UI_SPLASH || u->scene == OK_UI_FILM) {
        if (u->scene == OK_UI_SPLASH && u->intro_t == 60) u->sound = 1 + OK_SFX_JINGLE;
        if (++u->intro_t >= (u->scene == OK_UI_SPLASH ? OK_SPLASH_TICKS : OK_FILM_TICKS)) {
            u->scene = u->scene == OK_UI_SPLASH ? OK_UI_FILM : OK_UI_HOME;
            u->intro_t = 0;
        }
        return u->intro_t % 2 == 0 ? OK_UI_CHANGED : OK_UI_NOTHING;
    }
    if (ok_house_where() == OK_PLACE_GARDEN && p->asleep) go(u, p, OK_PLACE_ATTIC); /* bed is indoors */
    if (u->mind_ticks) u->mind_ticks--;
    if (u->going && u->anim.at_door) { /* through the door: it comes in (or out) by the other side */
        int where = u->going - 1;
        u->going = 0;
        go(u, p, where);
        u->anim.x = u->anim.room.door_x;
        u->anim.z = u->anim.room.door_z;
    }
    if (!u->ready) { /* the first frame knows the loaded pet: asleep, its drawing */
        ok_monkey_room room;
        ok_house_room(&room);
        ok_monkey_init(&u->anim, &room, p->asleep, 0x6f6b7570u ^ (uint32_t)p->updated);
        u->ready = 1;
    }
    /* The wall: a picture the console just saw in a block gets hung, in
     * the attic, with the okupa there to see it. */
    if (!u->wall_known) {
        u->wall_known = 1;
        u->wall_seen = wall_sum(u);
    } else if (!u->hang_t && wall_sum(u) != u->wall_seen) {
        if (u->wall_count <= 0) {
            u->wall_seen = wall_sum(u);
        } else if (ok_house_where() == OK_PLACE_ATTIC) {
            u->wall_seen = wall_sum(u);
            u->hang_slot = u->wall_count - 1;
            u->hang_t = 1;
        } else if (!u->going) {
            ok_ui_direct(u, p, OK_MOVE_TO_DOOR, OK_PLACE_ATTIC, OK_POSE_HAPPY, NULL);
        }
    }
    if (u->hang_t) {
        if (u->hang_t == 22) { /* it lands on its nail */
            u->sound = 1 + OK_SFX_POP;
            u->happy_ticks = 360;
            if (!p->asleep) ok_monkey_event(&u->anim, OK_EVENT_HELLO);
        }
        if (u->hang_t == 60) u->sound = 1 + OK_SFX_YAY;
        if (++u->hang_t > OK_HOUSE_HANG_TICKS) u->hang_t = 0;
    }
    u->ticks++;
    if (u->say_ticks && --u->say_ticks == 0) u->say = NULL;
    if (u->thought_ticks) u->thought_ticks--;
    if (u->dream_ticks) u->dream_ticks--;
    if (u->ask_ticks) u->ask_ticks--;
    if (u->happy_ticks) u->happy_ticks--;
    if (u->pressed_ticks) u->pressed_ticks--;
    ok_monkey_step(&u->anim, pose(u, p), u->look.wear);
    /* Sounds that go with what it does: each bite, each catch, each hop. */
    if (u->anim.move != u->heard_move) u->heard_bites = 0;
    if (u->anim.bites != u->heard_bites) u->sound = 1 + (u->anim.move == OK_MOVE_EAT ? OK_SFX_EAT : OK_SFX_POP);
    if ((u->anim.move == OK_MOVE_HOP && u->anim.move_t == 12) || (u->anim.move == OK_MOVE_PEEK && u->anim.move_t == 150))
        u->sound = 1 + OK_SFX_BOING;
    if (u->anim.move == OK_MOVE_EAT && u->anim.move_t == 161) u->sound = 1 + OK_SFX_YAY;
    u->heard_bites = u->anim.bites;
    u->heard_move = u->anim.move;
    return u->ticks % 2 == 0 ? OK_UI_CHANGED : OK_UI_NOTHING;
}

static int inside(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

static void say(ok_ui *u, const char *line)
{
    u->say = line;
    u->say_ticks = 180;
}

static void plot(ok_ui *u, int x, int y)
{
    int i, j;
    for (j = 0; j < 2; j++)
        for (i = 0; i < 2; i++) ok_drawing_set(u->drawing, (unsigned)(x + i), (unsigned)(y + j), !u->eraser);
}

/* A stroke from the last point to this one, so fast strokes leave no gaps. */
static void stroke(ok_ui *u, int x, int y)
{
    int dx = x - u->last_x, dy = y - u->last_y, steps, s;
    if (u->last_x < 0) {
        plot(u, x, y);
    } else {
        steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        for (s = 0; s <= steps; s++) plot(u, u->last_x + (steps ? dx * s / steps : 0), u->last_y + (steps ? dy * s / steps : 0));
    }
    u->last_x = x;
    u->last_y = y;
}

void ok_ui_release(ok_ui *u)
{
    u->last_x = u->last_y = -1;
}

int ok_ui_touch(ok_ui *u, ok_pet *p, int x, int y, int held, int64_t now)
{
    int i;
    if (u->scene == OK_UI_SPLASH || u->scene == OK_UI_FILM) { /* a touch skips ahead */
        if (held) return OK_UI_NOTHING;
        u->scene = u->scene == OK_UI_SPLASH ? OK_UI_FILM : OK_UI_HOME;
        u->intro_t = 0;
        return OK_UI_CHANGED;
    }
    if (u->dream_ticks && u->scene == OK_UI_HOME && !held) { /* a tap puts the dream away */
        u->dream_ticks = 0;
        return OK_UI_CHANGED;
    }
    if (u->scene == OK_UI_SKETCH) {
        if (inside(x, y, CX, CY, 128, 128)) {
            if (u->drawing_sent) return OK_UI_NOTHING; /* the signed pixels must stay as sent */
            if (!held) u->last_x = u->last_y = -1;
            stroke(u, (x - CX) / 2, (y - CY) / 2);
            return OK_UI_CHANGED;
        }
        if (held) return OK_UI_NOTHING;
        for (i = 0; i < TOOLS; i++) {
            if (!inside(x, y, TX, TY(i), TW, TH)) continue;
            if (i == T_PEN) u->eraser = 0;
            if (i == T_ERASER) u->eraser = 1;
            if (i == T_CLEAR && !u->drawing_sent) memset(u->drawing, 0, sizeof u->drawing);
            if (i == T_SEND) {
                if (u->drawing_sent) return OK_UI_NOTHING;
                say(u, "Off to Ethereum. It goes on the wall once a block has it.");
                u->scene = OK_UI_HOME;
                return OK_UI_SEND_DRAWING;
            }
            if (i == T_DONE) {
                int k;
                u->scene = OK_UI_HOME;
                for (k = 0; k < (int)sizeof u->drawing && !u->drawing[k]; k++) {}
                if (u->drawing_sent) say(u, "It's on its way. Watch the wall!");
                else if (k < (int)sizeof u->drawing) say(u, "I love it! Send it and it goes on the wall.");
                else say(u, "Nothing yet? Maybe next time.");
                if (k < (int)sizeof u->drawing) ok_monkey_event(&u->anim, OK_EVENT_HELLO);
            }
            return OK_UI_CHANGED;
        }
        return OK_UI_NOTHING;
    }
    if (u->scene == OK_UI_WALLET) {
        if (!u->wallet_ready && inside(x, y, SCRIBBLE_X, SCRIBBLE_Y, SCRIBBLE_W, SCRIBBLE_H)) return OK_UI_WALLET_SCRIBBLE;
        if (u->wallet_ready && !held && inside(x, y, 4, 166, 120, 22)) return OK_UI_WALLET_HELLO;
        if (held || !inside(x, y, 190, 166, 62, 22)) return OK_UI_NOTHING;
        u->scene = OK_UI_DEV;
        return OK_UI_CHANGED;
    }
    if (held) return OK_UI_NOTHING;
    if (u->scene == OK_UI_DEV) {
        if (inside(x, y, 4, 166, 60, 22)) return OK_UI_RUN_NETWORK;
        if (inside(x, y, 66, 166, 58, 22)) {
            u->scene = OK_UI_WIFI;
            return OK_UI_WIFI_SCAN;
        }
        if (inside(x, y, 126, 166, 62, 22)) {
            u->scene = OK_UI_WALLET;
            return OK_UI_WALLET_OPEN;
        }
        if (!inside(x, y, 190, 166, 62, 22)) return OK_UI_NOTHING;
        u->scene = OK_UI_HOME;
        return OK_UI_CHANGED;
    }
    if (u->scene == OK_UI_WIFI) {
        if (inside(x, y, 6, 166, 100, 22)) return OK_UI_WIFI_SCAN;
        if (inside(x, y, 176, 166, 74, 22)) {
            u->scene = OK_UI_DEV;
            return OK_UI_CHANGED;
        }
        for (i = 0; i < u->ap_count && i < OK_UI_APS; i++) {
            if (!inside(x, y, 6, AP_Y(i), 244, 22)) continue;
            u->ap_selected = i;
            memset(u->password, 0, sizeof u->password);
            u->password_len = 0;
            u->keys = 0;
            if (!u->aps[i].secure) return OK_UI_WIFI_CONNECT;
            u->scene = OK_UI_PASSWORD;
            return OK_UI_CHANGED;
        }
        return OK_UI_NOTHING;
    }
    if (u->scene == OK_UI_PASSWORD) {
        int r, c;
        for (r = 0; r < 4; r++)
            for (c = 0; c < 10; c++)
                if (inside(x, y, KEY_X(c), KEY_Y(r), 22, 24)) {
                    if (u->password_len < (int)sizeof u->password - 1) u->password[u->password_len++] = key_rows[u->keys][r][c];
                    return OK_UI_CHANGED;
                }
        if (inside(x, y, 8, 140, 58, 22)) u->keys = u->keys == 1 ? 0 : 1;
        else if (inside(x, y, 68, 140, 58, 22)) u->keys = u->keys == 2 ? 0 : 2;
        else if (inside(x, y, 128, 140, 58, 22)) {
            if (u->password_len < (int)sizeof u->password - 1) u->password[u->password_len++] = ' ';
        } else if (inside(x, y, 188, 140, 60, 22)) {
            if (u->password_len) u->password[--u->password_len] = 0;
        } else if (inside(x, y, 8, 166, 110, 22)) {
            memset(u->password, 0, sizeof u->password);
            u->password_len = 0;
            u->scene = OK_UI_WIFI;
        } else if (inside(x, y, 138, 166, 110, 22)) {
            u->scene = OK_UI_WIFI;
            return OK_UI_WIFI_CONNECT;
        } else {
            return OK_UI_NOTHING;
        }
        return OK_UI_CHANGED;
    }
    if (u->scene == OK_UI_CLOSET) {
        for (i = 0; i < OK_TOPS; i++)
            if (inside(x, y, 8 + i * 61, TOP_Y, 57, 24)) u->look.top = (uint8_t)i;
        for (i = 0; i < OK_COLOURS; i++)
            if (inside(x, y, 8 + i * 31, SWATCH_Y, 26, 22)) u->look.colour = (uint8_t)i;
        for (i = 0; i < OK_WEARS; i++)
            if (inside(x, y, WEAR_X(i), WEAR_ROW(i), 76, 19)) u->look.wear = (uint8_t)i;
        u->sound = 1 + OK_SFX_POP;
        if (inside(x, y, DONE_X, DONE_Y, 72, 20)) {
            u->sound = 1 + OK_SFX_HELLO;
            u->scene = OK_UI_HOME;
            say(u, "Thanks! I feel brand new.");
            ok_monkey_event(&u->anim, OK_EVENT_HELLO);
        }
        return OK_UI_CHANGED;
    }
    if (inside(x, y, OUT_X, OUT_Y, OUT_W, OUT_H)) {
        u->pressed = 101;
        u->pressed_ticks = 8;
        u->sound = 1 + OK_SFX_POP;
        if (p->asleep) {
            say(u, "Shh... it's asleep.");
            return OK_UI_CHANGED;
        }
        go(u, p, ok_house_where() == OK_PLACE_GARDEN ? OK_PLACE_ATTIC : OK_PLACE_GARDEN);
        say(u, ok_house_where() == OK_PLACE_GARDEN ? "Fresh air! Look at the gallery." : "Home, sweet attic.");
        return OK_UI_CHANGED;
    }
    if (inside(x, y, CLOSET_X, CLOSET_Y, CLOSET_W, CLOSET_H)) {
        u->scene = OK_UI_CLOSET;
        u->pressed = 100;
        u->pressed_ticks = 8;
        return OK_UI_CHANGED;
    }
    for (i = 0; i < BUTTONS; i++) {
        int action, ok;
        if (!inside(x, y, BX(i), BY(i), BW, BH)) continue;
        u->pressed = i + 1;
        u->pressed_ticks = 8;
        u->sound = 1 + OK_SFX_TAP;
        switch (i) {
        case B_DRAW: u->scene = OK_UI_SKETCH; u->eraser = 0; return OK_UI_CHANGED;
        case B_DEV: u->scene = OK_UI_DEV; return OK_UI_RUN_CHECKS;
        case B_PHOTO: say(u, "A camera? Soon, I promise."); return OK_UI_CHANGED;
        case B_MAIL: say(u, "No letters yet. Friends are coming."); return OK_UI_CHANGED;
        }
        action = i == B_FEED ? OK_DO_FEED : i == B_PLAY ? OK_DO_PLAY : i == B_PET ? OK_DO_PET
                 : p->asleep ? OK_DO_WAKE : OK_DO_SLEEP;
        ok = ok_pet_do(p, action, now);
        say(u, p->asleep && action != OK_DO_SLEEP ? "Zzz... (it's sleeping)" : ok_pet_reply(action, ok));
        if (!p->asleep && action != OK_DO_SLEEP && action != OK_DO_WAKE) {
            ok_monkey_event(&u->anim, !ok ? OK_EVENT_REFUSE : action == OK_DO_FEED ? OK_EVENT_FEED
                                      : action == OK_DO_PLAY ? OK_EVENT_PLAY : OK_EVENT_PET);
            if (ok) u->happy_ticks = 600;
            u->sound = 1 + (!ok ? OK_SFX_NOPE : action == OK_DO_PET ? OK_SFX_YAY : OK_SFX_TAP);
        } else if (!ok && action == OK_DO_SLEEP) {
            ok_monkey_event(&u->anim, OK_EVENT_REFUSE);
            u->sound = 1 + OK_SFX_NOPE;
        } else if (ok && action == OK_DO_WAKE) {
            u->sound = 1 + OK_SFX_HELLO;
        }
        return OK_UI_CHANGED;
    }
    return OK_UI_NOTHING;
}
