#include "okupa_ui.h"
#include "okupa_act.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <string.h>

/* Placeholder art: a cosy room and a little person, all from rectangles and
 * circles until the real okupa and house are drawn. */
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define INK RGB(6, 5, 5)
#define PAPER RGB(31, 30, 27)
#define WALL RGB(28, 25, 20)
#define WALL_LINE RGB(26, 23, 18)
#define FLOOR RGB(21, 14, 9)
#define FLOOR_LINE RGB(18, 11, 7)
#define WOOD RGB(15, 9, 5)
#define DESK RGB(27, 25, 22)
#define MUTED RGB(17, 16, 15)
#define SKIN RGB(31, 24, 18)
#define HAIR RGB(10, 6, 3)
#define SHIRT RGB(29, 12, 9)
#define PANTS RGB(7, 10, 20)
#define BLANKET RGB(10, 16, 27)
#define GREEN RGB(10, 22, 10)

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

/* Up to two lines of at most 28 characters, broken at spaces. */
static void bubble(frame f, const uint8_t (*font)[8], const char *s)
{
    char line[2][29] = {{0}};
    size_t n = strlen(s), cut = n;
    frame_box(f, 8, 148, 240, 38, PAPER, INK);
    rect(f, 120, 144, 8, 5, PAPER);
    if (n > 28) {
        for (cut = 28; cut > 0 && s[cut] != ' '; cut--) {}
        if (cut == 0) cut = 28;
    }
    memcpy(line[0], s, cut > 28 ? 28 : cut);
    if (cut < n) strncpy(line[1], s + cut + (s[cut] == ' '), 28);
    centred(f, font, 128, line[1][0] ? 155 : 163, line[0], INK, 1);
    if (line[1][0]) centred(f, font, 128, 169, line[1], INK, 1);
}

static void needs_panel(frame f, const uint8_t (*font)[8], const ok_pet *p)
{
    static const char *names[OK_NEEDS] = {"Food", "Sleep", "Fun", "Love"};
    int i;
    frame_box(f, 6, 6, 104, 48, PAPER, MUTED);
    for (i = 0; i < OK_NEEDS; i++) {
        int y = 11 + i * 10, v = p->need[i] * 48 / OK_NEED_MAX;
        uint16_t c = p->need[i] < 35 ? RGB(28, 8, 6) : p->need[i] < 65 ? RGB(28, 22, 6) : RGB(9, 22, 10);
        text(f, font, 11, y, names[i], INK, 1);
        rect(f, 56, y + 1, 48, 6, RGB(24, 23, 21));
        rect(f, 56, y + 1, v, 6, c);
    }
}

static void window_view(frame f, int hour)
{
    uint16_t sky = hour >= 7 && hour < 18 ? RGB(15, 23, 31) : hour >= 18 && hour < 21 ? RGB(30, 17, 10) : RGB(3, 4, 11);
    frame_box(f, 156, 22, 72, 60, PAPER, WOOD);
    rect(f, 160, 26, 64, 52, sky);
    if (hour >= 21 || hour < 7) {
        rect(f, 170, 34, 1, 1, PAPER);
        rect(f, 200, 30, 1, 1, PAPER);
        rect(f, 214, 50, 1, 1, PAPER);
        disc(f, 186, 46, 5, RGB(30, 29, 22));
    } else {
        disc(f, 208, 40, 6, RGB(31, 29, 12));
    }
    rect(f, 191, 26, 2, 52, PAPER);
    rect(f, 160, 51, 64, 2, PAPER);
}

static void face(frame f, int cx, int cy, int mood, uint32_t ticks)
{
    int blink = ticks % 180 < 6;
    disc(f, cx, cy, 11, SKIN);
    rect(f, cx - 11, cy - 11, 23, 7, HAIR);
    rect(f, cx - 12, cy - 6, 4, 6, HAIR);
    rect(f, cx + 9, cy - 6, 4, 6, HAIR);
    if (mood == OK_MOOD_ASLEEP || mood == OK_MOOD_SLEEPY || blink) {
        rect(f, cx - 6, cy + 1, 4, 1, INK);
        rect(f, cx + 3, cy + 1, 4, 1, INK);
    } else {
        rect(f, cx - 5, cy - 1, 2, 3, INK);
        rect(f, cx + 4, cy - 1, 2, 3, INK);
    }
    if (mood == OK_MOOD_HAPPY) {
        rect(f, cx - 3, cy + 6, 7, 1, INK);
        rect(f, cx - 4, cy + 5, 1, 1, INK);
        rect(f, cx + 4, cy + 5, 1, 1, INK);
    } else if (mood == OK_MOOD_HUNGRY || mood == OK_MOOD_LONELY) {
        rect(f, cx - 1, cy + 5, 3, 3, INK);
    } else {
        rect(f, cx - 2, cy + 6, 5, 1, INK);
    }
}

static void room(frame f, const uint8_t (*font)[8], const ok_ui *u, const ok_pet *p, int hour, int minute)
{
    int i, mood = ok_pet_mood(p);
    char clock[8];
    rect(f, 0, 0, OK_UI_W, 118, WALL);
    for (i = 12; i < OK_UI_W; i += 24) rect(f, i, 0, 1, 118, WALL_LINE);
    rect(f, 0, 118, OK_UI_W, 74, FLOOR);
    for (i = 130; i < OK_UI_H; i += 14) rect(f, 0, i, OK_UI_W, 1, FLOOR_LINE);
    rect(f, 0, 116, OK_UI_W, 3, WOOD);
    window_view(f, hour);
    /* The wall: the newest four framed drawings, each one seen in a block. */
    {
        int k, first = u->wall_count > 4 ? u->wall_count - 4 : 0;
        for (k = first; k < u->wall_count; k++) {
            int fx = 8 + (k - first) * 36, fy = 60, x, y;
            frame_box(f, fx, fy, 36, 36, WOOD, WOOD);
            rect(f, fx + 2, fy + 2, 32, 32, PAPER);
            for (y = 0; y < 32; y++)
                for (x = 0; x < 32; x++)
                    if (ok_drawing_get(u->wall[k], 2 * x, 2 * y) || ok_drawing_get(u->wall[k], 2 * x + 1, 2 * y) ||
                        ok_drawing_get(u->wall[k], 2 * x, 2 * y + 1) || ok_drawing_get(u->wall[k], 2 * x + 1, 2 * y + 1))
                        f[(fy + 2 + y) * OK_UI_W + fx + 2 + x] = INK;
        }
    }
    /* Bed, plant. */
    rect(f, 14, 104, 84, 30, WOOD);
    rect(f, 18, 98, 22, 12, PAPER);
    rect(f, 40, 100, 56, 22, BLANKET);
    rect(f, 232, 104, 14, 14, WOOD);
    disc(f, 239, 96, 9, GREEN);
    if (p->asleep) {
        face(f, 30, 96, OK_MOOD_ASLEEP, u->ticks);
        text(f, font, 46, 76 - (int)(u->ticks / 20 % 4), "z", INK, 1);
        text(f, font, 54, 68 - (int)(u->ticks / 20 % 4), "Z", INK, 1);
        for (i = 0; i < OK_UI_W * OK_UI_H; i++) f[i] = (uint16_t)(0x8000u | (f[i] & 0x7bdeu) >> 1); /* lights off */
    } else {
        int bob = (int)(u->ticks / 30 % 2), x = 136;
        rect(f, x - 7, 128, 6, 16, PANTS);
        rect(f, x + 2, 128, 6, 16, PANTS);
        rect(f, x - 10, 106 + bob, 21, 24 - bob, SHIRT);
        rect(f, x - 14, 108 + bob, 4, 16, SKIN);
        rect(f, x + 11, 108 + bob, 4, 16, SKIN);
        face(f, x, 94 + bob, mood, u->ticks);
    }
    needs_panel(f, font, p);
    snprintf(clock, sizeof clock, "%02d:%02d", hour % 24, minute % 60);
    frame_box(f, 196, 6, 54, 12, PAPER, MUTED);
    text(f, font, 203, 8, clock, INK, 1);
    bubble(f, font, u->say ? u->say : ok_pet_line(p, hour));
}

/* Bottom screen buttons: four columns, two rows. */
enum { B_FEED, B_PLAY, B_PET, B_SLEEP, B_DRAW, B_PHOTO, B_MAIL, B_DEV, BUTTONS };
static const char *labels[BUTTONS] = {"Feed", "Play", "Pet", "Sleep", "Draw", "Photo", "Mail", "Dev"};
static const int soon[BUTTONS] = {0, 0, 0, 0, 0, 1, 1, 0};
#define BX(i) (8 + ((i) % 4) * 62)
#define BY(i) (36 + ((i) / 4) * 66)
#define BW 54
#define BH 58

static void icon(frame f, int i, int cx, int cy, uint16_t c)
{
    switch (i) {
    case B_FEED: disc(f, cx, cy + 2, 8, RGB(27, 6, 6)); rect(f, cx, cy - 9, 2, 5, WOOD); break;
    case B_PLAY: disc(f, cx, cy, 9, RGB(8, 14, 28)); rect(f, cx - 9, cy - 1, 19, 2, PAPER); break;
    case B_PET: disc(f, cx - 4, cy - 2, 5, RGB(28, 9, 14)); disc(f, cx + 4, cy - 2, 5, RGB(28, 9, 14));
        rect(f, cx - 7, cy, 15, 4, RGB(28, 9, 14)); rect(f, cx - 4, cy + 4, 9, 3, RGB(28, 9, 14));
        rect(f, cx - 1, cy + 7, 3, 2, RGB(28, 9, 14)); break;
    case B_SLEEP: disc(f, cx, cy, 9, RGB(28, 26, 12)); disc(f, cx + 5, cy - 3, 8, PAPER); break;
    case B_DRAW: rect(f, cx - 8, cy + 6, 16, 3, c); rect(f, cx - 2, cy - 9, 5, 14, RGB(29, 22, 6));
        rect(f, cx - 1, cy + 5, 3, 2, INK); break;
    case B_PHOTO: rect(f, cx - 10, cy - 5, 21, 14, c); disc(f, cx, cy + 2, 4, PAPER); rect(f, cx - 4, cy - 8, 8, 3, c); break;
    case B_MAIL: rect(f, cx - 10, cy - 6, 21, 14, c); rect(f, cx - 8, cy - 4, 17, 10, PAPER);
        rect(f, cx - 4, cy, 9, 1, c); break;
    default: rect(f, cx - 8, cy - 2, 17, 4, c); rect(f, cx - 2, cy - 8, 4, 17, c); break;
    }
}

static void home_buttons(frame f, const uint8_t (*font)[8], const ok_ui *u, const ok_pet *p)
{
    int i;
    centred(f, font, 128, 10, "OKUPA", PAPER, 2);
    for (i = 0; i < BUTTONS; i++) {
        uint16_t c = soon[i] ? MUTED : INK;
        const char *label = i == B_SLEEP && p->asleep ? "Wake" : labels[i];
        frame_box(f, BX(i), BY(i), BW, BH, soon[i] ? RGB(22, 21, 19) : PAPER, INK);
        icon(f, i, BX(i) + BW / 2, BY(i) + 20, c);
        centred(f, font, BX(i) + BW / 2, BY(i) + 40, label, c, 1);
        if (soon[i]) centred(f, font, BX(i) + BW / 2, BY(i) + 4, "soon", RGB(26, 9, 6), 1);
    }
    centred(f, font, 128, 178, u->net_status[0] ? u->net_status : "Tap to look after your okupa", RGB(20, 18, 15), 1);
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
    for (i = 0; i < OK_UI_DEV_LINES; i++) text(f, font, 6, 20 + i * 10, u->dev[i], PAPER, 1);
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

void ok_ui_render(const ok_ui *u, const ok_pet *p, int hour, int minute, const uint8_t (*font)[8],
                  uint16_t top[OK_UI_W * OK_UI_H], uint16_t bottom[OK_UI_W * OK_UI_H])
{
    int x, y;
    room(top, font, u, p, hour, minute);
    rect(bottom, 0, 0, OK_UI_W, OK_UI_H, RGB(9, 8, 8));
    for (y = 4; y < OK_UI_H; y += 8)
        for (x = 4; x < OK_UI_W; x += 8) rect(bottom, x, y, 1, 1, RGB(12, 11, 11));
    if (u->scene == OK_UI_SKETCH) {
        sketch(bottom, font, u);
        bubble(top, font, "Draw me something for the house!");
    } else if (u->scene == OK_UI_DEV) {
        dev(bottom, font, u);
    } else if (u->scene == OK_UI_WIFI) {
        wifi(bottom, font, u);
        bubble(top, font, u->wifi_status[0] ? u->wifi_status : "Which Wi-Fi do we use?");
    } else if (u->scene == OK_UI_PASSWORD) {
        password(bottom, font, u);
        bubble(top, font, "Psst. I won't peek.");
    } else if (u->scene == OK_UI_WALLET) {
        wallet(bottom, font, u);
        bubble(top, font, u->wallet_ready ? "My very own address!" : "Scribble! Wilder! More!");
    } else {
        home_buttons(bottom, font, u, p);
    }
}

void ok_ui_init(ok_ui *u)
{
    memset(u, 0, sizeof *u);
    u->last_x = u->last_y = -1;
}

int ok_ui_tick(ok_ui *u)
{
    u->ticks++;
    if (u->say_ticks && --u->say_ticks == 0) u->say = NULL;
    return u->ticks % 6 == 0 ? OK_UI_CHANGED : OK_UI_NOTHING; /* 10 fps is plenty for this art */
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
                u->scene = OK_UI_HOME;
                say(u, "I'll keep it safe. One day it goes on the wall.");
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
    for (i = 0; i < BUTTONS; i++) {
        int action, ok;
        if (!inside(x, y, BX(i), BY(i), BW, BH)) continue;
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
        return OK_UI_CHANGED;
    }
    return OK_UI_NOTHING;
}
