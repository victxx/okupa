#include "okupa_house.h"
#include "okupa_act.h"
#include <string.h>

#define W OK_R3D_W
#define H OK_R3D_H
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define CHARCOAL RGB(5, 5, 6)
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
    room->bed_x = -620;
    room->bed_y = 215;
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
        l->amb = ok_v3_make(660, 740, 1300);
        l->key = ok_v3_make(900, 1060, 1640);
    }
}

static void apply(const light *l)
{
    ok_r3d_light(ok_v3_make(-2048, 4096, 2867), l->key, l->amb);
}

/* ------------------------------------------------------------ the room */

static uint16_t art[64 * 64];

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

/* Brightens a convex quad on screen: the sunbeam. */
static void beam(uint16_t *f, const int px[4], const int py[4], int add)
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
            f[y * W + x] = RGB(R > 31 ? 31 : R, G > 31 ? 31 : G, B > 31 ? 31 : B);
        }
    }
}

static struct {
    int valid, key;
    uint32_t art_sum;
    int beam_x[4], beam_y[4], candle_x, candle_y, stars[6][2], night, day, lights_off;
} kept;

static void room(uint16_t *frame, const light *l, int lights_off, const uint8_t *drawing)
{
    int i, lit[24][2], lights = 0, on = l->night && !lights_off;
    uint16_t wood = RGB(19, 12, 7), beam_c = RGB(12, 7, 4);
    next_id = 0;
    ok_r3d_begin(frame, CHARCOAL);
    ok_r3d_camera(ok_v3_make(0, U(2100), U(4600)), ok_v3_make(0, U(650), 0), 340);
    apply(l);
    /* Floor planks, wallpaper below the eaves, plaster above, a patch of bare brick. */
    tex(OK_ROOM_PLANKS, 4, wood, 0);
    box(-1400, -100, -1100, 1400, 0, 1300);
    tex(OK_ROOM_WALLPAPER, 5, RGB(20, 22, 17), 0);
    ok_r3d_quad(P(-1400, 0, -1100), P(1400, 0, -1100), P(1400, 700, -1100), P(-1400, 700, -1100));
    tex(OK_ROOM_PLASTER, 5, RGB(24, 21, 17), 0);
    ok_r3d_quad(P(-1400, 700, -1100), P(1400, 700, -1100), P(0, 2100, -1100), P(0, 2100, -1100));
    tex(OK_ROOM_BRICK, 7, RGB(18, 8, 6), 0);
    ok_r3d_quad(P(380, 120, -1094), P(1150, 120, -1094), P(1250, 560, -1094), P(520, 640, -1094));
    tex(OK_ROOM_BOARDS, 6, RGB(16, 11, 7), OK_R3D_BOTH);
    ok_r3d_quad(P(-1400, 0, -1100), P(-1400, 700, -1100), P(-1400, 700, 1300), P(-1400, 0, 1300));
    ok_r3d_quad(P(1400, 0, -1100), P(1400, 700, -1100), P(1400, 700, 1300), P(1400, 0, 1300));
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
    /* The player's drawing, framed above the bed. */
    if (drawing) {
        int x, y;
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) art[y * 64 + x] = ok_drawing_get(drawing, (unsigned)x, (unsigned)y) ? RGB(5, 4, 4) : RGB(30, 29, 26);
        tex(OK_ROOM_BEAM, 8, RGB(14, 9, 5), OK_R3D_EDGE);
        box(-1060, 330, -1100, -640, 750, -1070);
        ok_r3d_texture(art, 64);
        ok_r3d_uv(0, 0, 64, 64);
        ok_r3d_tile(0);
        ok_r3d_material(RGB(30, 29, 26), ++next_id, OK_R3D_TEXTURED);
        ok_r3d_quad(P(-1030, 360, -1065), P(-670, 360, -1065), P(-670, 720, -1065), P(-1030, 720, -1065));
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
    blob(-1130, 245, -640, 150, 52, 140);
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
    ok_r3d_cylinder(P(1180, 0, 300), U(110), U(150), 10);
    tex(OK_ROOM_LEAVES, 0, RGB(4, 12, 5), OK_R3D_EDGE);
    blob(1180, 260, 300, 120, 110, 120);
    blob(1120, 390, 280, 70, 110, 70);
    blob(1250, 360, 320, 70, 90, 70);
    /* A pile of books by the plant, a floor cushion by the rug. */
    mat(RGB(9, 14, 22), OK_R3D_EDGE);
    box(-1300, 0, 150, -1020, 50, 330);
    mat(RGB(26, 20, 8), OK_R3D_EDGE);
    box(-1280, 50, 160, -1050, 95, 320);
    mat(RGB(22, 9, 8), OK_R3D_EDGE);
    box(-1290, 95, 170, -1060, 130, 315);
    tex(OK_ROOM_KNIT, 0, RGB(5, 8, 16), OK_R3D_EDGE);
    blob(900, 70, 760, 220, 75, 180);
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
        beam(frame, kept.beam_x, kept.beam_y, l->day ? 2 : 1);
    }
    for (i = 0; i < 6; i++) {
        ok_r3d_project(P(-150 + i * 61 % 300, 1100 + (i * 137) % 300, -1080), &kept.stars[i][0], &kept.stars[i][1], NULL);
    }
    kept.night = l->night;
    kept.day = l->day && l->sun >= 0;
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

void ok_house_draw(uint16_t *frame, int hour, int minute, int lights_off, const uint8_t *drawing, const ok_look *look,
                   const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face)
{
    int m = (hour % 24) * 60 + minute % 60, key = m / 5 * 2 + (lights_off != 0), i;
    uint32_t s = sum(drawing), t = a->ticks;
    light l;
    light_at(m, lights_off, &l);
    if (!kept.valid || kept.key != key || kept.art_sum != s) {
        room(frame, &l, lights_off, drawing);
        kept.valid = 1;
        kept.key = key;
        kept.art_sum = s;
    }
    ok_r3d_restore(frame);
    ok_r3d_camera(ok_v3_make(0, U(2100), U(4600)), ok_v3_make(0, U(650), 0), 340);
    apply(&l);
    ok_monkey_draw(look, skin, a, face);
    ok_r3d_finish();
    ok_monkey_face_draw(frame, face);
    /* Small living things. */
    if (kept.night)
        for (i = 0; i < 6; i++)
            if ((t / 20 + (uint32_t)i * 7) % 11) dot(frame, kept.stars[i][0], kept.stars[i][1], RGB(28, 28, 22));
    if (kept.night && !kept.lights_off && ok_r3d_id_at(kept.candle_x, kept.candle_y + 2) != 0) {
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
