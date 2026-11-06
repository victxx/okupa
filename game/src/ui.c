#include "okupa_ui.h"
#include "okupa_act.h"
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

static void text(frame f, const uint8_t (*font)[8], int x, int y, const char *s, uint16_t c, int scale)
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

static void centred(frame f, const uint8_t (*font)[8], int cx, int y, const char *s, uint16_t c, int scale)
{
    text(f, font, cx - (int)strlen(s) * 4 * scale, y, s, c, scale);
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

static const char *const i_food[] = {"......kk", ".....yk.", "....yy..", "...yyY..", "..yyYY..", "yyyYY...", ".YYY....", "........"};
static const char *const i_sleep[] = {"..MMM...", ".MM.....", "MM......", "MM......", "MM......", "MMM...M.", ".MMMMMM.", "..MMMM.."};
static const char *const i_fun[] = {"..rrww..", ".rrrwbb.", "rrrwwbbb", "rrwwwwbb", "yywwwwbb", "yyywwbbb", ".yyywbb.", "..yyyb.."};
static const char *const i_love[] = {".rr..rr.", "rwrrrrrr", "rrrrrrrr", "rrrrrrrR", ".rrrrrR.", "..rrrR..", "...rR...", "........"};

/* The four needs along the bottom: an icon and a bar each, on glass. */
static void needs(frame f, const ok_pet *p)
{
    static const char *const *icons[OK_NEEDS] = {i_food, i_sleep, i_fun, i_love};
    int i;
    for (i = 0; i < OK_NEEDS; i++) {
        int x = 6 + i * 62, y = 176, v = p->need[i] * 36 / OK_NEED_MAX;
        uint16_t c = p->need[i] < 35 ? RGB(30, 9, 7) : p->need[i] < 65 ? RGB(30, 23, 6) : RGB(10, 25, 12);
        glass(f, x, y, 58, 13);
        pixels(f, x + 4, y + 3, icons[i], 8, 1);
        rect(f, x + 16, y + 5, 38, 4, RGB(3, 3, 4));
        rect(f, x + 17, y + 6, v, 2, c);
        if (p->need[i] < 35 && i == 0) rect(f, x + 17, y + 6, v, 1, RGB(31, 18, 14));
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

/* What it says, in a bubble above its head with a tail pointing at it. */
static void bubble(frame f, const uint8_t (*font)[8], const char *s, int hx, int hy)
{
    char line[2][29] = {{0}};
    size_t n = strlen(s), cut = n, longest;
    int w, h, x, y, tx, lines;
    if (n > 28) {
        for (cut = 28; cut > 0 && s[cut] != ' '; cut--) {}
        if (cut == 0) cut = 28;
    }
    memcpy(line[0], s, cut > 28 ? 28 : cut);
    if (cut < n) strncpy(line[1], s + cut + (s[cut] == ' '), 28);
    lines = line[1][0] ? 2 : 1;
    longest = strlen(line[0]) > strlen(line[1]) ? strlen(line[0]) : strlen(line[1]);
    w = (int)longest * 8 + 14;
    h = lines * 10 + 9;
    if (hx < -50) hx = 128, hy = 110;
    x = hx - w / 2;
    x = x < 4 ? 4 : x + w > OK_UI_W - 4 ? OK_UI_W - 4 - w : x;
    y = hy - h - 12;
    if (y < 20) y = 20;
    tx = hx < x + 10 ? x + 10 : hx > x + w - 10 ? x + w - 10 : hx;
    round_box(f, x - 1, y - 1, w + 2, h + 2, 5, INK);
    round_box(f, x, y, w, h, 4, PAPER);
    {
        int k;
        for (k = 0; k < 6; k++) {
            rect(f, tx - 4 + k / 2 + (hx > tx ? k / 2 : 0), y + h + k, 8 - k, 1, PAPER);
            rect(f, tx - 5 + k / 2 + (hx > tx ? k / 2 : 0), y + h + k, 1, 1, INK);
            rect(f, tx + 4 - k + k / 2 + (hx > tx ? k / 2 : 0), y + h + k, 1, 1, INK);
        }
    }
    centred(f, font, x + w / 2, y + 5, line[0], INK, 1);
    if (lines == 2) centred(f, font, x + w / 2, y + 15, line[1], INK, 1);
}

static uint16_t skin[OK_SKIN * OK_SKIN];
static ok_look skin_look = {255, 255, 255};

static void top_screen(frame f, const uint8_t (*font)[8], const ok_ui *u, const ok_pet *p, int hour, int minute,
                       const char *line)
{
    ok_monkey_face face;
    if (memcmp(&skin_look, &u->look, sizeof skin_look)) {
        ok_monkey_skin(&u->look, skin);
        skin_look = u->look;
    }
    ok_house_draw(f, hour, minute, p->asleep, u->wall_set ? u->wall : NULL, &u->look, skin, &u->anim, &face);
    clock_pill(f, font, hour, minute);
    needs(f, p);
    if (line && !(p->asleep && line == ok_pet_line(p, hour))) bubble(f, font, line, face.top_x, face.top_y);
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
/* The closet, top right, clear of the buttons. */
#define CLOSET_X 186
#define CLOSET_Y 8
#define CLOSET_W 62
#define CLOSET_H 20

static const char *const b_feed[] = {"..........kn", ".........kn.", "........yyk.", ".......yyy..",
                                     "......yyyY..", ".....yyyYY..", "...yyyyYY...", ".yyyyyYY....",
                                     "yyyyYYY.....", ".YYYYY......", "..YYY.......", "............"};
static const char *const b_play[] = {"....rrww....", "..rrrrwwbb..", ".rrrrwwwbbb.", ".rrrwwwwbbb.",
                                     "rrrwwwwwwbbb", "rrwwwwwwwbbb", "yyywwwwwwbbb", "yyyywwwwbbbb",
                                     ".yyyywwwbbb.", ".yyyyywwbbb.", "..yyyyybb...", "....yyyy...."};
static const char *const b_pet[] = {"............", "..rrr..rrr..", ".rwwrrrrrrr.", ".rwrrrrrrrr.",
                                    ".rrrrrrrrrr.", ".rrrrrrrrrR.", "..rrrrrrrR..", "...rrrrrR...",
                                    "....rrrR....", ".....rR.....", "............", "............"};
static const char *const b_sleep[] = {"....MMMM....", "..MMM.......", ".MMM........", ".MM.......k.",
                                      "MMM......kkk", "MMM.......k.", "MMM.........", "MMMM......M.",
                                      ".MMMM....MM.", ".MMMMMMMMM..", "...MMMMMM...", "............"};
static const char *const b_wake[] = {".....y......", ".y...y...y..", "..y.....y...", "....yyy.....",
                                     "...yyyyy....", "yy.yyyyy.yy.", "...yyyyy....", "....yyy.....",
                                     "..y.....y...", ".y...y...y..", ".....y......", "............"};
static const char *const b_draw[] = {"..........pp", ".........ppp", "........yyp.", ".......yyy..",
                                     "......yyy...", ".....yyy....", "....yyy.....", "...yyy......",
                                     "..ttt.......", "..tt........", ".kt.........", "k..........."};
static const char *const b_photo[] = {"............", "..kkkk......", "kkkkkkkkkkkk", "kGGGGkkkGGGk",
                                      "kGGGkwwkkGGk", "kGGkwbbwkGGk", "kGGkwbbwkGGk", "kGGGkwwkGGGk",
                                      "kGGGGkkGGGGk", "kkkkkkkkkkkk", "............", "............"};
static const char *const b_mail[] = {"............", "kkkkkkkkkkkk", "kwkwwwwwwkwk", "kwwkwwwwkwwk",
                                     "kwwwkwwkwwwk", "kwwwwkkwwwwk", "kwwwwwwwwwwk", "kwwwwwwwwwwk",
                                     "kkkkkkkkkkkk", "............", "............", "............"};
static const char *const b_dev[] = {".....kk.....", "..k.kggk.k..", ".kgkggggkgk.", "..kggkkggk..",
                                    ".kggk..kggk.", "kggk....kggk", "kggk....kggk", ".kggk..kggk.",
                                    "..kggkkggk..", ".kgkggggkgk.", "..k.kggk.k..", ".....kk....."};
static const char *const b_closet[] = {"...kk...", "..k..k..", ".....k..", "....k...", "...kk...",
                                       ".kkkkkk.", "k......k", "kkkkkkkk"};

static void button(frame f, const uint8_t (*font)[8], int i, const char *label, int pressed, const ok_pet *p)
{
    static const char *const *art[BUTTONS] = {b_feed, b_play, b_pet, b_sleep, b_draw, b_photo, b_mail, b_dev};
    int x = BX(i), y = BY(i) + (pressed ? 2 : 0);
    uint16_t card = soon[i] ? RGB(20, 19, 18) : PAPER;
    if (!pressed) round_box(f, x, BY(i) + 3, BW, BH, 5, RGB(4, 4, 4)); /* shadow */
    round_box(f, x, y, BW, BH, 5, pressed ? half(card) + (uint16_t)RGB(6, 6, 6) - 0x8000u : card);
    disc(f, x + BW / 2, y + 20, 15, soon[i] ? RGB(17, 16, 16) : accent[i]);
    pixels(f, x + BW / 2 - 12, y + 8, i == B_SLEEP && p->asleep ? b_wake : art[i], 12, 2);
    centred(f, font, x + BW / 2, y + 44, label, soon[i] ? RGB(12, 11, 11) : INK, 1);
    if (soon[i]) {
        round_box(f, x + BW - 30, y - 4, 32, 10, 3, RGB(29, 11, 8));
        centred(f, font, x + BW - 14, y - 3, "soon", PAPER, 1);
    }
}

static void home_buttons(frame f, const uint8_t (*font)[8], const ok_ui *u, const ok_pet *p)
{
    int i;
    text(f, font, 10, 10, "OKUPA", RGB(31, 26, 3), 2);
    for (i = 0; i < BUTTONS; i++)
        button(f, font, i, i == B_SLEEP && p->asleep ? "Wake" : labels[i], u->pressed == i + 1 && u->pressed_ticks > 0, p);
    round_box(f, CLOSET_X, CLOSET_Y, CLOSET_W, CLOSET_H, 5, u->pressed == 100 && u->pressed_ticks ? RGB(22, 20, 12) : RGB(31, 26, 3));
    pixels(f, CLOSET_X + 6, CLOSET_Y + 6, b_closet, 8, 1);
    text(f, font, CLOSET_X + 17, CLOSET_Y + 6, "Wear", INK, 1);
    centred(f, font, 128, 178, u->net_status[0] ? u->net_status : "Tap to look after your okupa", RGB(20, 18, 15), 1);
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
        round_box(f, 8 + i * 82, TOP_Y, 76, 24, 5, on ? RGB(31, 26, 3) : PAPER);
        centred(f, font, 8 + i * 82 + 38, TOP_Y + 8, ok_top_name(i), INK, 1);
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
enum { T_PEN, T_ERASER, T_CLEAR, T_DONE, TOOLS };
static const char *tools[TOOLS] = {"Pen", "Eraser", "Clear", "Done"};
#define TX 148
#define TY(i) (40 + (i) * 34)
#define TW 100
#define TH 28

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
        centred(f, font, TX + TW / 2, TY(i) + 10, tools[i], INK, 1);
    }
}

static void dev(frame f, const uint8_t (*font)[8], const ok_ui *u)
{
    int i;
    centred(f, font, 128, 6, "Dev: the core on this console", PAPER, 1);
    for (i = 0; i < OK_UI_DEV_LINES; i++) text(f, font, 6, 20 + i * 10, u->dev[i], PAPER, 1);
    frame_box(f, 6, 166, 80, 22, PAPER, INK);
    centred(f, font, 46, 173, "Network", INK, 1);
    frame_box(f, 92, 166, 78, 22, PAPER, INK);
    centred(f, font, 131, 173, "Wi-Fi", INK, 1);
    frame_box(f, 176, 166, 74, 22, PAPER, INK);
    centred(f, font, 213, 173, "Back", INK, 1);
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
    snprintf(shown, sizeof shown, "%s", u->password + (n > 28 ? n - 28 : 0));
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
    return u->happy_ticks ? OK_POSE_HAPPY : OK_POSE_IDLE;
}

void ok_ui_render(const ok_ui *u, const ok_pet *p, int hour, int minute, const uint8_t (*font)[8],
                  uint16_t top[OK_UI_W * OK_UI_H], uint16_t bottom[OK_UI_W * OK_UI_H])
{
    int x, y;
    const char *line = u->say ? u->say : ok_pet_line(p, hour);
    rect(bottom, 0, 0, OK_UI_W, OK_UI_H, RGB(8, 7, 7));
    for (y = 4; y < OK_UI_H; y += 8)
        for (x = 4; x < OK_UI_W; x += 8) rect(bottom, x, y, 1, 1, RGB(11, 10, 10));
    if (u->scene == OK_UI_SKETCH) {
        sketch(bottom, font, u);
        line = "Draw me something for the house!";
    } else if (u->scene == OK_UI_DEV) {
        dev(bottom, font, u);
    } else if (u->scene == OK_UI_WIFI) {
        wifi(bottom, font, u);
        line = u->wifi_status[0] ? u->wifi_status : "Which Wi-Fi do we use?";
    } else if (u->scene == OK_UI_PASSWORD) {
        password(bottom, font, u);
        line = "Psst. I won't peek.";
    } else if (u->scene == OK_UI_CLOSET) {
        closet(bottom, font, u);
        if (!u->say) line = "How do I look?";
    } else {
        home_buttons(bottom, font, u, p);
    }
    top_screen(top, font, u, p, hour, minute, line);
}

void ok_ui_init(ok_ui *u)
{
    ok_monkey_room room;
    memset(u, 0, sizeof *u);
    u->last_x = u->last_y = -1;
    u->look.wear = OK_WEAR_SMILEY;
    ok_house_room(&room);
    ok_monkey_init(&u->anim, &room, 0, 0x6f6b7570u);
}

int ok_ui_tick(ok_ui *u, const ok_pet *p)
{
    if (!u->ready) { /* the first frame knows the loaded pet: asleep, its drawing */
        ok_monkey_room room;
        ok_house_room(&room);
        ok_monkey_init(&u->anim, &room, p->asleep, 0x6f6b7570u ^ (uint32_t)p->updated);
        memcpy(u->wall, u->drawing, sizeof u->wall);
        for (u->wall_set = 0; u->wall_set < (int)sizeof u->wall && !u->wall[u->wall_set]; u->wall_set++) {}
        u->wall_set = u->wall_set < (int)sizeof u->wall;
        u->ready = 1;
    }
    u->ticks++;
    if (u->say_ticks && --u->say_ticks == 0) u->say = NULL;
    if (u->happy_ticks) u->happy_ticks--;
    if (u->pressed_ticks) u->pressed_ticks--;
    ok_monkey_step(&u->anim, pose(u, p), u->look.wear);
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
    if (u->scene == OK_UI_SKETCH) {
        if (inside(x, y, CX, CY, 128, 128)) {
            if (!held) u->last_x = u->last_y = -1;
            stroke(u, (x - CX) / 2, (y - CY) / 2);
            return OK_UI_CHANGED;
        }
        if (held) return OK_UI_NOTHING;
        for (i = 0; i < TOOLS; i++) {
            if (!inside(x, y, TX, TY(i), TW, TH)) continue;
            if (i == T_PEN) u->eraser = 0;
            if (i == T_ERASER) u->eraser = 1;
            if (i == T_CLEAR) memset(u->drawing, 0, sizeof u->drawing);
            if (i == T_DONE) {
                int k;
                u->scene = OK_UI_HOME;
                memcpy(u->wall, u->drawing, sizeof u->wall);
                for (k = 0; k < (int)sizeof u->wall && !u->wall[k]; k++) {}
                u->wall_set = k < (int)sizeof u->wall;
                say(u, u->wall_set ? "There, on the wall. I love it!" : "Nothing yet? Maybe next time.");
                if (u->wall_set) ok_monkey_event(&u->anim, OK_EVENT_HELLO);
            }
            return OK_UI_CHANGED;
        }
        return OK_UI_NOTHING;
    }
    if (held) return OK_UI_NOTHING;
    if (u->scene == OK_UI_DEV) {
        if (inside(x, y, 6, 166, 80, 22)) return OK_UI_RUN_NETWORK;
        if (inside(x, y, 92, 166, 78, 22)) {
            u->scene = OK_UI_WIFI;
            return OK_UI_WIFI_SCAN;
        }
        if (!inside(x, y, 176, 166, 74, 22)) return OK_UI_NOTHING;
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
            if (inside(x, y, 8 + i * 82, TOP_Y, 76, 24)) u->look.top = (uint8_t)i;
        for (i = 0; i < OK_COLOURS; i++)
            if (inside(x, y, 8 + i * 31, SWATCH_Y, 26, 22)) u->look.colour = (uint8_t)i;
        for (i = 0; i < OK_WEARS; i++)
            if (inside(x, y, WEAR_X(i), WEAR_ROW(i), 76, 19)) u->look.wear = (uint8_t)i;
        if (inside(x, y, DONE_X, DONE_Y, 72, 20)) {
            u->scene = OK_UI_HOME;
            say(u, "Thanks! I feel brand new.");
            ok_monkey_event(&u->anim, OK_EVENT_HELLO);
        }
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
        } else if (!ok && action == OK_DO_SLEEP) {
            ok_monkey_event(&u->anim, OK_EVENT_REFUSE);
        }
        return OK_UI_CHANGED;
    }
    return OK_UI_NOTHING;
}
