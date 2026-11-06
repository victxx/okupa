/* Sheets of the okupa for choosing and painting it, drawn with the game's
 * own code. Host only.
 *   build/okupa-monkey <out dir>
 * monkey-skin-template.png  the 128x128 skin, unfolded, each part labelled
 * monkey-skin-map.png       a test pattern on the skin, front / side / back
 * monkey-looks.png          what it can wear, its tops and its moods */
#include "okupa_font.h"
#include "okupa_monkey.h"
#include "png.h"

#define W OK_R3D_W
#define H OK_R3D_H
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define CHARCOAL RGB(5, 5, 6)
#define LABEL RGB(28, 27, 25)
#define DIM RGB(16, 16, 16)

static uint16_t frame[W * H], skin[OK_SKIN * OK_SKIN];

static void text(uint16_t *canvas, int cw, int x, int y, const char *s, uint16_t c)
{
    const uint8_t (*font)[8] = ok_font();
    int row, col;
    for (; *s; s++, x += 8)
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (font[*s & 127][row] >> col & 1) canvas[(y + row) * cw + x + col] = c;
}

static void centred(uint16_t *canvas, int cw, int cx, int y, const char *s, uint16_t c)
{
    text(canvas, cw, cx - (int)strlen(s) * 4, y, s, c);
}

/* One view of the monkey, the camera turned round it by yaw. */
static void shot(const ok_look *look, const uint16_t *tex, int pose, uint32_t ticks, int yaw)
{
    ok_monkey_face face;
    ok_r3d_begin(frame, CHARCOAL);
    ok_r3d_camera(OK_V3(0, 6.2, 28), OK_V3(0, 4.7, 0), 300);
    ok_r3d_light(OK_V3(-0.5, 1.0, 0.7), OK_V3(0.48, 0.46, 0.42), OK_V3(0.62, 0.62, 0.66));
    ok_r3d_material(RGB(9, 9, 10), 1, 0);
    ok_r3d_disc(OK_V3(0, 0, 0), OK_FX(4.5), OK_FX(3.2), 20);
    ok_r3d_push();
    ok_r3d_rotate_y(yaw);
    if (pose == OK_POSE_ASLEEP) ok_r3d_translate(OK_V3(2.5, 0.2, 0));
    ok_monkey_draw(look, tex, pose, ticks, &face);
    ok_r3d_pop();
    ok_r3d_finish();
    ok_monkey_face_draw(frame, &face);
}

#define CELL_W 120
#define CELL_H 136

/* The middle of the frame into a cell of the canvas, with a caption. */
static void cell(uint16_t *canvas, int cw, int col, int row, int top, const char *caption)
{
    int x0 = 8 + col * (CELL_W + 8), y0 = top + row * (CELL_H + 22), y;
    for (y = 0; y < CELL_H; y++) memcpy(canvas + (y0 + y) * cw + x0, frame + (y + 14) * W + (W - CELL_W) / 2, CELL_W * 2);
    centred(canvas, cw, x0 + CELL_W / 2, y0 + CELL_H + 6, caption, LABEL);
}

static void looks(const char *dir)
{
    enum { COLS = 4, CW = 8 + COLS * (CELL_W + 8), CH = 32 + 5 * (CELL_H + 22) + 32 };
    static uint16_t canvas[CW * CH];
    static const char *moods[OK_POSES] = {"Idle", "Happy", "Hungry", "Sleepy", "Asleep"};
    char path[512];
    ok_look look = {OK_WEAR_NONE, OK_TOP_HOODIE, 0};
    int i;
    for (i = 0; i < CW * CH; i++) canvas[i] = CHARCOAL;
    text(canvas, CW, 8, 8, "Instead of the beanie: what it can wear", LABEL);
    for (i = 0; i < OK_WEARS; i++) {
        look.wear = (uint8_t)i;
        ok_monkey_skin(&look, skin);
        shot(&look, skin, OK_POSE_IDLE, 0, 0);
        cell(canvas, CW, i % COLS, i / COLS, 28, ok_wear_name(i));
    }
    text(canvas, CW, 8, 28 + 2 * (CELL_H + 22), "Tops, any of 8 colours", LABEL);
    {
        static const ok_look tops[4] = {{OK_WEAR_NONE, OK_TOP_HOODIE, 0}, {OK_WEAR_NONE, OK_TOP_TEE, 1},
                                        {OK_WEAR_NONE, OK_TOP_STRIPES, 3}, {OK_WEAR_NONE, OK_TOP_TEE, 2}};
        static const char *names[4] = {"Hoodie", "T-shirt", "Stripes", "T-shirt"};
        for (i = 0; i < 4; i++) {
            ok_monkey_skin(&tops[i], skin);
            shot(&tops[i], skin, OK_POSE_IDLE, 0, 0);
            cell(canvas, CW, i, 2, 48, names[i]);
        }
        for (i = 0; i < OK_COLOURS; i++) {
            int x0 = 8 + 4 * (CELL_W + 8) - 8 - (OK_COLOURS - i) * 22, y0 = 28 + 2 * (CELL_H + 22), x, y;
            for (y = 0; y < 12; y++)
                for (x = 0; x < 18; x++) canvas[(y0 - 2 + y) * CW + x0 + x] = ok_top_colour(i);
        }
    }
    text(canvas, CW, 8, 48 + 3 * (CELL_H + 22), "Moods", LABEL);
    look.wear = OK_WEAR_HEADPHONES;
    look.top = OK_TOP_TEE;
    look.colour = 1;
    ok_monkey_skin(&look, skin);
    for (i = 0; i < OK_POSES; i++) {
        uint32_t t = i == OK_POSE_HAPPY ? 8 : i == OK_POSE_SLEEPY ? 250 : 30;
        shot(&look, skin, i, t, 0);
        cell(canvas, CW, i % COLS, 3 + i / COLS, 68, moods[i]);
    }
    snprintf(path, sizeof path, "%s/monkey-looks.png", dir);
    png_write(path, canvas, CW, CH, 2);
}

/* The skin unfolded at 4x, each part outlined and named. */
static void template(const char *dir)
{
    enum { Z = 4, S = OK_SKIN * Z, CW = S + 16, CH = S + 64 };
    static uint16_t canvas[CW * CH];
    ok_look look = {OK_WEAR_NONE, OK_TOP_HOODIE, 0};
    char path[512];
    int i, x, y;
    ok_monkey_skin(&look, skin);
    for (i = 0; i < CW * CH; i++) canvas[i] = CHARCOAL;
    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++) {
            uint16_t c = skin[(y / Z) * OK_SKIN + x / Z];
            if ((x % (8 * Z) == 0) || (y % (8 * Z) == 0)) c = (uint16_t)(0x8000 | ((c >> 1) & 0x3def) + ((c >> 2) & 0x1ce7));
            canvas[(y + 40) * CW + 8 + x] = c;
        }
    for (i = 0; i < OK_PARTS; i++) {
        const ok_part *p = ok_monkey_part(i);
        int x0 = 8 + p->x * Z, y0 = 40 + p->y * Z, w = p->w * Z, h = p->h * Z;
        for (x = 0; x < w; x++) canvas[y0 * CW + x0 + x] = canvas[(y0 + h - 1) * CW + x0 + x] = RGB(31, 31, 31);
        for (y = 0; y < h; y++) canvas[(y0 + y) * CW + x0] = canvas[(y0 + y) * CW + x0 + w - 1] = RGB(31, 31, 31);
        for (y = 0; y < h; y += 4) canvas[(y0 + y) * CW + x0 + w / 2] = RGB(31, 31, 8); /* the front */
        for (y = 0; y < 10; y++)
            for (x = 0; x < (int)strlen(p->name) * 8 + 4; x++) canvas[(y0 + 3 + y) * CW + x0 + 3 + x] = RGB(2, 2, 2);
        text(canvas, CW, x0 + 5, y0 + 4, p->name, RGB(31, 31, 31));
    }
    text(canvas, CW, 8, 8, "Okupa skin, 128 x 128 (shown 4x)", LABEL);
    text(canvas, CW, 8, 22, "Yellow: front of each part. Top row = top of part.", DIM);
    text(canvas, CW, 8, CH - 18, "Left/right edges of a part meet at its back.", DIM);
    snprintf(path, sizeof path, "%s/monkey-skin-template.png", dir);
    png_write(path, canvas, CW, CH, 1);
    /* The same skin without guides, to paint over. */
    snprintf(path, sizeof path, "%s/monkey-skin.png", dir);
    png_write(path, skin, OK_SKIN, OK_SKIN, 4);
}

/* A test skin: each part its own colour, checks of 8 texels, a white line
 * down its front and a dark band along its top. */
static void map(const char *dir)
{
    static const uint16_t hues[OK_PARTS] = {RGB(28, 8, 8),  RGB(8, 20, 30), RGB(29, 26, 6), RGB(30, 15, 4),
                                            RGB(22, 8, 26), RGB(30, 12, 22), RGB(8, 26, 12), RGB(6, 24, 26),
                                            RGB(24, 24, 24), RGB(18, 28, 8), RGB(12, 12, 30)};
    enum { CW = 8 + 3 * (CELL_W + 8), CH = 32 + CELL_H + 22 };
    static uint16_t canvas[CW * CH];
    ok_look look = {OK_WEAR_NONE, OK_TOP_HOODIE, 0};
    char path[512];
    int i, x, y;
    for (i = 0; i < OK_PARTS; i++) {
        const ok_part *p = ok_monkey_part(i);
        for (y = 0; y < p->h; y++)
            for (x = 0; x < p->w; x++) {
                uint16_t c = hues[i];
                if ((x / 8 + y / 8) % 2) c = (uint16_t)(0x8000 | ((c >> 1) & 0x3def) + ((c >> 2) & 0x1ce7));
                if (x == p->w / 2) c = RGB(31, 31, 31);
                if (y < 3) c = RGB(4, 4, 4);
                skin[(p->y + y) * OK_SKIN + p->x + x] = c;
            }
    }
    for (i = 0; i < CW * CH; i++) canvas[i] = CHARCOAL;
    text(canvas, CW, 8, 8, "Test pattern on the skin", LABEL);
    shot(&look, skin, OK_POSE_IDLE, 0, 0);
    cell(canvas, CW, 0, 0, 28, "front");
    shot(&look, skin, OK_POSE_IDLE, 0, OK_TURN / 4);
    cell(canvas, CW, 1, 0, 28, "side");
    shot(&look, skin, OK_POSE_IDLE, 0, OK_TURN / 2);
    cell(canvas, CW, 2, 0, 28, "back");
    snprintf(path, sizeof path, "%s/monkey-skin-map.png", dir);
    png_write(path, canvas, CW, CH, 2);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/preview";
    template(dir);
    map(dir);
    looks(dir);
    return 0;
}
