#include "okupa_house.h"
#include "okupa_act.h"
#include <string.h>

#define W OK_R3D_W
#define H OK_R3D_H
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define CHARCOAL RGB(2, 2, 3) /* nearly black around the attic: it floats like a diorama */
#define DEG(d) ((d) * OK_TURN / 360)
#define U(v) ((ok_fx)((v) * 4096 / 100)) /* hundredths of a unit */

static ok_v3 P(int x, int y, int z)
{
    return ok_v3_make(U(x), U(y), U(z));
}

static void box(int x0, int y0, int z0, int x1, int y1, int z1)
{
    ok_r3d_box(P(x0, y0, z0), P(x1, y1, z1));
}

static void blob(int x, int y, int z, int rx, int ry, int rz)
{
    ok_r3d_ellipsoid(P(x, y, z), P(rx, ry, rz), 10, 7);
}

/* A quad cut into nu x nv smaller ones (corners counterclockwise from the
 * front): textures stay straight in perspective, small triangles warp little. */
static void quad_grid(ok_v3 a, ok_v3 b, ok_v3 c, ok_v3 d, int nu, int nv)
{
    int i, j;
    for (j = 0; j < nv; j++)
        for (i = 0; i < nu; i++) {
            ok_v3 p[4];
            int k;
            for (k = 0; k < 4; k++) {
                int u = i + (k == 1 || k == 2), v = j + (k >= 2);
                /* bilinear: along a-b and d-c, then between them */
                ok_fx x0 = a.x + (b.x - a.x) * u / nu, y0 = a.y + (b.y - a.y) * u / nu, z0 = a.z + (b.z - a.z) * u / nu;
                ok_fx x1 = d.x + (c.x - d.x) * u / nu, y1 = d.y + (c.y - d.y) * u / nu, z1 = d.z + (c.z - d.z) * u / nu;
                p[k] = ok_v3_make(x0 + (x1 - x0) * v / nv, y0 + (y1 - y0) * v / nv, z0 + (z1 - z0) * v / nv);
            }
            ok_r3d_quad(p[0], p[1], p[2], p[3]);
        }
}

static void limb_house(int x0, int y0, int z0, int x1, int y1, int z1, int r)
{
    ok_r3d_capsule(P(x0, y0, z0), P(x1, y1, z1), U(r), 6);
}

static int next_id;

static void mat(uint16_t c, int flags)
{
    ok_r3d_texture(NULL, 0);
    ok_r3d_material(c, ++next_id, flags);
}

/* The same, from the outside's skin (game/src/outside_skin.c). */
static void otex(int tile, int per_unit, uint16_t c, int flags)
{
    ok_r3d_texture(ok_outside_skin, 128);
    ok_r3d_uv(tile % 4 * 32, tile / 4 * 32, 32, 32);
    ok_r3d_tile(per_unit);
    ok_r3d_material(c, ++next_id, flags | OK_R3D_TEXTURED);
}

/* A textured material: one tile of the attic's skin, repeated `per_unit`
 * texels to a unit (0: wrapped once round a curved shape). c is its outline. */
static void tex(int tile, int per_unit, uint16_t c, int flags)
{
    ok_r3d_texture(ok_room_skin, 128);
    ok_r3d_uv(tile % 4 * 32, tile / 4 * 32, 32, 32);
    ok_r3d_tile(per_unit);
    ok_r3d_material(c, ++next_id, flags | OK_R3D_TEXTURED);
}

void ok_house_room(ok_monkey_room *room)
{
    memset(room, 0, sizeof *room);
    room->home_x = 250;
    room->home_z = 300;
    room->bed_x = -680; /* lying on its side, the head on the pillow */
    room->bed_y = 300;
    room->bed_z = -640;
    room->spot_x[0] = 150, room->spot_z[0] = -380; /* under the window */
    room->spot_x[1] = 520, room->spot_z[1] = -330; /* by the boxes */
    room->spot_x[2] = -820, room->spot_z[2] = 520; /* by the plant */
}

/* ----------------------------------------------------------------- light */

/* The day in keys: minute, sky, ambient and key light (thousandths). */
typedef struct {
    int minute;
    uint8_t sky[3];
    int16_t amb[3], key[3];
} light_key;

static const light_key day_keys[] = {
    {0, {3, 4, 12}, {240, 250, 420}, {520, 380, 220}},
    {330, {3, 4, 12}, {240, 250, 420}, {520, 380, 220}},
    {400, {28, 17, 20}, {480, 430, 520}, {560, 400, 340}},
    {480, {15, 24, 31}, {620, 620, 660}, {480, 460, 420}},
    {1020, {15, 24, 31}, {620, 620, 660}, {480, 460, 420}},
    {1140, {30, 19, 11}, {560, 480, 450}, {600, 420, 250}},
    {1230, {16, 9, 19}, {380, 340, 500}, {500, 330, 280}},
    {1290, {3, 4, 12}, {240, 250, 420}, {520, 380, 220}},
    {1440, {3, 4, 12}, {240, 250, 420}, {520, 380, 220}},
};

typedef struct {
    uint16_t sky;
    ok_v3 amb, key;
    int night, day, sun; /* sun: 0..1000 across the window, -1 if none */
} light;

static int mix(int a, int b, int t, int n)
{
    return a + (b - a) * t / n;
}

static void light_at(int minute, int lights_off, light *l)
{
    int i = 0, t, n;
    const light_key *a, *b;
    while (day_keys[i + 1].minute <= minute && i + 2 < (int)(sizeof day_keys / sizeof day_keys[0])) i++;
    a = &day_keys[i];
    b = &day_keys[i + 1];
    t = minute - a->minute;
    n = b->minute - a->minute;
    l->sky = RGB(mix(a->sky[0], b->sky[0], t, n), mix(a->sky[1], b->sky[1], t, n), mix(a->sky[2], b->sky[2], t, n));
    l->amb = ok_v3_make(mix(a->amb[0], b->amb[0], t, n) * 4096 / 1000, mix(a->amb[1], b->amb[1], t, n) * 4096 / 1000,
                        mix(a->amb[2], b->amb[2], t, n) * 4096 / 1000);
    l->key = ok_v3_make(mix(a->key[0], b->key[0], t, n) * 4096 / 1000, mix(a->key[1], b->key[1], t, n) * 4096 / 1000,
                        mix(a->key[2], b->key[2], t, n) * 4096 / 1000);
    l->night = minute < 390 || minute >= 1200;
    l->day = minute >= 450 && minute < 1080;
    l->sun = minute >= 420 && minute < 1170 ? (minute - 420) * 1000 / 750 : -1;
    if (lights_off && l->night) {
        l->amb = ok_v3_make(900, 980, 1560); /* moonlight: dark, but it can still be seen */
        l->key = ok_v3_make(1000, 1160, 1760);
    }
}

static void apply(const light *l)
{
    ok_r3d_light(ok_v3_make(-2048, 4096, 2867), l->key, l->amb);
}

/* ------------------------------------------------------------ the room */

/* The gallery: six frames on the gable wall, each a 64x64 one-bit picture. */
static uint8_t gallery[OK_HOUSE_FRAMES][OK_DRAWING_BYTES], hung[OK_HOUSE_FRAMES];
static uint16_t art[OK_HOUSE_FRAMES][64 * 64];
/* Where each frame hangs (centre x, y, half size, tilt in degrees, wood). */
static const int16_t spots[6][5] = {
    {-1050, 520, 165, -2, 0}, {-660, 545, 150, 2, 1}, {-270, 560, 140, -1, 2},
    {150, 545, 150, 3, 1},   {-650, 1010, 140, 2, 2}, {650, 1010, 140, -3, 0},
};

void ok_house_frame(int slot, const uint8_t *pixels)
{
    if (slot < 0 || slot >= OK_HOUSE_FRAMES) return;
    hung[slot] = pixels != NULL;
    if (pixels) memcpy(gallery[slot], pixels, OK_DRAWING_BYTES);
}

/* One frame: wood, a cream mount, and the drawing in ink, or nothing yet
 * (pixels NULL): an empty frame waiting for its picture. */
static void framed_at(int slot, const uint8_t *pixels, int fx, int fy, int fz, int h, int tilt, int wood, int yaw, int lean)
{
    static const uint16_t woods[3] = {RGB(14, 9, 5), RGB(21, 14, 7), RGB(28, 26, 22)};
    int x, y, m = h * 82 / 100, d = h * 70 / 100;
    for (y = 0; y < 64 && pixels; y++)
        for (x = 0; x < 64; x++)
            art[slot][y * 64 + x] = ok_drawing_get(pixels, (unsigned)x, (unsigned)y) ? RGB(5, 4, 4) : RGB(30, 29, 26);
    ok_r3d_push();
    ok_r3d_translate(P(fx, fy, fz));
    ok_r3d_rotate_y(DEG(yaw));
    ok_r3d_rotate_x(-DEG(lean));
    ok_r3d_rotate_z(DEG(tilt));
    tex(OK_ROOM_BEAM, 8, woods[wood], OK_R3D_EDGE); /* the frame */
    box(-h, -h, 0, h, h, 26);
    mat(RGB(29, 27, 23), 0); /* the mount */
    ok_r3d_quad(P(-m, -m, 28), P(m, -m, 28), P(m, m, 28), P(-m, m, 28));
    if (pixels) {
        ok_r3d_texture(art[slot], 64);
        ok_r3d_uv(0, 0, 64, 64);
        ok_r3d_tile(0);
        ok_r3d_material(RGB(30, 29, 26), ++next_id, OK_R3D_TEXTURED);
    } else {
        mat(RGB(25, 23, 19), 0); /* empty: a bare, slightly shadowed backing */
    }
    ok_r3d_quad(P(-d, -d, 30), P(d, -d, 30), P(d, d, 30), P(-d, d, 30));
    if (!lean) {
        limb_house(0, h, 2, -h / 2, h + h / 2, -6, 4); /* the string and the nail */
        limb_house(0, h, 2, h / 2, h + h / 2, -6, 4);
    }
    ok_r3d_pop();
}

static void framed(int slot, const uint8_t *pixels)
{
    const int16_t *sp = spots[slot];
    framed_at(slot, pixels, sp[0], sp[1], -1096, sp[2], sp[3], sp[4], 0, 0);
}

static void bulbs(int x0, int y0, int x1, int y1, int z, int n, int on, int out[][2], int *count)
{
    static const uint16_t colours[] = {RGB(31, 26, 10), RGB(31, 15, 12), RGB(16, 28, 20), RGB(31, 26, 10), RGB(20, 22, 31)};
    int i;
    mat(RGB(4, 4, 4), 0);
    for (i = 0; i < n; i++) {
        int t0 = i * 1000 / n, t1 = (i + 1) * 1000 / n;
        int s0 = 90 * 4 * t0 / 1000 * (1000 - t0) / 1000, s1 = 90 * 4 * t1 / 1000 * (1000 - t1) / 1000;
        ok_r3d_capsule(P(x0 + (x1 - x0) * t0 / 1000, y0 + (y1 - y0) * t0 / 1000 - s0, z),
                       P(x0 + (x1 - x0) * t1 / 1000, y0 + (y1 - y0) * t1 / 1000 - s1, z), U(5), 4);
    }
    for (i = 1; i < n; i++) {
        int t = i * 1000 / n, s = 90 * 4 * t / 1000 * (1000 - t) / 1000;
        uint16_t c = colours[i % 5];
        ok_v3 at = P(x0 + (x1 - x0) * t / 1000, y0 + (y1 - y0) * t / 1000 - s - 35, z + 20);
        if (!on) c = RGB((c & 31) / 2 + 5, (c >> 5 & 31) / 2 + 5, (c >> 10 & 31) / 2 + 5);
        mat(c, on ? OK_R3D_UNLIT : 0);
        ok_r3d_ellipsoid(at, P(28, 36, 28), 6, 4);
        if (on && *count < 24 && ok_r3d_project(at, &out[*count][0], &out[*count][1], NULL)) (*count)++;
    }
}

/* Adds warm light around a point, fading out by radius r. */
static void glow(uint16_t *f, int cx, int cy, int r, int rr, int gg, int bb)
{
    int x, y;
    for (y = cy - r; y <= cy + r; y++)
        for (x = cx - r; x <= cx + r; x++) {
            int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy), k, c, R, G, B;
            if (x < 0 || y < 0 || x >= W || y >= H || d2 > r * r) continue;
            k = 256 - d2 * 256 / (r * r);
            c = f[y * W + x];
            R = (c & 31) + rr * k / 256;
            G = (c >> 5 & 31) + gg * k / 256;
            B = (c >> 10 & 31) + bb * k / 256;
            f[y * W + x] = RGB(R > 31 ? 31 : R, G > 31 ? 31 : G, B > 31 ? 31 : B);
        }
}

/* Brightens a convex quad on screen: the sunbeam. With okupa set, only the
 * okupa's pixels (objects 100 and up), so the beam falls on it too. */
static void beam(uint16_t *f, const int px[4], const int py[4], int add, int okupa)
{
    int y, i, y0 = H, y1 = -1;
    for (i = 0; i < 4; i++) {
        if (py[i] < y0) y0 = py[i];
        if (py[i] > y1) y1 = py[i];
    }
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    for (y = y0; y <= y1; y++) {
        int lo = W, hi = -1, x;
        for (i = 0; i < 4; i++) {
            int j = (i + 1) % 4, ya = py[i], yb = py[j];
            if ((y < ya) == (y < yb) || ya == yb) continue;
            x = px[i] + (px[j] - px[i]) * (y - ya) / (yb - ya);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        if (lo < 0) lo = 0;
        if (hi > W - 1) hi = W - 1;
        for (x = lo; x <= hi; x++) {
            int c = f[y * W + x], R = (c & 31) + add, G = (c >> 5 & 31) + add, B = (c >> 10 & 31) + add / 2;
            if (okupa && ok_r3d_id_at(x, y) < 100) continue;
            f[y * W + x] = RGB(R > 31 ? 31 : R, G > 31 ? 31 : G, B > 31 ? 31 : B);
        }
    }
}

static struct {
    int valid, key;
    uint32_t art_sum;
    int beam_x[4], beam_y[4], beam_add, candle_x, candle_y, stars[6][2], night, day, sun, lights_off;
} kept;

static void room(uint16_t *frame, const light *l, int lights_off, const uint8_t *drawing, int mask_on_floor)
{
    int i, lit[24][2], lights = 0, on = l->night && !lights_off;
    uint16_t wood = RGB(19, 12, 7), beam_c = RGB(12, 7, 4);
    next_id = 0;
    ok_r3d_begin(frame, CHARCOAL);
    ok_r3d_camera(ok_v3_make(0, U(2100), U(4600)), ok_v3_make(0, U(650), 0), 340);
    apply(l);
    /* Floor planks, wallpaper below the eaves, plaster above, a patch of bare brick. */
    tex(OK_ROOM_PLANKS, 4, wood, 0);
    quad_grid(P(-1400, 0, 1300), P(1400, 0, 1300), P(1400, 0, -1100), P(-1400, 0, -1100), 14, 12);
    tex(OK_ROOM_BEAM, 6, wood, 0); /* the floor's front edge */
    quad_grid(P(-1400, -100, 1300), P(1400, -100, 1300), P(1400, 0, 1300), P(-1400, 0, 1300), 14, 1);
    tex(OK_ROOM_WALLPAPER, 5, RGB(20, 22, 17), 0);
    quad_grid(P(-1400, 0, -1100), P(1400, 0, -1100), P(1400, 700, -1100), P(-1400, 700, -1100), 14, 4);
    tex(OK_ROOM_PLASTER, 5, RGB(24, 21, 17), 0);
    quad_grid(P(-1400, 700, -1100), P(1400, 700, -1100), P(0, 2100, -1100), P(0, 2100, -1100), 8, 7);
    tex(OK_ROOM_BRICK, 7, RGB(18, 8, 6), 0);
    quad_grid(P(380, 120, -1094), P(1150, 120, -1094), P(1250, 560, -1094), P(520, 640, -1094), 4, 3);
    tex(OK_ROOM_BOARDS, 6, RGB(16, 11, 7), OK_R3D_BOTH);
    quad_grid(P(-1400, 0, -1100), P(-1400, 700, -1100), P(-1400, 700, 1300), P(-1400, 0, 1300), 3, 12);
    quad_grid(P(1400, 0, -1100), P(1400, 700, -1100), P(1400, 700, 1300), P(1400, 0, 1300), 3, 12);
    /* The door in the right kneewall (out to the terrace) and a small window in the left one. */
    tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), OK_R3D_EDGE);
    box(1360, 0, -340, 1400, 600, -300);
    box(1360, 0, 260, 1400, 600, 300);
    tex(OK_ROOM_CRATE, 8, RGB(10, 6, 3), OK_R3D_EDGE);
    box(1370, 0, -300, 1398, 560, 260);
    ok_r3d_push();
    ok_r3d_translate(P(1384, 560, -20));
    ok_r3d_rotate_z(OK_TURN / 4);
    ok_r3d_cylinder(P(0, -14, 0), U(280), U(28), 10);
    ok_r3d_pop();
    mat(RGB(29, 24, 8), OK_R3D_EDGE);
    blob(1360, 300, 180, 20, 20, 20);
    tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), OK_R3D_EDGE);
    box(-1400, 260, 260, -1370, 600, 660);
    mat(l->sky, OK_R3D_UNLIT);
    box(-1372, 290, 290, -1366, 570, 630);
    tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), 0);
    box(-1368, 420, 290, -1360, 440, 630);
    /* Skirting, beams and the ridge. */
    tex(OK_ROOM_BEAM, 8, beam_c, 0);
    box(-1400, 0, -1100, 1400, 60, -1080);
    for (i = 0; i < 2; i++) {
        int z = -1040 + i * 700, s;
        for (s = -1; s <= 1; s += 2) {
            ok_r3d_push();
            ok_r3d_translate(P(s * 700, 1400, z));
            ok_r3d_rotate_z(-s * OK_TURN / 8);
            box(-960, -50, -40, 960, 50, 40);
            ok_r3d_pop();
        }
    }
    box(-1400, 660, -1090, 1400, 740, -1020);
    /* The round window: sky by the hour, the sun crossing it by day, the moon at night. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 1250, -1095));
    ok_r3d_rotate_x(OK_TURN / 4);
    tex(OK_ROOM_FRAME, 0, RGB(22, 21, 19), 0);
    ok_r3d_disc(P(0, 0, 0), U(340), U(340), 16);
    mat(l->sky, OK_R3D_UNLIT);
    ok_r3d_disc(P(0, 5, 0), U(280), U(280), 16);
    if (l->sun >= 0) {
        int sx = -230 + 460 * l->sun / 1000, sz = 60 - (l->sun - 500) * (l->sun - 500) / 2500;
        mat(RGB(31, 30, 18), OK_R3D_UNLIT);
        ok_r3d_disc(P(sx, 8, -sz), U(70), U(70), 10);
    } else if (l->night) {
        mat(RGB(29, 29, 24), OK_R3D_UNLIT);
        ok_r3d_disc(P(-110, 8, -90), U(70), U(70), 10);
        mat(l->sky, OK_R3D_UNLIT);
        ok_r3d_disc(P(-85, 10, -105), U(58), U(58), 10);
    }
    tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), 0);
    box(-280, 6, -22, 280, 22, 22);
    box(-22, 6, -280, 22, 22, 280);
    ok_r3d_pop();
    bulbs(-1300, 720, 0, 1950, -980, 9, on, lit, &lights);
    bulbs(0, 1950, 1300, 720, -980, 9, on, lit, &lights);
    /* The gallery: six frames, empty until a picture is hung in them. */
    {
        int k;
        for (k = 0; k < 6; k++) framed(k, hung[k] ? gallery[k] : k == 0 ? drawing : NULL);
    }
    /* A woven rug. */
    tex(OK_ROOM_RUG, 0, RGB(20, 8, 6), 0);
    ok_r3d_disc(P(200, 5, 300), U(660), U(430), 20);
    /* The mattress on a pallet, pillow, a knitted blanket folded at its foot. */
    tex(OK_ROOM_CRATE, 8, RGB(18, 12, 6), OK_R3D_EDGE);
    box(-1380, 0, -930, -300, 60, -350);
    tex(OK_ROOM_TICKING, 10, RGB(16, 18, 24), OK_R3D_EDGE);
    box(-1350, 60, -900, -330, 205, -380);
    tex(OK_ROOM_COTTON, 0, RGB(24, 22, 19), OK_R3D_EDGE);
    blob(-1120, 230, -640, 140, 34, 130);
    tex(OK_ROOM_KNIT, 6, RGB(5, 8, 16), OK_R3D_EDGE);
    box(-560, 205, -880, -340, 265, -400);
    /* Cardboard boxes, a little radio on top. */
    tex(OK_ROOM_CARDBOARD, 7, RGB(17, 12, 7), OK_R3D_EDGE);
    box(700, 0, -1000, 1150, 350, -600);
    box(760, 350, -960, 1100, 630, -660);
    box(1160, 0, -950, 1360, 220, -650);
    mat(RGB(24, 8, 7), OK_R3D_EDGE);
    box(820, 630, -880, 1040, 760, -760);
    mat(RGB(30, 28, 22), 0);
    ok_r3d_push();
    ok_r3d_translate(P(880, 695, -755));
    ok_r3d_rotate_x(OK_TURN / 4);
    ok_r3d_disc(P(0, 0, 0), U(42), U(42), 10);
    ok_r3d_pop();
    mat(RGB(6, 6, 6), 0);
    box(960, 660, -756, 1010, 730, -750);
    limb_house(1010, 760, -820, 1080, 880, -820, 8);
    /* The crate table with a mug and a candle. */
    tex(OK_ROOM_CRATE, 8, RGB(18, 12, 6), OK_R3D_EDGE);
    box(-250, 0, -1000, 150, 260, -700);
    mat(RGB(28, 28, 27), OK_R3D_EDGE);
    ok_r3d_cylinder(P(-120, 260, -850), U(50), U(90), 8);
    mat(RGB(30, 29, 25), OK_R3D_EDGE);
    ok_r3d_cylinder(P(60, 260, -800), U(26), U(70), 8);
    ok_r3d_project(P(60, 360, -800), &kept.candle_x, &kept.candle_y, NULL);
    /* Plants: one in an old tin, one in a clay pot. */
    tex(OK_ROOM_TIN, 6, RGB(14, 15, 16), OK_R3D_EDGE);
    ok_r3d_cylinder(P(-1150, 0, 600), U(90), U(160), 10);
    tex(OK_ROOM_LEAVES, 0, RGB(4, 12, 5), OK_R3D_EDGE);
    blob(-1150, 280, 600, 140, 150, 140);
    blob(-1230, 390, 580, 80, 100, 80);
    blob(-1060, 360, 640, 70, 80, 70);
    tex(OK_ROOM_CLAY, 8, RGB(18, 8, 4), OK_R3D_EDGE);
    ok_r3d_cylinder(P(1180, 0, 700), U(110), U(150), 10);
    tex(OK_ROOM_LEAVES, 0, RGB(4, 12, 5), OK_R3D_EDGE);
    blob(1180, 260, 700, 120, 110, 120);
    blob(1120, 390, 680, 70, 110, 70);
    blob(1250, 360, 720, 70, 90, 70);
    /* A pile of books by the plant, a floor cushion by the rug. */
    mat(RGB(9, 14, 22), OK_R3D_EDGE);
    box(-1300, 0, 150, -1020, 50, 330);
    mat(RGB(26, 20, 8), OK_R3D_EDGE);
    box(-1280, 50, 160, -1050, 95, 320);
    mat(RGB(22, 9, 8), OK_R3D_EDGE);
    box(-1290, 95, 170, -1060, 130, 315);
    tex(OK_ROOM_KNIT, 0, RGB(5, 8, 16), OK_R3D_EDGE);
    blob(900, 70, 760, 220, 75, 180);
    if (mask_on_floor) { /* the smiley mask, tossed on the floor */
        int k;
        ok_r3d_push();
        ok_r3d_translate(P(560, 22, 620));
        ok_r3d_rotate_x(-DEG(72));
        ok_r3d_rotate_z(DEG(24));
        mat(RGB(31, 26, 3), OK_R3D_EDGE);
        ok_r3d_ellipsoid(P(0, 0, 0), P(105, 105, 20), 14, 5);
        mat(RGB(3, 3, 2), OK_R3D_EDGE);
        ok_r3d_ellipsoid(P(-34, 28, 16), P(11, 24, 6), 6, 4);
        ok_r3d_ellipsoid(P(34, 28, 16), P(11, 24, 6), 6, 4);
        for (k = 0; k < 4; k++) {
            int a0 = 200 + k * 35, a1 = a0 + 35;
            limb_house((int)((int64_t)ok_r3d_cos(DEG(a0)) * 56 / 4096), (int)((int64_t)ok_r3d_sin(DEG(a0)) * 56 / 4096) + 4, 18,
                       (int)((int64_t)ok_r3d_cos(DEG(a1)) * 56 / 4096), (int)((int64_t)ok_r3d_sin(DEG(a1)) * 56 / 4096) + 4, 18, 7);
        }
        limb_house(-100, 10, -6, -170, -40, -10, 7); /* its elastic, trailing */
        limb_house(-170, -40, -10, -150, -110, -10, 7);
        ok_r3d_pop();
    }
    ok_r3d_tile(0);
    ok_r3d_finish();
    /* Light that is not geometry: the bulbs' glow, the candle, the sunbeam. */
    for (i = 0; i < lights; i++) glow(frame, lit[i][0], lit[i][1], 9, 4, 3, 1);
    if (l->night && !lights_off) glow(frame, kept.candle_x, kept.candle_y, 16, 5, 3, 0);
    if (l->sun >= 0) {
        static const int corner[4][3] = {{-240, 1450, -1090}, {240, 1450, -1090}, {700, 0, 700}, {-300, 0, 700}};
        int k;
        for (k = 0; k < 4; k++) {
            int sx = corner[k][0] + (k >= 2 ? (500 - l->sun) * 3 / 2 : 0);
            ok_r3d_project(P(sx, corner[k][1], corner[k][2]), &kept.beam_x[k], &kept.beam_y[k], NULL);
        }
        beam(frame, kept.beam_x, kept.beam_y, l->day ? 2 : 1, 0);
        kept.beam_add = l->day ? 2 : 1;
    }
    for (i = 0; i < 6; i++) {
        ok_r3d_project(P(-150 + i * 61 % 300, 1100 + (i * 137) % 300, -1080), &kept.stars[i][0], &kept.stars[i][1], NULL);
    }
    kept.night = l->night;
    kept.day = l->day && l->sun >= 0;
    kept.sun = l->sun >= 0;
    kept.lights_off = lights_off;
    ok_r3d_keep();
}

static uint32_t sum(const uint8_t *d)
{
    uint32_t s = 1;
    int i;
    if (!d) return 0;
    for (i = 0; i < OK_DRAWING_BYTES; i++) s = s * 31 + d[i];
    return s;
}

static void dot(uint16_t *f, int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < W && y < H) f[y * W + x] = c;
}

static int place;
static void outside_scene(const light *l, int on);
static void sky(uint16_t *f, int minute, int pan, uint32_t t);
static int sky_pan(ok_v3 eye, ok_v3 target);
static void fireflies(uint16_t *f, uint32_t t, int night);
static void fog(uint16_t *f, uint16_t haze, int near_units, int far_units);

/* The terrace, seen from the garden: kept like the attic, the okupa on top. */
static void garden_draw(uint16_t *frame, int m, const ok_look *look, const uint16_t *skin, const ok_monkey_anim *a,
                        ok_monkey_face *face)
{
    static const int cam[6] = {1300, 1050, 3400, 1300, 380, 0};
    uint32_t s = 0;
    int i, key = m / 5 * 4 + 3;
    light l;
    light_at(m, 0, &l);
    for (i = 6; i < OK_HOUSE_FRAMES; i++) s = s * 7 + (hung[i] ? sum(gallery[i]) + 1 : 0);
    if (!kept.valid || kept.key != key || kept.art_sum != s) {
        next_id = 0;
        ok_r3d_begin(frame, CHARCOAL);
        sky(frame, m, sky_pan(P(cam[0], cam[1], cam[2]), P(cam[3], cam[4], cam[5])), a->ticks);
        ok_r3d_camera(P(cam[0], cam[1], cam[2]), P(cam[3], cam[4], cam[5]), 330);
        apply(&l);
        outside_scene(&l, l.night || m >= 1140);
        fog(frame, frame[(H - 30) * W], 45, 150);
        ok_r3d_keep();
        kept.valid = 1;
        kept.key = key;
        kept.art_sum = s;
        kept.sun = 0;
        kept.night = 0;
    }
    ok_r3d_restore(frame);
    ok_r3d_camera(P(cam[0], cam[1], cam[2]), P(cam[3], cam[4], cam[5]), 330);
    apply(&l);
    ok_monkey_draw(look, skin, a, face);
    ok_r3d_finish();
    ok_monkey_face_draw(frame, face);
    fireflies(frame, a->ticks, l.night || m >= 1140);
}

void ok_house_draw(uint16_t *frame, int hour, int minute, int lights_off, const uint8_t *drawing, const ok_look *look,
                   const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face)
{
    if (place == OK_PLACE_GARDEN) {
        garden_draw(frame, (hour % 24) * 60 + minute % 60, look, skin, a, face);
        return;
    }
    {
        int mask_on_floor = look->wear != OK_WEAR_SMILEY;
        int m = (hour % 24) * 60 + minute % 60, key = ((m / 5 * 2 + (lights_off != 0)) * 2 + mask_on_floor) * 4, i;
        uint32_t s = sum(drawing), t = a->ticks;
        for (i = 0; i < OK_HOUSE_FRAMES; i++) s = s * 7 + (hung[i] ? sum(gallery[i]) + 1 : 0);
        light l;
        light_at(m, lights_off, &l);
        if (!kept.valid || kept.key != key || kept.art_sum != s) {
            room(frame, &l, lights_off, drawing, mask_on_floor);
            kept.valid = 1;
            kept.key = key;
            kept.art_sum = s;
        }
        ok_r3d_restore(frame);
        ok_r3d_camera(ok_v3_make(0, U(2100), U(4600)), ok_v3_make(0, U(650), 0), 340);
        apply(&l);
        ok_monkey_draw(look, skin, a, face);
        /* In bed, the knitted blanket over it, up to the shoulders. */
        if (a->s[OK_SPRING_LIE].x / 256 > 55) {
            tex(OK_ROOM_KNIT, 0, RGB(5, 8, 16), OK_R3D_EDGE);
            ok_r3d_ellipsoid(P(a->room.bed_x + 90, 318 + (int)((int64_t)ok_r3d_sin((int)(a->ticks * 10u)) * 6 / 4096), a->room.bed_z + 20),
                             P(300, 132, 250), 12, 7);
            ok_r3d_tile(0);
        }
        ok_r3d_finish();
        if (kept.sun) beam(frame, kept.beam_x, kept.beam_y, kept.beam_add, 1); /* standing in the sun, it is lit too */
        ok_monkey_face_draw(frame, face);
        /* Small living things. */
        if (kept.night)
            for (i = 0; i < 6; i++)
                if ((t / 20 + (uint32_t)i * 7) % 11 && ok_r3d_id_at(kept.stars[i][0], kept.stars[i][1]) < 100)
                    dot(frame, kept.stars[i][0], kept.stars[i][1], RGB(28, 28, 22));
        /* The flame only where nothing stands in front of the candle. */
        if (kept.night && !kept.lights_off && ok_r3d_id_at(kept.candle_x, kept.candle_y + 2) != 0 &&
            ok_r3d_id_at(kept.candle_x, kept.candle_y + 2) < 100 && ok_r3d_id_at(kept.candle_x, kept.candle_y - 3) < 100) {
            int fl = (int)((t / 5 + t / 13) % 3) - 1;
            dot(frame, kept.candle_x + fl / 2, kept.candle_y - 2, RGB(31, 30, 20));
            dot(frame, kept.candle_x, kept.candle_y - 1, RGB(31, 26, 8));
            dot(frame, kept.candle_x + fl, kept.candle_y - 3, RGB(31, 22, 6));
            dot(frame, kept.candle_x, kept.candle_y, RGB(31, 18, 4));
        }
        if (kept.day) /* dust drifting in the sunbeam */
            for (i = 0; i < 10; i++) {
                int u = (int)((t / 3 + (uint32_t)i * 97) % 600), v = (int)((t / 5 + (uint32_t)i * 53) % 400);
                int x = kept.beam_x[0] + (kept.beam_x[3] - kept.beam_x[0]) * v / 400 +
                        ((kept.beam_x[1] - kept.beam_x[0]) + ((kept.beam_x[2] - kept.beam_x[3]) - (kept.beam_x[1] - kept.beam_x[0])) * v / 400) * u / 600;
                int y = kept.beam_y[0] + (kept.beam_y[3] - kept.beam_y[0]) * v / 400 + (int)(ok_r3d_sin((int)(t * 9 + (uint32_t)i * 400)) * 2 / 4096);
                if (ok_r3d_id_at(x, y) < 100) dot(frame, x, y, RGB(31, 30, 24));
            }
    }
}

/* ------------------------------------------------------------- outside */

/* The house from outside: a little island floating in the dark, the house
 * with its tiled roof and round attic window, a tree, a path, a lamp, and
 * the terrace with its gallery (frames 6-11). Coordinates: the island's
 * centre at the origin, the house's front facing +z. */

/* The painted sky of the hour (game/src/sky_art.c: dawn, day, sunset,
 * night), two of them melting into each other between their hours, slid
 * a little sideways as the camera turns. The clouds are in the painting. */
static void sky(uint16_t *f, int minute, int pan, uint32_t t)
{
    static const int16_t keys[][2] = {{0, 3}, {300, 3}, {390, 0}, {480, 1}, {1050, 1}, {1140, 2}, {1230, 3}, {1440, 3}};
    int i = 0, x, y, k, a, b, ox = 16 + pan;
    while (keys[i + 1][0] <= minute && i + 2 < (int)(sizeof keys / sizeof keys[0])) i++;
    a = keys[i][1];
    b = keys[i + 1][1];
    k = (minute - keys[i][0]) * 256 / (keys[i + 1][0] - keys[i][0]);
    ox = ox < 0 ? 0 : ox > 32 ? 32 : ox;
    for (y = 0; y < H; y++) {
        const uint16_t *ra = ok_sky_art[a] + (y / 2) * 160 + ox, *rb = ok_sky_art[b] + (y / 2) * 160 + ox;
        for (x = 0; x < W; x++) {
            uint16_t c = ra[x / 2], d = rb[x / 2];
            f[y * W + x] = a == b ? c : RGB((c & 31) + (((d & 31) - (c & 31)) * k >> 8), (c >> 5 & 31) + (((d >> 5 & 31) - (c >> 5 & 31)) * k >> 8),
                                            (c >> 10 & 31) + (((d >> 10 & 31) - (c >> 10 & 31)) * k >> 8));
        }
    }
    (void)t;
}

/* How far the sky slides for a camera looking from eye to target. */
static int sky_pan(ok_v3 eye, ok_v3 target)
{
    return (int)((int64_t)ok_r3d_sin(ok_r3d_atan2(target.x - eye.x, eye.z - target.z)) * 16 / 4096);
}

/* A gnarled little tree: a leaning trunk, two branches, a canopy of clumps. */
static void tree(int x, int z, int s)
{
    otex(OK_OUT_BARK, 6, RGB(9, 5, 3), OK_R3D_EDGE);
    ok_r3d_capsule(P(x, 0, z), P(x + 60 * s / 100, 700 * s / 100, z), U(95 * s / 100), 7);
    ok_r3d_capsule(P(x + 60 * s / 100, 650 * s / 100, z), P(x - 260 * s / 100, 950 * s / 100, z + 60), U(50 * s / 100), 6);
    ok_r3d_capsule(P(x + 60 * s / 100, 700 * s / 100, z), P(x + 330 * s / 100, 980 * s / 100, z - 60), U(45 * s / 100), 6);
    otex(OK_OUT_LEAVES, 0, RGB(3, 9, 4), OK_R3D_EDGE);
    blob(x + 40 * s / 100, 1120 * s / 100, z, 400 * s / 100, 300 * s / 100, 380 * s / 100);
    blob(x - 300 * s / 100, 980 * s / 100, z + 80, 260 * s / 100, 220 * s / 100, 260 * s / 100);
    blob(x + 360 * s / 100, 1000 * s / 100, z - 60, 250 * s / 100, 210 * s / 100, 250 * s / 100);
    blob(x - 60 * s / 100, 1350 * s / 100, z - 40, 250 * s / 100, 200 * s / 100, 250 * s / 100);
    ok_r3d_tile(0);
}

static void easel(int slot, int x, int z, int yaw)
{
    ok_r3d_push();
    ok_r3d_translate(P(x, 0, z));
    ok_r3d_rotate_y(DEG(yaw));
    tex(OK_ROOM_CRATE, 8, RGB(18, 12, 6), OK_R3D_EDGE);
    limb_house(-120, 0, 60, -40, 760, -10, 14);
    limb_house(120, 0, 60, 40, 760, -10, 14);
    limb_house(0, 0, -200, 0, 720, -30, 12);
    box(-150, 300, 30, 150, 330, 90);
    ok_r3d_pop();
    framed_at(slot, hung[slot] ? gallery[slot] : NULL, x, 520, z + 40, 150, 0, slot % 3, yaw, 12);
}

/* A little temple for art on the terrace: a stone platform, four columns,
 * a gabled roof, and a back wall inside it with three frames (6-8). */
static void temple(void)
{
    static const int16_t pediment[3][2] = {{-820, 0}, {820, 0}, {0, 380}};
    int i, k;
    ok_r3d_push();
    ok_r3d_translate(P(1300, 0, -480));
    otex(OK_OUT_STONE, 5, RGB(13, 12, 10), OK_R3D_EDGE);
    box(-820, 0, -380, 820, 70, 380);
    box(-760, 70, -330, 760, 130, 330);
    otex(OK_OUT_STUCCO, 4, RGB(22, 19, 15), OK_R3D_EDGE);
    for (i = 0; i < 4; i++) {
        int x = i % 2 ? 640 : -640, z = i / 2 ? 230 : -230;
        ok_r3d_cylinder(P(x, 130, z), U(62), U(780), 8);
        box(x - 90, 900, z - 90, x + 90, 950, z + 90); /* capitals */
    }
    box(-720, 130, -300, 720, 900, -250); /* the back wall, for the pictures */
    otex(OK_OUT_STONE, 5, RGB(13, 12, 10), OK_R3D_EDGE);
    box(-800, 950, -330, 800, 1030, 330); /* the beam */
    ok_r3d_push();
    ok_r3d_translate(P(0, 1030, 330));
    otex(OK_OUT_STUCCO, 4, RGB(22, 19, 15), OK_R3D_EDGE);
    ok_r3d_extrude(pediment, 3, U(16), 0, 120);
    ok_r3d_pop();
    otex(OK_OUT_MOSS_TILES, 6, RGB(10, 6, 3), OK_R3D_EDGE | OK_R3D_FLAT);
    for (k = -1; k <= 1; k += 2) {
        ok_r3d_push();
        ok_r3d_translate(P(k * 420, 1225, 0));
        ok_r3d_rotate_z(-k * DEG(25));
        box(-500, -30, -400, 500, 30, 400);
        ok_r3d_pop();
    }
    otex(OK_OUT_IVY, 0, RGB(3, 9, 4), OK_R3D_EDGE); /* ivy round two columns */
    blob(-640, 520, 230, 95, 300, 95);
    blob(640, 300, -230, 95, 180, 95);
    ok_r3d_pop();
    for (i = 0; i < 3; i++) framed_at(6 + i, hung[6 + i] ? gallery[6 + i] : NULL, 860 + i * 440, 560 + (i % 2) * 30, -720, 150, (i - 1) * 2, i, 0, 0);
}

/* Where the fireflies are at a tick: slow loops round the island. */
static void firefly_at(int i, uint32_t t, int out[3])
{
    int a = (int)(t * (5u + (uint32_t)i % 4)) + i * 997, r = 900 + (i * 431) % 1900;
    out[0] = (int)((int64_t)ok_r3d_cos(a / 3 + i * 300) * r / 4096) + 300;
    out[2] = (int)((int64_t)ok_r3d_sin(a / 3 + i * 300) * r / 4096);
    out[1] = 250 + (i * 173) % 900 + (int)((int64_t)ok_r3d_sin(a) * 120 / 4096);
}

/* Fireflies at night, drifting motes of light by day; drawn over the frame. */
static void fireflies(uint16_t *f, uint32_t t, int night)
{
    int i, n = night ? 16 : 8;
    for (i = 0; i < n; i++) {
        int p[3], x, y, blink = (int)((t + (uint32_t)i * 37) % 90);
        firefly_at(i, t, p);
        if (!ok_r3d_project(P(p[0], p[1], p[2]), &x, &y, NULL)) continue;
        if (night) {
            if (blink < 60) glow(f, x, y, 4 + (blink < 30 ? blink / 10 : (60 - blink) / 10), 6, 6, 1);
            dot(f, x, y, RGB(31, 31, 18));
        } else if (blink < 70) {
            dot(f, x, y, RGB(31, 31, 28));
        }
    }
}

static void outside_scene(const light *l, int on)
{
    int i, k, lit[24][2], lights = 0;
    /* The island: a lawn with a lip of grass spilling over, cliffs below
     * that taper to a point, rock teeth and roots hanging off. */
    otex(OK_OUT_GRASS, 3, RGB(6, 12, 4), 0);
    ok_r3d_cylinder(P(0, -40, 0), U(2700), U(40), 18);
    otex(OK_OUT_GRASS, 3, RGB(6, 12, 4), 0);
    for (i = 0; i < 16; i++) {
        int a = DEG(i * 360 / 16 + 7), r = 2600 + (i * 37) % 120;
        blob((int)((int64_t)ok_r3d_cos(a) * r / 4096), -95, (int)((int64_t)ok_r3d_sin(a) * r / 4096), 560, 90, 500);
    }
    otex(OK_OUT_EARTH, 3, RGB(10, 5, 2), OK_R3D_FLAT | OK_R3D_EDGE);
    ok_r3d_cone(P(0, -900, 0), U(2000), U(2600), U(820), 16);
    ok_r3d_cone(P(0, -1700, 0), U(1250), U(2000), U(800), 14);
    ok_r3d_cone(P(0, -2700, 0), U(250), U(1250), U(1000), 12);
    for (i = 0; i < 14; i++) { /* rock teeth down the cliffs */
        int a = DEG(i * 360 / 14 + 13), r = 2150 - (i * 97) % 500, len = 700 + (i * 313) % 1100;
        ok_r3d_cone(P((int)((int64_t)ok_r3d_cos(a) * r / 4096), -700, (int)((int64_t)ok_r3d_sin(a) * r / 4096)), U(320), U(30), -U(len), 6);
    }
    mat(RGB(5, 12, 4), OK_R3D_EDGE); /* roots and vines hanging from the lip */
    for (i = 0; i < 10; i++) {
        int a = DEG(i * 36 + 20), r = 2720, x = (int)((int64_t)ok_r3d_cos(a) * r / 4096), z = (int)((int64_t)ok_r3d_sin(a) * r / 4096);
        int len = 400 + (i * 211) % 600;
        limb_house(x, -80, z, x + 40, -80 - len / 2, z + 30, 22);
        limb_house(x + 40, -80 - len / 2, z + 30, x, -80 - len, z + 10, 16);
    }
    otex(OK_OUT_EARTH, 3, RGB(10, 5, 2), OK_R3D_FLAT | OK_R3D_EDGE); /* little rocks floating near it */
    for (i = 0; i < 4; i++) {
        int a = DEG(i * 90 + 40), r = 3700 + i * 300, x = (int)((int64_t)ok_r3d_cos(a) * r / 4096), z = (int)((int64_t)ok_r3d_sin(a) * r / 4096);
        otex(OK_OUT_GRASS, 3, RGB(6, 12, 4), 0);
        ok_r3d_cylinder(P(x, -300 - i * 250, z), U(260 - i * 30), U(30), 8);
        otex(OK_OUT_EARTH, 3, RGB(10, 5, 2), OK_R3D_FLAT | OK_R3D_EDGE);
        ok_r3d_cone(P(x, -700 - i * 250, z), U(40), U(260 - i * 30), U(400), 7);
    }
    /* The house: the attic itself, seen from outside. Built in the attic's
     * own coordinates (walls at x +-14, the round window in the gable at
     * z -11, the door in the right kneewall), mirrored in z so the round
     * window faces the front, and shrunk to fit the island. A stone footing,
     * a timber frame, mossy tiles, ivy: a cottage from the country. */
    {
        static const int16_t gable[5][2] = {{-1420, 0}, {1420, 0}, {1420, 700}, {0, 2120}, {-1420, 700}};
        ok_r3d_push();
        ok_r3d_translate(P(-700, 0, -100));
        ok_r3d_scale(U(62), U(62), -U(62));
        otex(OK_OUT_STUCCO, 4, RGB(22, 19, 15), OK_R3D_EDGE);
        box(-1420, 0, -1100, -1380, 700, 1300); /* kneewalls */
        box(1380, 0, -1100, 1420, 700, 1300);
        for (i = 0; i < 2; i++) {
            ok_r3d_push();
            ok_r3d_translate(P(0, 0, i ? 1300 : -1120));
            ok_r3d_extrude(gable, 5, U(10), 0, 800);
            ok_r3d_pop();
        }
        otex(OK_OUT_STONE, 4, RGB(12, 11, 9), OK_R3D_EDGE);
        box(-1460, 0, -1160, 1460, 300, 1360); /* stone footing, waist high */
        otex(OK_OUT_BARK, 8, RGB(6, 4, 2), OK_R3D_EDGE); /* the timber frame on the gables */
        for (i = 0; i < 2; i++) {
            int z = i ? 1322 : -1142;
            box(-1440, 680, z - 12, 1440, 740, z + 12);
            box(-40, 740, z - 12, 40, 2080, z + 12);
            ok_r3d_push();
            ok_r3d_translate(P(-700, 1060, z));
            ok_r3d_rotate_z(OK_TURN / 8);
            box(-30, -480, -12, 30, 480, 12);
            ok_r3d_pop();
            ok_r3d_push();
            ok_r3d_translate(P(700, 1060, z));
            ok_r3d_rotate_z(-OK_TURN / 8);
            box(-30, -480, -12, 30, 480, 12);
            ok_r3d_pop();
        }
        /* The roof: two 45 degree slopes, mossy, a chimney. */
        otex(OK_OUT_MOSS_TILES, 6, RGB(10, 6, 3), OK_R3D_EDGE | OK_R3D_FLAT);
        for (k = -1; k <= 1; k += 2) {
            ok_r3d_push();
            ok_r3d_translate(P(k * 700, 1440, 100));
            ok_r3d_rotate_z(-k * OK_TURN / 8);
            box(-1180, -45, -1300, 1180, 45, 1300);
            ok_r3d_pop();
        }
        otex(OK_OUT_STONE, 6, RGB(12, 11, 9), OK_R3D_EDGE);
        box(500, 1500, -500, 780, 2600, -220);
        /* The round window, where the attic's is. */
        ok_r3d_push();
        ok_r3d_translate(P(0, 1250, -1150));
        ok_r3d_rotate_x(-OK_TURN / 4);
        tex(OK_ROOM_FRAME, 0, RGB(22, 21, 19), OK_R3D_EDGE);
        ok_r3d_disc(P(0, 0, 0), U(340), U(340), 16);
        mat(on ? RGB(31, 24, 11) : RGB(12, 16, 22), on ? OK_R3D_UNLIT : 0);
        ok_r3d_disc(P(0, 6, 0), U(282), U(282), 16);
        tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), 0);
        box(-282, 8, -22, 282, 24, 22);
        box(-22, 8, -282, 22, 24, 282);
        ok_r3d_pop();
        /* The door, big, round-topped, under a little porch roof, with a lantern. */
        otex(OK_OUT_DOOR, 8, RGB(8, 5, 3), OK_R3D_EDGE);
        box(1420, 0, -300, 1480, 560, 260);
        ok_r3d_push();
        ok_r3d_translate(P(1450, 560, -20));
        ok_r3d_rotate_z(OK_TURN / 4);
        ok_r3d_cylinder(P(0, -30, 0), U(280), U(60), 10);
        ok_r3d_pop();
        tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), OK_R3D_EDGE);
        box(1420, 0, -350, 1500, 640, -300);
        box(1420, 0, 260, 1500, 640, 310);
        mat(RGB(29, 24, 8), OK_R3D_EDGE);
        blob(1490, 300, 180, 24, 24, 24);
        otex(OK_OUT_MOSS_TILES, 6, RGB(10, 6, 3), OK_R3D_EDGE | OK_R3D_FLAT);
        ok_r3d_push();
        ok_r3d_translate(P(1560, 900, -20));
        ok_r3d_rotate_z(-DEG(25));
        box(-200, -25, -480, 200, 25, 440);
        ok_r3d_pop();
        mat(on ? RGB(31, 27, 12) : RGB(24, 23, 20), on ? OK_R3D_UNLIT | OK_R3D_EDGE : OK_R3D_EDGE);
        blob(1500, 700, 420, 40, 55, 40); /* the lantern */
        otex(OK_OUT_STONE, 6, RGB(12, 11, 9), OK_R3D_EDGE);
        box(1420, 0, -380, 1760, 70, 340); /* steps */
        box(1420, 0, -380, 1600, 140, 340);
        /* A small window in the left kneewall, as inside, with a flower box. */
        tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), OK_R3D_EDGE);
        box(-1480, 330, 260, -1420, 640, 660);
        mat(on ? RGB(31, 23, 10) : RGB(11, 15, 21), on ? OK_R3D_UNLIT : 0);
        box(-1490, 360, 290, -1480, 610, 630);
        otex(OK_OUT_CLOUD, 0, RGB(31, 18, 20), 0);
        mat(RGB(26, 9, 12), OK_R3D_EDGE);
        box(-1560, 290, 240, -1460, 340, 680);
        mat(RGB(30, 16, 22), OK_R3D_EDGE);
        blob(-1520, 370, 360, 50, 40, 60);
        blob(-1520, 370, 560, 50, 40, 60);
        /* Ivy climbing the corners and over the gable. */
        otex(OK_OUT_IVY, 0, RGB(3, 9, 4), OK_R3D_EDGE);
        blob(1330, 380, -1170, 150, 380, 60);
        blob(1180, 760, -1170, 120, 220, 55);
        blob(-1330, 300, -1170, 140, 280, 60);
        blob(1440, 420, 1100, 60, 360, 160);
        blob(-1440, 500, -900, 60, 420, 180);
        ok_r3d_pop();
    }
    /* Trees, well clear of the house. */
    tree(-1950, 1250, 90);
    tree(-1300, -1950, 70);
    /* The path to the door's steps, and the terrace with its art temple and easels. */
    otex(OK_OUT_COBBLE, 6, RGB(10, 10, 9), OK_R3D_EDGE | OK_R3D_FLAT);
    for (i = 0; i < 5; i++) ok_r3d_disc(P(330 + i * 110, 2, 250 + i * 330), U(150), U(115), 7);
    otex(OK_OUT_DECK, 4, RGB(19, 12, 7), 0);
    quad_grid(P(350, 1, 1000), P(2150, 1, 1000), P(2150, 1, -950), P(350, 1, -950), 6, 6);
    temple();
    easel(9, 650, 350, 28);
    easel(10, 1950, 350, -28);
    easel(11, 760, 820, 40);
    tex(OK_ROOM_BEAM, 6, RGB(12, 7, 4), OK_R3D_EDGE); /* posts and a rail */
    for (i = 0; i < 4; i++) box(2100, 0, -900 + i * 620, 2160, 520, -840 + i * 620);
    limb_house(2130, 500, -900, 2130, 500, 1000, 18);
    ok_r3d_push(); /* fairy lights along the rail */
    ok_r3d_rotate_y(OK_TURN / 4);
    bulbs(-1000, 560, 900, 560, 2130, 9, on, lit, &lights);
    ok_r3d_pop();
    mat(RGB(20, 22, 24), OK_R3D_EDGE);
    ok_r3d_cylinder(P(500, 0, 880), U(90), U(160), 8);
    otex(OK_OUT_LEAVES, 0, RGB(3, 9, 4), OK_R3D_EDGE);
    blob(500, 300, 880, 150, 160, 150);
    ok_r3d_tile(0);
    ok_r3d_finish();
    for (i = 0; i < lights; i++) glow(ok_r3d_frame(), lit[i][0], lit[i][1], 9, 4, 3, 1);
    (void)l;
}

/* Haze: what is far melts into the colour of the horizon. */
static void fog(uint16_t *f, uint16_t haze, int near_units, int far_units)
{
    int32_t zn = (int32_t)(((int64_t)1 << 34) / ((int64_t)near_units * 4096)), zf = (int32_t)(((int64_t)1 << 34) / ((int64_t)far_units * 4096));
    int32_t inv = (1 << 20) / (zn - zf > 0 ? zn - zf : 1);
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int32_t d = ok_r3d_depth_at(x, y), k;
            uint16_t c = f[y * W + x];
            if (!d || d >= zn) continue;
            k = (int32_t)((int64_t)(zn - d) * inv >> 12) * 6 / 10; /* 0..153 of 256: never all the way */
            if (k > 154) k = 154;
            f[y * W + x] = RGB((c & 31) + (((haze & 31) - (c & 31)) * k >> 8), (c >> 5 & 31) + (((haze >> 5 & 31) - (c >> 5 & 31)) * k >> 8),
                               (c >> 10 & 31) + (((haze >> 10 & 31) - (c >> 10 & 31)) * k >> 8));
        }
}

void ok_house_outside(uint16_t *frame, ok_v3 eye, ok_v3 target, int focal, int hour, int minute, uint32_t ticks)
{
    int m = (hour % 24) * 60 + minute % 60, i;
    light l;
    light_at(m, 0, &l);
    next_id = 0;
    ok_r3d_begin(frame, CHARCOAL);
    sky(frame, m, sky_pan(eye, target), ticks);
    ok_r3d_camera(eye, target, focal);
    apply(&l);
    outside_scene(&l, l.night || m >= 1140);
    fireflies(frame, ticks, l.night || m >= 1140);
    fog(frame, frame[(H - 30) * W], 70, 220);
    /* Smoke from the chimney. */
    for (i = 0; i < 4; i++) {
        int k = (int)((ticks / 2 + (uint32_t)i * 60) % 240), x, y;
        if (ok_r3d_project(P(-303 + k / 3 + (int)((int64_t)ok_r3d_sin((int)ticks * 8 + i * 900) * 30 / 4096), 1620 + k * 4, 123), &x, &y, NULL))
            glow(frame, x, y, 2 + k / 40, 2, 2, 2);
    }
    kept.valid = 0; /* the attic's kept frame is gone */
}

void ok_house_place(int where)
{
    if (where != place) kept.valid = 0;
    place = where;
}

int ok_house_where(void)
{
    return place;
}

void ok_house_garden(ok_monkey_room *room)
{
    memset(room, 0, sizeof *room);
    room->home_x = 1300;
    room->home_z = 420;
    room->bed_x = 1300; /* no bed out here: it goes in to sleep */
    room->bed_z = 0;
    room->spot_x[0] = 800, room->spot_z[0] = -350; /* looking at the wall's pictures */
    room->spot_x[1] = 1750, room->spot_z[1] = -350;
    room->spot_x[2] = 1650, room->spot_z[2] = 700;
}
