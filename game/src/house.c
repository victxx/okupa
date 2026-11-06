#include "okupa_house.h"
#include "okupa_act.h"
#include <string.h>

#define W OK_R3D_W
#define H OK_R3D_H
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define CHARCOAL RGB(2, 2, 3) /* nearly black around the attic: it floats like a diorama */
#define DEG(d) ((d) * OK_TURN / 360)
#define FLATTEN 150 /* the painted textures, toned down to a hint over flat colour */
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
    ok_r3d_ellipsoid(P(x, y, z), P(rx, ry, rz), 6, 4);
}

/* A quad cut into nu x nv smaller ones (corners counterclockwise from the
 * front): textures stay straight in perspective, small triangles warp little. */
static void quad_grid(ok_v3 a, ok_v3 b, ok_v3 c, ok_v3 d, int nu, int nv)
{
    int i, j;
    if (ok_r3d_on_hardware()) nu = nv = 1; /* the hardware keeps textures straight by itself */
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
    ok_r3d_capsule(P(x0, y0, z0), P(x1, y1, z1), U(r), 4);
}

uint32_t (*ok_house_clock)(void);
void (*ok_house_sky_layer)(int cam_az, int pitch, int focal, int mood, int next, int k);
uint32_t ok_house_prof[OK_HOUSE_PROF];
static uint32_t prof_at;
static void prof(int k)
{
    uint32_t t;
    if (!ok_house_clock) return;
    t = ok_house_clock();
    if (k >= 0) ok_house_prof[k] = (ok_house_prof[k] * 7 + (t - prof_at)) / 8;
    prof_at = t;
}

static int next_id;
static int place; /* OK_PLACE_* */

static void mat(uint16_t c, int flags)
{
    ok_r3d_texture(NULL, 0);
    ok_r3d_flatten(0);
    ok_r3d_material(c, ++next_id, flags);
}

/* The same, from the outside's skin (game/src/outside_skin.c). */
static void otex(int tile, int per_unit, uint16_t c, int flags)
{
    ok_r3d_texture(ok_outside_skin, 128);
    ok_r3d_flatten(FLATTEN);
    ok_r3d_uv(tile % 4 * 32, tile / 4 * 32, 32, 32);
    ok_r3d_tile(per_unit);
    ok_r3d_material(c, ++next_id, flags | OK_R3D_TEXTURED);
}

/* A textured material: one tile of the attic's skin, repeated `per_unit`
 * texels to a unit (0: wrapped once round a curved shape). c is its outline. */
static void tex(int tile, int per_unit, uint16_t c, int flags)
{
    ok_r3d_texture(ok_room_skin, 128);
    ok_r3d_flatten(FLATTEN);
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
    room->door_x = 1150, room->door_z = -20;       /* inside the door in the right wall */
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
    {150, 545, 150, 3, 1},   {-485, 1235, 105, 2, 2}, {485, 1235, 105, -3, 0},
};

void ok_house_frame(int slot, const uint8_t *pixels)
{
    if (slot < 0 || slot >= OK_HOUSE_FRAMES) return;
    hung[slot] = pixels != NULL;
    if (pixels) memcpy(gallery[slot], pixels, OK_DRAWING_BYTES);
}

/* The frame that holds the okupa's own drawing (-1: none). */
static int own_slot = -1;
void ok_house_own_frame(int slot)
{
    own_slot = slot >= 0 && slot < OK_HOUSE_FRAMES ? slot : -1;
}

/* Something the okupa's mind made, on the floor of one place: boxes on a
 * 16-cube grid, coloured from a palette of sixteen. */
static uint8_t thing[16][7];
static int thing_n, thing_place = -1;
static int32_t thing_x, thing_z;
void ok_house_object(const uint8_t (*boxes)[7], int n, int where, int32_t x, int32_t z)
{
    int i;
    thing_n = n < 0 ? 0 : n > 16 ? 16 : n;
    for (i = 0; i < thing_n; i++) memcpy(thing[i], boxes[i], 7);
    thing_place = where;
    thing_x = x;
    thing_z = z;
}

static void draw_thing(int where)
{
    static const uint16_t palette[16] = {
        RGB(3, 3, 4),    RGB(28, 28, 27), RGB(26, 6, 6),  RGB(31, 18, 4), RGB(31, 28, 6), RGB(8, 22, 8),
        RGB(4, 12, 6),   RGB(6, 14, 28),  RGB(4, 6, 16),  RGB(20, 8, 24), RGB(30, 16, 20), RGB(19, 12, 7),
        RGB(12, 7, 4),   RGB(16, 16, 17), RGB(10, 10, 11), RGB(24, 20, 14),
    };
    int i;
    if (thing_place != where || !thing_n) return;
    for (i = 0; i < thing_n; i++) {
        const uint8_t *b = thing[i];
        int x0 = (b[0] & 15) * 20 - 160, y0 = (b[1] & 15) * 20, z0 = (b[2] & 15) * 20 - 160;
        int w = (b[3] ? b[3] : 1) * 20, h = (b[4] ? b[4] : 1) * 20, d = (b[5] ? b[5] : 1) * 20;
        mat(palette[b[6] & 15], OK_R3D_EDGE);
        box(thing_x + x0, y0, thing_z + z0, thing_x + x0 + w, y0 + h, thing_z + z0 + d);
    }
}

/* Where each thing it may look at is, in the place it is in. */
void ok_house_gaze_point(int gaze, int32_t out[3])
{
    static const int32_t attic[OK_GAZES][3] = {{0, 2100, 4600}, {0, 1250, -1095}, {1384, 420, -20}, {-500, 560, -1096},
                                               {0, 0, 0},       {0, 1900, -1000}};
    static const int32_t garden[OK_GAZES][3] = {{0, 1150, -5600}, {0, 900, 300},     {1150, 420, 700},
                                                {0, 560, -1500},  {0, 0, 0},         {0, 3200, -3000}};
    const int32_t *p = (place == OK_PLACE_GARDEN ? garden : attic)[gaze < 0 || gaze >= OK_GAZES ? 0 : gaze];
    out[0] = p[0], out[1] = p[1], out[2] = p[2];
}

/* The frame being hung right now, and how far into it (ticks). */
static int hang_slot = -1, hang_t;
void ok_house_hanging(int slot, int t)
{
    hang_slot = slot >= 0 && slot < 6 ? slot : -1;
    hang_t = t;
}

/* One frame: wood, a cream mount, and the drawing in ink, or nothing yet
 * (pixels NULL): an empty frame waiting for its picture. */
static uint32_t sum(const uint8_t *d);

static void framed_at(int slot, const uint8_t *pixels, int fx, int fy, int fz, int h, int tilt, int wood, int yaw, int lean)
{
    static const uint16_t woods[4] = {RGB(14, 9, 5), RGB(21, 14, 7), RGB(28, 26, 22), RGB(29, 22, 4)};
    if (slot == own_slot) wood = 3; /* the okupa's own picture: a gilt frame */
    static uint32_t made[OK_HOUSE_FRAMES];
    int x, y, m = h * 82 / 100, d = h * 70 / 100;
    if (pixels && made[slot] != sum(pixels)) { /* the picture as texels, once per picture */
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++)
                art[slot][y * 64 + x] = ok_drawing_get(pixels, (unsigned)x, (unsigned)y) ? RGB(5, 4, 4) : RGB(30, 29, 26);
        made[slot] = sum(pixels);
        ok_r3d_texture_changed(art[slot]);
    }
    ok_r3d_push();
    ok_r3d_translate(P(fx, fy, fz));
    ok_r3d_rotate_y(DEG(yaw));
    ok_r3d_rotate_x(-DEG(lean));
    ok_r3d_rotate_z(DEG(tilt));
    tex(OK_ROOM_BEAM, 8, woods[wood], OK_R3D_EDGE); /* the frame */
    box(-h, -h, 0, h, h, 26);
    mat(RGB(29, 27, 23), 0); /* the mount */
    ok_r3d_quad(P(-m, -m, 36), P(m, -m, 36), P(m, m, 36), P(-m, m, 36)); /* layers well apart: the hardware's depth is coarse */
    if (pixels) {
        ok_r3d_flatten(0);
        ok_r3d_texture(art[slot], 64);
        ok_r3d_uv(0, 0, 64, 64);
        ok_r3d_tile(0);
        ok_r3d_material(RGB(30, 29, 26), ++next_id, OK_R3D_TEXTURED);
    } else {
        mat(RGB(25, 23, 19), 0); /* empty: a bare, slightly shadowed backing */
    }
    ok_r3d_quad(P(-d, -d, 46), P(d, -d, 46), P(d, d, 46), P(-d, d, 46));
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
        ok_r3d_ellipsoid(at, P(28, 36, 28), 4, 2);
        if (on && *count < 24 && ok_r3d_project(at, &out[*count][0], &out[*count][1], NULL)) (*count)++;
    }
}

/* Adds warm light around a point, fading out by radius r. */
static void glow(uint16_t *f, int cx, int cy, int r, int rr, int gg, int bb)
{
    if (ok_r3d_on_hardware()) return; /* over the hardware's 3D there is nothing to brighten */
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
    if (ok_r3d_on_hardware()) return;
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

/* A plant as two pictures crossed in an X (the old trick for leaves): four
 * triangles instead of a few hundred. which: 0 a houseplant, 1 a bush. */
static void sprite_x(int x, int y, int z, int w, int h, int which)
{
    ok_r3d_flatten(0);
    ok_r3d_tile(0);
    ok_r3d_texture(ok_plant_art, 64);
    ok_r3d_uv(which * 32, 0, 32, 32);
    ok_r3d_material(RGB(2, 6, 2), ++next_id, OK_R3D_TEXTURED | OK_R3D_CUTOUT | OK_R3D_BOTH);
    ok_r3d_quad(P(x - w / 2, y, z), P(x + w / 2, y, z), P(x + w / 2, y + h, z), P(x - w / 2, y + h, z));
    ok_r3d_quad(P(x, y, z + w / 2), P(x, y, z - w / 2), P(x, y + h, z - w / 2), P(x, y + h, z + w / 2));
}

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
    quad_grid(P(-1400, 0, -1100), P(1400, 0, -1100), P(1400, 950, -1100), P(-1400, 950, -1100), 14, 5);
    tex(OK_ROOM_PLASTER, 5, RGB(24, 21, 17), 0);
    quad_grid(P(-1400, 950, -1100), P(1400, 950, -1100), P(0, 2100, -1100), P(0, 2100, -1100), 8, 6);
    tex(OK_ROOM_BRICK, 7, RGB(18, 8, 6), 0);
    quad_grid(P(380, 120, -1094), P(1150, 120, -1094), P(1250, 560, -1094), P(520, 640, -1094), 4, 3);
    tex(OK_ROOM_BOARDS, 6, RGB(16, 11, 7), OK_R3D_BOTH);
    quad_grid(P(-1400, 0, -1100), P(-1400, 950, -1100), P(-1400, 950, 1300), P(-1400, 0, 1300), 4, 12);
    quad_grid(P(1400, 0, -1100), P(1400, 950, -1100), P(1400, 950, 1300), P(1400, 0, 1300), 4, 12);
    /* The door in the right kneewall (out to the terrace) and a small window in the left one. */
    tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), OK_R3D_EDGE); /* a plain rectangular door, frame and lintel */
    box(1360, 0, -340, 1400, 820, -300);
    box(1360, 0, 260, 1400, 820, 300);
    box(1360, 780, -340, 1400, 840, 300);
    tex(OK_ROOM_CRATE, 8, RGB(10, 6, 3), OK_R3D_EDGE);
    box(1370, 0, -300, 1398, 780, 260);
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
            ok_r3d_translate(P(s * 700, 1525, z)); /* rafters, from the kneewall's top to the ridge */
            ok_r3d_rotate_z(-s * (OK_TURN * 395 / 3600));
            box(-930, -50, -40, 930, 50, 40);
            ok_r3d_pop();
        }
    }
    box(-1400, 910, -1090, 1400, 990, -1020);
    /* The round window: sky by the hour, the sun crossing it by day, the moon at night;
     * its layers well apart, for the hardware's coarser depth. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 1250, -1095));
    ok_r3d_rotate_x(OK_TURN / 4);
    tex(OK_ROOM_FRAME, 0, RGB(22, 21, 19), 0);
    ok_r3d_disc(P(0, 0, 0), U(340), U(340), 16);
    mat(l->sky, OK_R3D_UNLIT);
    ok_r3d_disc(P(0, 12, 0), U(280), U(280), 16);
    if (l->sun >= 0) {
        int sx = -230 + 460 * l->sun / 1000, sz = 60 - (l->sun - 500) * (l->sun - 500) / 2500;
        mat(RGB(31, 30, 18), OK_R3D_UNLIT);
        ok_r3d_disc(P(sx, 20, -sz), U(70), U(70), 10);
    } else if (l->night) {
        mat(RGB(29, 29, 24), OK_R3D_UNLIT);
        ok_r3d_disc(P(-110, 20, -90), U(70), U(70), 10);
        mat(l->sky, OK_R3D_UNLIT);
        ok_r3d_disc(P(-85, 26, -105), U(58), U(58), 10);
    }
    tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), 0);
    box(-280, 30, -22, 280, 46, 22);
    box(-22, 30, -280, 22, 46, 280);
    ok_r3d_pop();
    bulbs(-1300, 960, 0, 1950, -980, 9, on, lit, &lights);
    bulbs(0, 1950, 1300, 960, -980, 9, on, lit, &lights);
    /* The gallery: six frames, empty until a picture is hung in them. */
    {
        int k;
        for (k = 0; k < 6; k++) {
            if (k == hang_slot) { /* on its way: only the nail waits for it */
                mat(RGB(6, 5, 4), 0);
                ok_r3d_ellipsoid(P(spots[k][0], spots[k][1] + spots[k][2] * 3 / 2, -1082), P(9, 9, 14), 4, 3);
                continue;
            }
            framed(k, hung[k] ? gallery[k] : k == 0 ? drawing : NULL);
        }
    }
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
    sprite_x(-1150, 140, 600, 420, 420, 0);
    tex(OK_ROOM_CLAY, 8, RGB(18, 8, 4), OK_R3D_EDGE);
    ok_r3d_cylinder(P(1180, 0, 700), U(110), U(150), 10);
    sprite_x(1180, 130, 700, 400, 400, 0);
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

static void outside_scene(const light *l, int on);
static void sky(uint16_t *f, ok_v3 eye, ok_v3 target, int focal, int minute);
static void fireflies(uint16_t *f, uint32_t t, int night);
static void fog(uint16_t *f, uint16_t haze, int near_units, int far_units);

/* The terrace, seen from the garden: kept like the attic, the okupa on top. */
static void garden_draw(uint16_t *frame, int m, const ok_look *look, const uint16_t *skin, const ok_monkey_anim *a,
                        ok_monkey_face *face)
{
    static const int cam[6] = {0, 1150, -5600, 0, 380, -1500};
    uint32_t s = 0;
    int i, key = m / 5 * 4 + 3;
    light l;
    light_at(m, 0, &l);
    for (i = 6; i < OK_HOUSE_FRAMES; i++) s = s * 7 + (hung[i] ? sum(gallery[i]) + 1 : 0);
    if (!ok_r3d_retains() || !kept.valid || kept.key != key || kept.art_sum != s) {
        next_id = 0;
        ok_r3d_begin(frame, CHARCOAL);
        ok_r3d_camera(P(cam[0], cam[1], cam[2]), P(cam[3], cam[4], cam[5]), 330);
        apply(&l);
        sky(frame, P(cam[0], cam[1], cam[2]), P(cam[3], cam[4], cam[5]), 330, m);
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
    draw_thing(OK_PLACE_GARDEN);
    ok_monkey_draw(look, skin, a, face);
    ok_r3d_finish();
    ok_monkey_face_draw(frame, face);
    fireflies(frame, a->ticks, l.night || m >= 1140);
}

/* A new picture being hung: it drops onto its nail, bumps, swings on its
 * string and settles; a puff of dust, then a few glints. */
#define HANG_DROP 22
static void hang_frame(void)
{
    const int16_t *sp = spots[hang_slot];
    int h = sp[2], t = hang_t, drop = 0, saved = next_id;
    int32_t swing; /* hundredths of a degree */
    if (t < HANG_DROP) {
        drop = 1300 * (HANG_DROP * HANG_DROP - t * t) / (HANG_DROP * HANG_DROP);
        swing = (int32_t)ok_r3d_sin(t * 160) * 300 / 4096;
    } else {
        int k = t - HANG_DROP, left = k < 120 ? 120 - k : 0;
        if (k < 12) drop = (int)((int64_t)ok_r3d_sin(k * OK_TURN / 24) * 40 / 4096);
        swing = (int32_t)((int64_t)ok_r3d_sin(k * OK_TURN / 38 + OK_TURN / 4) * 1100 / 4096 * left * left / (120 * 120));
    }
    next_id = 92;
    ok_r3d_push();
    ok_r3d_translate(P(sp[0], sp[1] + h * 3 / 2 + drop, -1096));
    ok_r3d_rotate_z(swing * OK_TURN / 36000);
    framed_at(hang_slot, gallery[hang_slot], 0, -h * 3 / 2, 0, h, sp[3], sp[4], 0, 0);
    ok_r3d_pop();
    next_id = saved;
}

static void hang_sparkle(uint16_t *frame)
{
    const int16_t *sp = spots[hang_slot];
    int h = sp[2], k = hang_t - HANG_DROP, i;
    if (k >= 0 && k < 26) /* dust shaken off the wall */
        for (i = 0; i < 8; i++) {
            int x, y, side = i & 1 ? 1 : -1;
            if (!ok_r3d_project(P(sp[0] + side * (h - 10), sp[1] - h, -1050), &x, &y, NULL)) continue;
            x += side * (k * (2 + i % 3) / 6 + i / 2);
            y += k * k / 60 + (i / 2) - k / 4;
            if (ok_r3d_id_at(x, y) < 100) dot(frame, x, y, RGB(26, 23, 18));
        }
    if (k >= 40 && k < 150) /* glints at the corners, one after another */
        for (i = 0; i < 4; i++) {
            int x, y, on = (k - 40 - i * 12) / 3, r;
            if (on < 0 || on > 8) continue;
            r = on < 4 ? on : 8 - on;
            if (!ok_r3d_project(P(sp[0] + (i & 1 ? h : -h), sp[1] + (i & 2 ? -h : h), -1050), &x, &y, NULL)) continue;
            for (; r >= 0; r--) {
                uint16_t c = r ? RGB(31, 29, 18) : RGB(31, 31, 30);
                dot(frame, x + r, y, c);
                dot(frame, x - r, y, c);
                dot(frame, x, y + r, c);
                dot(frame, x, y - r, c);
            }
        }
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
        s = s * 7 + (uint32_t)(hang_slot + 1);
        light l;
        light_at(m, lights_off, &l);
        if (!ok_r3d_retains() || !kept.valid || kept.key != key || kept.art_sum != s) {
            room(frame, &l, lights_off, drawing, mask_on_floor);
            kept.valid = 1;
            kept.key = key;
            kept.art_sum = s;
        }
        prof(-1);
        ok_r3d_restore(frame);
        prof(5);
        ok_r3d_camera(ok_v3_make(0, U(2100), U(4600)), ok_v3_make(0, U(650), 0), 340);
        apply(&l);
        if (hang_slot >= 0) hang_frame();
        draw_thing(OK_PLACE_ATTIC);
        ok_monkey_draw(look, skin, a, face);
        prof(6);
        /* In bed, the knitted blanket over it, up to the shoulders. */
        if (a->s[OK_SPRING_LIE].x / 256 > 55) {
            tex(OK_ROOM_KNIT, 0, RGB(5, 8, 16), OK_R3D_EDGE);
            ok_r3d_ellipsoid(P(a->room.bed_x + 90, 318 + (int)((int64_t)ok_r3d_sin((int)(a->ticks * 10u)) * 6 / 4096), a->room.bed_z + 20),
                             P(300, 132, 250), 12, 7);
            ok_r3d_tile(0);
        }
        ok_r3d_finish();
        prof(7);
        if (kept.sun) beam(frame, kept.beam_x, kept.beam_y, kept.beam_add, 1); /* standing in the sun, it is lit too */
        ok_monkey_face_draw(frame, face);
        if (hang_slot >= 0) hang_sparkle(frame);
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

/* The sky: four painted 360 degree panoramas (dawn, day, dusk, night, made
 * with ChatGPT, game/src/sky_art.c) laid behind everything by where the
 * camera looks: left and right turn round the panorama, up and down slide
 * it. Between two moods the two are blended. One lookup a pixel, no
 * triangles: the clouds, sun, moon and stars are in the pictures. */
#define SKY_W 512
#define SKY_H 256
#define SKY_TOP 80 /* degrees above the horizon at the panorama's top row */
#define SKY_SPAN 120 /* degrees from its top row to its bottom one */

static const struct {
    int minute, a, b, k; /* moods a and b, k/256 of b */
} sky_moods[] = {
    {0, OK_SKY_NIGHT, OK_SKY_NIGHT, 0},   {330, OK_SKY_NIGHT, OK_SKY_NIGHT, 0}, {420, OK_SKY_DAWN, OK_SKY_DAWN, 0},
    {450, OK_SKY_DAWN, OK_SKY_DAWN, 0},   {540, OK_SKY_DAY, OK_SKY_DAY, 0},     {1050, OK_SKY_DAY, OK_SKY_DAY, 0},
    {1140, OK_SKY_DUSK, OK_SKY_DUSK, 0},  {1180, OK_SKY_DUSK, OK_SKY_DUSK, 0}, {1250, OK_SKY_NIGHT, OK_SKY_NIGHT, 0},
    {1440, OK_SKY_NIGHT, OK_SKY_NIGHT, 0},
};

static uint32_t hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ h >> 15;
}

static uint32_t isqrt_h(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    for (; bit; bit >>= 2) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return (uint32_t)r;
}

static void sky(uint16_t *f, ok_v3 eye, ok_v3 target, int focal, int minute)
{
    static uint16_t col[W];
    int i = 0, x, y, a, b, k, n = (int)(sizeof sky_moods / sizeof sky_moods[0]);
    int64_t dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
    int cam_az = ok_r3d_atan2((int32_t)(dx >> 4), (int32_t)(dz >> 4)), flat = (int)isqrt_h((uint64_t)(dx * dx + dz * dz) >> 8);
    int pitch = ok_r3d_atan2((int32_t)(dy >> 4), flat); /* in 1/4096 turn, from the horizon */
    const uint8_t *pa, *pb;
    const uint16_t *ca, *cb;
    while (i + 2 < n && sky_moods[i + 1].minute <= minute) i++;
    a = sky_moods[i].a;
    b = sky_moods[i + 1].a;
    k = a == b ? 0 : (minute - sky_moods[i].minute) * 256 / (sky_moods[i + 1].minute - sky_moods[i].minute);
    if (ok_house_sky_layer) { /* the platform shows it as a background layer behind the 3D */
        ok_house_sky_layer(cam_az, pitch, focal, a, b, k);
        ok_r3d_see_through();
        return;
    }
    pa = ok_sky_art[a];
    pb = ok_sky_art[b];
    ca = ok_sky_palette[a];
    cb = ok_sky_palette[b];
    /* Which panorama column each screen column sees (the panorama's x runs
     * the same way as the turn), and which row each screen row sees. */
    for (x = 0; x < W; x++) {
        int az = cam_az + ok_r3d_atan2(x - W / 2, focal) + OK_TURN / 4; /* the mirror's seams to the sides, where no camera looks */
        col[x] = (uint16_t)(((az & (OK_TURN - 1)) * SKY_W / OK_TURN) % SKY_W);
    }
    for (y = 0; y < H; y++) {
        int el = (pitch + ok_r3d_atan2(H / 2 - y, focal)) & (OK_TURN - 1), row;
        if (el > OK_TURN / 2) el -= OK_TURN; /* -half a turn .. half a turn */
        row = (SKY_TOP * OK_TURN / 360 - el) * SKY_H / (SKY_SPAN * OK_TURN / 360);
        row = row < 0 ? 0 : row >= SKY_H ? SKY_H - 1 : row;
        {
            const uint8_t *ra = pa + row * SKY_W, *rb = pb + row * SKY_W;
            uint16_t *out = f + y * W;
            if (!k) {
                for (x = 0; x < W; x++) out[x] = ca[ra[col[x]]];
            } else {
                for (x = 0; x < W; x++) {
                    uint16_t c = ca[ra[col[x]]], d = cb[rb[col[x]]];
                    out[x] = (uint16_t)(0x8000u | (((c & 31) * (256 - k) + (d & 31) * k) >> 8) |
                                        ((((c >> 5 & 31) * (256 - k) + (d >> 5 & 31) * k) >> 8) << 5) |
                                        ((((c >> 10 & 31) * (256 - k) + (d >> 10 & 31) * k) >> 8) << 10));
                }
            }
        }
    }
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
    /* The canvas on the ledge, in front of the legs: in the easel's own
     * frame, so it faces the way the easel does. */
    framed_at(slot, hung[slot] ? gallery[slot] : NULL, 0, 520, 70, 150, 0, slot % 3, 0, 12);
    ok_r3d_pop();
}

/* A little temple for art on the terrace: a stone platform, four columns,
 * a gabled roof, and a back wall inside it with three frames (6-8). */
static void temple(void)
{
    static const int16_t pediment[3][2] = {{-820, 0}, {820, 0}, {0, 380}};
    int i, k;
    ok_r3d_push();
    ok_r3d_translate(P(0, 0, -1500));
    ok_r3d_rotate_y(OK_TURN / 2); /* it faces the back of the island, away from the house */
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
    for (i = 0; i < 3; i++) framed_at(6 + i, hung[6 + i] ? gallery[6 + i] : NULL, -440 + i * 440, 560 + (i % 2) * 30, -245, 150, (i - 1) * 2, i, 0, 0);
    ok_r3d_pop();
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

/* The island's outline: a wobbly circle, never closer than the terrace's
 * corners (radius in hundredths at turn angle a, for an island of size 1000). */
static int island_r(int a, int seed)
{
    static const int16_t ph[3][3] = {{2966, 1369, 228}, {700, 2900, 1800}, {3500, 400, 2600}};
    return 2900 + (int)(((int64_t)ok_r3d_sin(2 * a + ph[seed][0]) * 260 + (int64_t)ok_r3d_sin(3 * a + ph[seed][1]) * 170 +
                         (int64_t)ok_r3d_sin(5 * a + ph[seed][2]) * 100 + (int64_t)ok_r3d_sin(7 * a + ph[seed][1] * 3) * 60 +
                         (int64_t)ok_r3d_sin(11 * a + ph[seed][0] * 5) * 35) / 4096);
}

/* An island: a flat lawn, a lip that rounds over, then rocky cliffs
 * narrowing to a point; size 1000 is the one the house stands on. */
static void island(int cx, int cy, int cz, int size, int seed, int segs)
{
    /* Rings from the lawn's edge down: radius (per 1000 of the outline) and height. */
    static const int16_t ring[][2] = {{1000, 0}, {1028, -40}, {1040, -110}, {1026, -190}, {975, -270}, {840, -700},
                                      {620, -1350}, {360, -2000}, {120, -2650}, {0, -2950}};
    enum { RINGS = sizeof ring / sizeof ring[0] };
    int i, k;
    for (k = 0; k < RINGS - 1; k++) {
        if (segs < 20 && k >= 1 && k <= 3) continue; /* small ones: no rounded lip */
        if (k == 0) otex(OK_OUT_GRASS, 3, RGB(5, 10, 3), 0);
        if (k == 3) {
            ok_r3d_flatten(210);
            ok_r3d_texture(ok_outside_skin, 128);
            ok_r3d_uv(OK_OUT_EARTH % 4 * 32, OK_OUT_EARTH / 4 * 32, 32, 32);
            ok_r3d_tile(3);
            ok_r3d_material(RGB(11, 7, 3), ++next_id, OK_R3D_TEXTURED | OK_R3D_EDGE | OK_R3D_FLAT);
        }
        for (i = 0; i < segs; i++) {
            ok_v3 q[4];
            int n;
            for (n = 0; n < 4; n++) {
                int ii = (i + (n == 1 || n == 2)) % segs, kk = (segs < 20 && k == 0 ? 4 : k + (n >= 2)), a = ii * OK_TURN / segs;
                if (segs < 20 && k == 0 && n < 2) kk = 0;
                int r = island_r(a, seed) * ring[kk][0] / 1000, y = ring[kk][1], ox = 0, oz = 0;
                if (kk >= 1 && kk <= 4) y += (int)(hash2(ii + seed * 30, kk) % 50) - 25; /* the lip dips and swells */
                if (kk >= 5) { /* below, a rock: lumpy, and leaning off to one side as it narrows */
                    if (kk < RINGS - 1) r = r * (86 + (int)(hash2(ii + seed * 50, kk) % 29)) / 100;
                    ox = (int)((int64_t)ok_r3d_sin(kk * 700 + seed * 1300) * 60 * (kk - 4) / 4096);
                    oz = (int)((int64_t)ok_r3d_cos(kk * 500 + seed * 900) * 50 * (kk - 4) / 4096);
                }
                q[n] = P(cx + ((int)((int64_t)ok_r3d_cos(a) * r / 4096) + ox) * size / 1000, cy + y * size / 1000,
                         cz + ((int)((int64_t)ok_r3d_sin(a) * r / 4096) + oz) * size / 1000);
            }
            ok_r3d_quad(q[0], q[1], q[2], q[3]);
        }
    }
    otex(OK_OUT_GRASS, 3, RGB(5, 10, 3), 0);
    /* The lawn on top in rings, not one fan of long wedges: big thin
     * triangles bend the texture and the depth, and the grass looked like
     * the face of a clock. */
    {
        static const int16_t rr[] = {0, 300, 600, 1000}, rs[] = {0, 250, 500, 750, 1000};
        const int16_t *rad = ok_r3d_on_hardware() ? rr : rs;
        int rings = ok_r3d_on_hardware() ? 3 : 4;
        for (i = 0; i < segs; i++) {
            int a0 = i * OK_TURN / segs, a1 = (i + 1) * OK_TURN / segs, r0 = island_r(a0, seed), r1 = island_r(a1, seed);
            int x0 = (int)((int64_t)ok_r3d_cos(a0) * r0 / 4096), z0 = (int)((int64_t)ok_r3d_sin(a0) * r0 / 4096);
            int x1 = (int)((int64_t)ok_r3d_cos(a1) * r1 / 4096), z1 = (int)((int64_t)ok_r3d_sin(a1) * r1 / 4096);
            for (k = 0; k < rings; k++) {
                int f0 = rad[k], f1 = rad[k + 1];
                ok_v3 in0 = P(cx + x0 * f0 / 1000 * size / 1000, cy, cz + z0 * f0 / 1000 * size / 1000);
                ok_v3 in1 = P(cx + x1 * f0 / 1000 * size / 1000, cy, cz + z1 * f0 / 1000 * size / 1000);
                ok_v3 out0 = P(cx + x0 * f1 / 1000 * size / 1000, cy, cz + z0 * f1 / 1000 * size / 1000);
                ok_v3 out1 = P(cx + x1 * f1 / 1000 * size / 1000, cy, cz + z1 * f1 / 1000 * size / 1000);
                if (!f0) ok_r3d_quad(in0, out1, out0, out0);
                else ok_r3d_quad(in0, in1, out1, out0);
            }
        }
    }
    ok_r3d_tile(0);
}

/* Ivy hanging over an island's lip: n strands at the outline's edge,
 * each a see-through sprite on a quad facing out. */
static void ivy(int cx, int cy, int cz, int size, int seed, int n)
{
    int v;
    ok_r3d_flatten(0);
    ok_r3d_tile(0);
    ok_r3d_texture(ok_ivy_art, 6 * 32);
    for (v = 0; v < n; v++) {
        int a = v * OK_TURN / n + (int)(hash2(v, seed + 40) % (uint32_t)(OK_TURN / n / 2)), sprite = (int)(hash2(v, seed + 41) % 6);
        int r = island_r(a, seed) * 1046 / 1000, len = (700 + (int)(hash2(v, seed + 42) % 700)) * size / 1000, w = len * 3 / 4;
        int32_t ca = ok_r3d_cos(a), sa = ok_r3d_sin(a);
        int px = cx + (int)((int64_t)ca * r / 4096) * size / 1000, pz = cz + (int)((int64_t)sa * r / 4096) * size / 1000, py = cy - 50 * size / 1000;
        int tx = (int)((int64_t)-sa * w / 2 / 4096), tz = (int)((int64_t)ca * w / 2 / 4096);
        ok_r3d_uv(sprite * 32, 0, 32, 64);
        ok_r3d_material(RGB(2, 6, 2), ++next_id, OK_R3D_TEXTURED | OK_R3D_CUTOUT | OK_R3D_BOTH);
        ok_r3d_quad(P(px + tx, py - len, pz + tz), P(px - tx, py - len, pz - tz), P(px - tx, py, pz - tz), P(px + tx, py, pz + tz));
    }
}

/* A small bush: a crossed picture. */
static void bush(int x, int z, int s, int seed)
{
    (void)seed;
    sprite_x(x, 0, z, 460 * s / 100, 400 * s / 100, 1);
}

static void outside_scene(const light *l, int on)
{
    int i, k, lit[24][2], lights = 0;
    /* The island and two little islets, each an irregular low-poly rock. */
    prof(-1);
    island(0, 0, 0, 1000, 0, 22);
    island(-2600, -600, 3300, 105, 1, 12);
    island(3900, -900, -1500, 90, 2, 12);
    ivy(0, 0, 0, 1000, 0, 18);
    ivy(-2600, -600, 3300, 105, 1, 4);
    ivy(3900, -900, -1500, 90, 2, 4);
    prof(5); /* islands and ivy */
    /* The house: the attic itself, seen from outside. Built in the attic's
     * own coordinates (walls at x +-14, the round window in the gable at
     * z -11, the door in the right kneewall), mirrored in z so the round
     * window faces the front, and shrunk to fit the island. A stone footing,
     * a timber frame, mossy tiles, ivy: a cottage from the country. */
    {
        static const int16_t gable[5][2] = {{-1420, 0}, {1420, 0}, {1420, 950}, {0, 2120}, {-1420, 950}};
        ok_r3d_push();
        ok_r3d_translate(P(0, 0, 700)); /* in front, the temple behind it */
        ok_r3d_scale(U(62), U(62), -U(62));
        otex(OK_OUT_STUCCO, 4, RGB(22, 19, 15), OK_R3D_EDGE);
        box(-1420, 0, -1100, -1380, 950, 1300); /* kneewalls */
        box(1380, 0, -1100, 1420, 950, 1300);
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
            box(-1440, 920, z - 12, 1440, 980, z + 12);
            box(-40, 980, z - 12, 40, 2080, z + 12);
            ok_r3d_push();
            ok_r3d_translate(P(-620, 1300, z));
            ok_r3d_rotate_z(OK_TURN / 8);
            box(-30, -380, -12, 30, 380, 12);
            ok_r3d_pop();
            ok_r3d_push();
            ok_r3d_translate(P(620, 1300, z));
            ok_r3d_rotate_z(-OK_TURN / 8);
            box(-30, -380, -12, 30, 380, 12);
            ok_r3d_pop();
        }
        /* The roof: two slopes from the kneewalls to the ridge, mossy, with
         * a short overhang all round, and a chimney. */
        otex(OK_OUT_MOSS_TILES, 6, RGB(10, 6, 3), OK_R3D_EDGE | OK_R3D_FLAT);
        for (k = -1; k <= 1; k += 2) {
            ok_r3d_push();
            ok_r3d_translate(P(k * 735, 1575, 90));
            ok_r3d_rotate_z(-k * (OK_TURN * 395 / 3600));
            box(-990, -32, -1250, 990, 32, 1250);
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
        ok_r3d_disc(P(0, 14, 0), U(282), U(282), 16);
        tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), 0);
        box(-282, 30, -22, 282, 46, 22);
        box(-22, 30, -282, 22, 46, 282);
        ok_r3d_pop();
        /* The door, big and rectangular as inside, under a little porch roof, with a lantern. */
        otex(OK_OUT_DOOR, 8, RGB(8, 5, 3), OK_R3D_EDGE);
        box(1420, 0, -300, 1480, 780, 260);
        tex(OK_ROOM_FRAME, 10, RGB(22, 21, 19), OK_R3D_EDGE);
        box(1420, 0, -350, 1500, 840, -300);
        box(1420, 0, 260, 1500, 840, 310);
        box(1420, 780, -350, 1500, 840, 310);
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
    prof(6); /* the house */
    /* Small bushes here and there, no trees. */
    bush(-1950, 900, 100, 1);
    bush(-2300, -300, 80, 2);
    bush(1950, 1500, 90, 3);
    bush(2100, -600, 75, 4);
    bush(-700, 2200, 70, 5);
    bush(1700, -1700, 85, 6);
    bush(-1800, -1600, 70, 7);
    /* A path from the door round to the back, where the deck is: the art
     * temple, three easels, a rail with fairy lights, a plant. */
    otex(OK_OUT_COBBLE, 6, RGB(10, 10, 9), OK_R3D_EDGE | OK_R3D_FLAT);
    for (i = 0; i < 5; i++) ok_r3d_disc(P(1150 + (i % 2) * 60, 2, 600 - i * 300), U(150), U(115), 7);
    otex(OK_OUT_DECK, 4, RGB(19, 12, 7), 0);
    quad_grid(P(-1400, 1, -700), P(1400, 1, -700), P(1400, 1, -2350), P(-1400, 1, -2350), 7, 5);
    temple();
    easel(9, -950, -2050, 180 + 25);
    easel(10, 950, -2050, 180 - 25);
    easel(11, -1150, -1250, 180 + 50);
    tex(OK_ROOM_BEAM, 6, RGB(12, 7, 4), OK_R3D_EDGE); /* posts and rails down the sides, fairy lights */
    for (k = -1; k <= 1; k += 2) {
        for (i = 0; i < 4; i++) box(k * 1400 - 30, 0, -2350 + i * 500, k * 1400 + 30, 520, -2290 + i * 500);
        limb_house(k * 1400, 500, -2350, k * 1400, 500, -800, 18);
        ok_r3d_push();
        ok_r3d_rotate_y(OK_TURN / 4);
        bulbs(800, 560, 2350, 560, k * 1400, 7, on, lit, &lights);
        ok_r3d_pop();
    }
    mat(RGB(20, 22, 24), OK_R3D_EDGE);
    ok_r3d_cylinder(P(1150, 0, -900), U(90), U(160), 8);
    otex(OK_OUT_LEAVES, 0, RGB(3, 9, 4), OK_R3D_EDGE);
    blob(1150, 300, -900, 150, 160, 150);
    ok_r3d_tile(0);
    ok_r3d_finish();
    prof(7); /* bushes, terrace, temple */
    for (i = 0; i < lights; i++) glow(ok_r3d_frame(), lit[i][0], lit[i][1], 9, 4, 3, 1);
    (void)l;
}

/* Haze: what is far melts into the colour of the horizon. */
static void fog(uint16_t *f, uint16_t haze, int near_units, int far_units)
{
    if (ok_r3d_on_hardware()) return; /* no depth to read back from the hardware */
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
    prof(-1);
    light_at(m, 0, &l);
    next_id = 0;
    ok_r3d_begin(frame, CHARCOAL);
    ok_r3d_camera(eye, target, focal);
    apply(&l);
    prof(0);
    sky(frame, eye, target, focal, m);
    prof(1);
    {
        /* On the hardware the scene is built once and drawn again under each
         * new camera: the film flies round it without rebuilding it. */
        static int world_minute = -1;
        if (!(ok_r3d_on_hardware() && world_minute == m && ok_r3d_world_again())) {
            ok_r3d_cpu_cull(0); /* seen again from other places */
            outside_scene(&l, l.night || m >= 1140);
            ok_r3d_cpu_cull(1);
            if (ok_r3d_on_hardware()) {
                ok_r3d_world_keep();
                world_minute = m;
            }
        }
    }
    prof(2);
    fireflies(frame, ticks, l.night || m >= 1140);
    prof(3);
    fog(frame, frame[(H - 30) * W], 70, 220);
    prof(4);
    /* Smoke from the chimney. */
    for (i = 0; i < 4; i++) {
        int k = (int)((ticks / 2 + (uint32_t)i * 60) % 240), x, y;
        if (ok_r3d_project(P(397 + k / 3 + (int)((int64_t)ok_r3d_sin((int)ticks * 8 + i * 900) * 30 / 4096), 1620 + k * 4, 923), &x, &y, NULL))
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
    room->home_x = 0;
    room->home_z = -2050;
    room->bed_x = 0; /* no bed out here: it goes in to sleep */
    room->bed_z = -2050;
    room->spot_x[0] = 0, room->spot_z[0] = -1500;     /* in the temple, by its pictures */
    room->spot_x[1] = -900, room->spot_z[1] = -1700;  /* by the easels */
    room->spot_x[2] = 900, room->spot_z[2] = -1800;
    room->door_x = 1150, room->door_z = 700;          /* on the step outside the door */
    room->face_yaw = 180;                             /* towards the camera at the back */
}
