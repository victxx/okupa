#include "okupa_monkey.h"
#include <string.h>

#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define FUR RGB(15, 9, 5)
#define FUR_DARK RGB(12, 7, 4)
#define CREAM RGB(30, 24, 18)
#define EAR_IN RGB(28, 18, 15)
#define INK RGB(3, 2, 2)
#define WHITE RGB(31, 31, 31)
#define BLUSH RGB(30, 17, 15)
#define DEG(d) ((d) * OK_TURN / 360)

/* Sizes in hundredths of a unit (a unit is about 10 cm). */
#define H(v) ((ok_fx)((v) * 4096 / 100))
static ok_v3 P(int x, int y, int z)
{
    return ok_v3_make(H(x), H(y), H(z));
}

static const ok_part parts[OK_PARTS] = {
    {"head", 0, 0, 64, 48},     {"top", 64, 0, 64, 48},      {"eye patches", 0, 48, 32, 32},
    {"muzzle", 32, 48, 32, 32}, {"ear", 64, 48, 16, 32},     {"inner ear", 80, 48, 16, 32},
    {"hands, feet", 96, 48, 32, 32}, {"sleeve", 0, 80, 32, 48}, {"leg", 32, 80, 32, 48},
    {"tail", 64, 80, 32, 48},   {"hood", 96, 80, 32, 48},
};

static const uint16_t tops[OK_COLOURS] = {RGB(9, 19, 17), RGB(29, 12, 10), RGB(29, 22, 7), RGB(6, 10, 20),
                                          RGB(21, 16, 27), RGB(17, 26, 20), RGB(29, 27, 23), RGB(8, 8, 9)};

const ok_part *ok_monkey_part(int part)
{
    return part >= 0 && part < OK_PARTS ? &parts[part] : &parts[0];
}

const char *ok_wear_name(int wear)
{
    static const char *n[OK_WEARS] = {"Nothing", "Headphones", "Bandana", "Glasses", "Scarf", "Flower", "Leaf", "Cap"};
    return wear >= 0 && wear < OK_WEARS ? n[wear] : "";
}

const char *ok_top_name(int top)
{
    static const char *n[OK_TOPS] = {"Hoodie", "T-shirt", "Stripes"};
    return top >= 0 && top < OK_TOPS ? n[top] : "";
}

uint16_t ok_top_colour(int colour)
{
    return tops[(unsigned)colour % OK_COLOURS];
}

static uint16_t shade(uint16_t c, int num, int den)
{
    int r = (c & 31) * num / den, g = (c >> 5 & 31) * num / den, b = (c >> 10 & 31) * num / den;
    return (uint16_t)(0x8000u | (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10);
}

static void fill(uint16_t *skin, int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) skin[j * OK_SKIN + i] = c;
}

void ok_monkey_skin(const ok_look *look, uint16_t skin[OK_SKIN * OK_SKIN])
{
    const ok_part *p;
    uint16_t top = ok_top_colour(look->colour);
    int i;
    fill(skin, 0, 0, OK_SKIN, OK_SKIN, FUR);
    /* Fur: a slightly darker crown and back of the head. */
    p = &parts[OK_PART_HEAD];
    fill(skin, p->x, p->y, p->w, p->h, FUR);
    fill(skin, p->x, p->y, p->w, 6, FUR_DARK);
    fill(skin, p->x, p->y, 10, p->h, FUR_DARK);
    fill(skin, p->x + p->w - 10, p->y, 10, p->h, FUR_DARK);
    p = &parts[OK_PART_EYES];
    fill(skin, p->x, p->y, p->w, p->h, CREAM);
    p = &parts[OK_PART_MUZZLE];
    fill(skin, p->x, p->y, p->w, p->h, CREAM);
    fill(skin, p->x + 13, p->y + 9, 6, 3, RGB(20, 12, 10)); /* nose */
    p = &parts[OK_PART_EAR_IN];
    fill(skin, p->x, p->y, p->w, p->h, EAR_IN);
    p = &parts[OK_PART_HANDS];
    fill(skin, p->x, p->y, p->w, p->h, CREAM);
    p = &parts[OK_PART_TAIL];
    fill(skin, p->x, p->y + p->h - 8, p->w, 8, CREAM);
    /* The top, and sleeves: full for the hoodie, short for the tees. */
    p = &parts[OK_PART_TOP];
    fill(skin, p->x, p->y, p->w, p->h, top);
    if (look->top == OK_TOP_STRIPES)
        for (i = 6; i < p->h - 4; i += 8) fill(skin, p->x, p->y + i, p->w, 3, RGB(30, 29, 26));
    if (look->top == OK_TOP_HOODIE) {
        fill(skin, p->x + 20, p->y + 28, 24, 8, shade(top, 4, 5)); /* front pocket */
        fill(skin, p->x + 29, p->y + 12, 1, 9, RGB(29, 28, 26));   /* strings */
        fill(skin, p->x + 34, p->y + 12, 1, 9, RGB(29, 28, 26));
    } else {
        fill(skin, p->x + 26, p->y + 7, 12, 3, shade(top, 4, 5)); /* collar */
    }
    fill(skin, p->x, p->y + p->h - 4, p->w, 4, shade(top, 4, 5)); /* hem */
    p = &parts[OK_PART_SLEEVE];
    fill(skin, p->x, p->y, p->w, look->top == OK_TOP_HOODIE ? p->h : p->h / 3, top);
    if (look->top == OK_TOP_HOODIE) fill(skin, p->x, p->y + p->h * 2 / 3, p->w, 4, shade(top, 4, 5)); /* cuff */
    if (look->top == OK_TOP_STRIPES) fill(skin, p->x, p->y + 6, p->w, 3, RGB(30, 29, 26));
    p = &parts[OK_PART_HOOD];
    fill(skin, p->x, p->y, p->w, p->h, shade(top, 9, 10));
}

/* ------------------------------------------------------------------ body */

static int next_id;

static void part(const uint16_t *skin, int which, int id)
{
    const ok_part *p = &parts[which];
    ok_r3d_texture(skin, OK_SKIN);
    ok_r3d_uv(p->x, p->y, p->w, p->h);
    ok_r3d_material(FUR, id, OK_R3D_EDGE | OK_R3D_TEXTURED);
}

static int solid(uint16_t c)
{
    ok_r3d_material(c, ++next_id, OK_R3D_EDGE);
    return next_id;
}

static void blob(int x, int y, int z, int rx, int ry, int rz)
{
    ok_r3d_ellipsoid(P(x, y, z), P(rx, ry, rz), 10, 7);
}

static void limb(int x0, int y0, int z0, int x1, int y1, int z1, int r)
{
    ok_r3d_capsule(P(x0, y0, z0), P(x1, y1, z1), H(r), 8);
}

/* sin of an angle, as a small integer in -n..n. */
static int wave(uint32_t ticks, int speed, int n)
{
    return (int)((int64_t)ok_r3d_sin((int)(ticks * (uint32_t)speed)) * n / 4096);
}

typedef struct {
    int bob, lean, nod, turn, tilt, raise[2], forward[2], tail, shake;
} rig;

static void pose_rig(int pose, uint32_t t, rig *r)
{
    memset(r, 0, sizeof *r);
    r->tail = wave(t, 23, 8);
    r->raise[0] = r->raise[1] = 8;
    switch (pose) {
    case OK_POSE_HAPPY: {
        int hop = wave(t, 90, 60);
        r->bob = hop < 0 ? -hop : hop;
        r->raise[0] = 135 + wave(t, 180, 18);
        r->raise[1] = 135 - wave(t, 180, 18);
        r->tilt = wave(t, 45, 8);
        r->tail = wave(t, 90, 20);
        break;
    }
    case OK_POSE_HUNGRY:
        r->lean = 6;
        r->nod = 10;
        r->raise[0] = r->raise[1] = -32;
        r->forward[0] = r->forward[1] = 48;
        r->shake = (t / 60) % 4 == 0 ? wave(t, 700, 8) : 0;
        r->bob = wave(t, 30, 3);
        break;
    case OK_POSE_SLEEPY:
        r->nod = 12 + wave(t, 14, 6);
        r->tilt = 6;
        r->bob = wave(t, 14, 5) - 6;
        r->raise[0] = r->raise[1] = 3;
        break;
    case OK_POSE_ASLEEP:
        r->bob = wave(t, 20, 4);
        r->raise[0] = r->raise[1] = 25;
        r->tail = 0;
        break;
    default:
        r->bob = wave(t, 40, 5);
        r->turn = wave(t, 9, 10);
        r->tilt = wave(t, 13, 3);
        r->raise[0] = 8 + wave(t, 40, 3);
        r->raise[1] = 8 - wave(t, 40, 3);
    }
}

static void headphones(void)
{
    static const int arc[7][2] = {{248, 640}, {235, 735}, {180, 830}, {0, 905}, {-180, 830}, {-235, 735}, {-248, 640}};
    int i;
    solid(RGB(6, 6, 7));
    for (i = 0; i < 6; i++) limb(arc[i][0], arc[i][1], -20, arc[i + 1][0], arc[i + 1][1], -20, 18);
    solid(RGB(28, 9, 8));
    blob(-255, 625, -10, 70, 100, 100);
    blob(255, 625, -10, 70, 100, 100);
}

static void flower(void)
{
    int i;
    solid(RGB(31, 20, 24));
    for (i = 0; i < 5; i++) {
        int a = DEG(i * 72 + 90);
        blob(175 + (int)((int64_t)ok_r3d_cos(a) * 50 / 4096), 830 + (int)((int64_t)ok_r3d_sin(a) * 50 / 4096), 120, 42, 42, 22);
    }
    solid(RGB(31, 27, 8));
    blob(175, 830, 140, 26, 26, 18);
}

static void wear(int which)
{
    switch (which) {
    case OK_WEAR_HEADPHONES: headphones(); break;
    case OK_WEAR_BANDANA:
        solid(RGB(27, 7, 7));
        ok_r3d_ellipsoid(P(0, 735, -5), P(247, 48, 240), 12, 5);
        blob(150, 745, -195, 55, 45, 45);
        limb(150, 745, -200, 215, 610, -250, 22);
        limb(150, 745, -200, 110, 600, -270, 22);
        break;
    case OK_WEAR_FLOWER: flower(); break;
    case OK_WEAR_LEAF:
        solid(RGB(9, 13, 5));
        limb(0, 860, 0, 10, 960, 0, 10);
        solid(RGB(10, 22, 8));
        ok_r3d_push();
        ok_r3d_translate(P(60, 975, 0));
        ok_r3d_rotate_z(DEG(25));
        blob(0, 0, 0, 75, 20, 42);
        ok_r3d_pop();
        break;
    case OK_WEAR_CAP:
        solid(RGB(7, 12, 24));
        ok_r3d_ellipsoid(P(0, 740, -10), P(246, 150, 242), 12, 7);
        ok_r3d_ellipsoid(P(0, 765, -235), P(130, 22, 115), 10, 4);
        solid(RGB(29, 28, 26));
        blob(0, 885, -10, 28, 20, 28);
        break;
    default: break;
    }
}

void ok_monkey_draw(const ok_look *look, const uint16_t *skin, int pose, uint32_t ticks, ok_monkey_face *face)
{
    rig r;
    int s, i, legs, top, hands;
    pose_rig(pose, ticks, &r);
    memset(face, 0, sizeof *face);
    face->pose = pose;
    face->wear = look->wear;
    face->blink = pose == OK_POSE_IDLE ? ticks % 200 >= 193 : pose == OK_POSE_HUNGRY ? ticks % 260 >= 253 : 0;
    face->open = pose == OK_POSE_SLEEPY && ticks % 300 > 230;
    next_id = 100;
    ok_r3d_push();
    if (pose == OK_POSE_ASLEEP) {
        ok_r3d_translate(P(0, 150, 0));
        ok_r3d_rotate_z(DEG(90));
        ok_r3d_translate(P(0, -190, 0));
    } else {
        ok_r3d_material(0, 0, OK_R3D_SHADOW);
        ok_r3d_disc(P(30, 5, 0), H(250), H(185), 12);
    }
    ok_r3d_translate(P(r.shake, 0, 0));
    /* Legs and feet. */
    legs = ++next_id;
    part(skin, OK_PART_LEG, legs);
    limb(-75, 70, 0, -75, 190, 0, 55);
    limb(75, 70, 0, 75, 190, 0, 55);
    hands = ++next_id;
    part(skin, OK_PART_HANDS, hands);
    ok_r3d_ellipsoid(P(-85, 38, 35), P(70, 40, 95), 10, 6);
    ok_r3d_ellipsoid(P(85, 38, 35), P(70, 40, 95), 10, 6);
    /* The body bobs and leans around the hips. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 170 + r.bob, 0));
    ok_r3d_rotate_x(DEG(r.lean));
    ok_r3d_translate(P(0, -170, 0));
    /* Tail, curling up on its right. */
    part(skin, OK_PART_TAIL, ++next_id);
    ok_r3d_push();
    ok_r3d_translate(P(60, 160, -110));
    ok_r3d_rotate_y(DEG(r.tail));
    limb(150, -40, -40, 0, 0, 0, 32);
    limb(250, 40, -10, 150, -40, -40, 32);
    limb(270, 160, 40, 250, 40, -10, 32);
    limb(210, 240, 70, 270, 160, 40, 32);
    limb(165, 200, 80, 210, 240, 70, 32);
    ok_r3d_pop();
    top = ++next_id;
    part(skin, OK_PART_TOP, top);
    ok_r3d_ellipsoid(P(0, 290, 0), P(175, 170, 145), 12, 8);
    if (look->top == OK_TOP_HOODIE) {
        part(skin, OK_PART_HOOD, ++next_id);
        ok_r3d_ellipsoid(P(0, 430, -75), P(150, 75, 100), 10, 6);
    }
    if (look->wear == OK_WEAR_SCARF) {
        solid(RGB(28, 9, 8));
        ok_r3d_ellipsoid(P(0, 448, 0), P(158, 52, 140), 12, 5);
        solid(RGB(26, 7, 7));
        blob(70, 365, 125, 42, 90, 28);
    }
    /* Arms from the shoulders: raise turns them outwards, forward to the front. */
    for (s = -1; s <= 1; s += 2) {
        int k = s > 0;
        ok_r3d_push();
        ok_r3d_translate(P(s * 150, 390, 10));
        ok_r3d_rotate_z(DEG(s * r.raise[k]));
        ok_r3d_rotate_x(-DEG(r.forward[k]));
        part(skin, OK_PART_SLEEVE, ++next_id);
        limb(s * 45, -150, 30, 0, 0, 0, 48);
        part(skin, OK_PART_HANDS, hands);
        ok_r3d_ellipsoid(P(s * 50, -175, 35), P(55, 55, 55), 8, 6);
        ok_r3d_pop();
    }
    /* Head, turning on the neck. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 430, 0));
    ok_r3d_rotate_y(DEG(r.turn));
    ok_r3d_rotate_x(DEG(r.nod));
    ok_r3d_rotate_z(DEG(r.tilt));
    ok_r3d_translate(P(0, -430, 0));
    face->head_id = ++next_id;
    part(skin, OK_PART_HEAD, face->head_id);
    ok_r3d_ellipsoid(P(0, 630, 0), P(240, 230, 230), 12, 9);
    if (look->wear != OK_WEAR_HEADPHONES) {
        part(skin, OK_PART_EAR, face->head_id);
        blob(-235, 640, -10, 45, 85, 85);
        blob(235, 640, -10, 45, 85, 85);
        part(skin, OK_PART_EAR_IN, ++next_id);
        blob(-255, 640, 5, 25, 55, 55);
        blob(255, 640, 5, 25, 55, 55);
    }
    face->mask_id = ++next_id;
    part(skin, OK_PART_EYES, face->mask_id);
    blob(-75, 645, 145, 105, 115, 95);
    blob(75, 645, 145, 105, 115, 95);
    part(skin, OK_PART_MUZZLE, face->mask_id);
    blob(0, 555, 170, 135, 95, 85);
    wear(look->wear);
    for (i = 0; i < 2; i++)
        if (!ok_r3d_project(P(i ? 75 : -75, 640, 232), &face->eye_x[i], &face->eye_y[i], &face->eye_z[i])) face->eye_x[i] = -100;
    if (!ok_r3d_project(P(0, 540, 255), &face->mouth_x, &face->mouth_y, &face->mouth_z)) face->mouth_x = -100;
    ok_r3d_pop();
    ok_r3d_pop();
    ok_r3d_pop();
}

/* ------------------------------------------------------------------ face */

static void sprite(uint16_t *frame, const ok_monkey_face *f, int cx, int cy, const char *const rows[], int n)
{
    int r, c, w = (int)strlen(rows[0]);
    for (r = 0; r < n; r++)
        for (c = 0; c < w; c++) {
            int x = cx - w / 2 + c, y = cy - n / 2 + r, id = ok_r3d_id_at(x, y);
            char k = rows[r][c];
            if (k == '.' || (id != f->head_id && id != f->mask_id)) continue;
            frame[y * OK_R3D_W + x] = k == '#' ? INK : k == 'o' ? WHITE : k == 'p' ? BLUSH : RGB(27, 11, 11);
        }
}

static const char *const eye_open[] = {".##.", "#oo#", "#o##", "####", "####", ".##."};
static const char *const eye_down[] = {".##.", "####", "#o##", "#oo#", ".##."};
static const char *const eye_half[] = {"####", "#o##", ".##."};
static const char *const eye_shut[] = {"#..#", ".##."};
static const char *const eye_joy[] = {".##.", "#..#"};
static const char *const eye_blink[] = {"####"};
static const char *const mouth_smile[] = {"#...#", ".###."};
static const char *const mouth_open[] = {"#####", "#rrr#", ".###."};
static const char *const mouth_wobble[] = {".#.#.", "#.#.#"};
static const char *const mouth_yawn[] = {".##.", "#rr#", "#rr#", ".##."};
static const char *const mouth_small[] = {".##."};
static const char *const cheek[] = {"pp"};
static const char *const brow_l[] = {"..##", "##.."};
static const char *const brow_r[] = {"##..", "..##"};
static const char *const glasses[] = {"..####..", ".#....#.", "#......#", "#......#", "#......#", "#......#",
                                      ".#....#.", "..####.."};
static const char *const bridge[] = {"###"};

/* A face point counts only if nothing stands in front of it (the head turned
 * away, an arm across it). */
static int seen(int x, int y, int32_t z)
{
    int32_t d = ok_r3d_depth_at(x, y);
    return x > -50 && d && z >= d - d / 32;
}

void ok_monkey_face_draw(uint16_t *frame, const ok_monkey_face *f)
{
    int i;
    for (i = 0; i < 2; i++) {
        int x = f->eye_x[i], y = f->eye_y[i];
        if (!seen(x, y, f->eye_z[i])) continue;
        switch (f->pose) {
        case OK_POSE_HAPPY: sprite(frame, f, x, y, eye_joy, 2); break;
        case OK_POSE_SLEEPY: sprite(frame, f, x, y + 1, eye_half, 3); break;
        case OK_POSE_ASLEEP: sprite(frame, f, x, y + 1, eye_shut, 2); break;
        case OK_POSE_HUNGRY:
            if (f->blink) sprite(frame, f, x, y + 1, eye_blink, 1);
            else sprite(frame, f, x, y, eye_down, 5);
            sprite(frame, f, x + (i ? 1 : -1), y - 5, i ? brow_r : brow_l, 2);
            break;
        default:
            if (f->blink) sprite(frame, f, x, y + 1, eye_blink, 1);
            else sprite(frame, f, x, y, eye_open, 6);
        }
        if (f->pose != OK_POSE_HUNGRY) sprite(frame, f, x + (i ? 4 : -4), y + 5, cheek, 1);
        if (f->wear == OK_WEAR_GLASSES) sprite(frame, f, x, y, glasses, 8);
    }
    if (f->wear == OK_WEAR_GLASSES && seen(f->eye_x[0], f->eye_y[0], f->eye_z[0]) && seen(f->eye_x[1], f->eye_y[1], f->eye_z[1]))
        sprite(frame, f, (f->eye_x[0] + f->eye_x[1]) / 2, f->eye_y[0] - 1, bridge, 1);
    if (!seen(f->mouth_x, f->mouth_y, f->mouth_z)) return;
    switch (f->pose) {
    case OK_POSE_HAPPY: sprite(frame, f, f->mouth_x, f->mouth_y, mouth_open, 3); break;
    case OK_POSE_HUNGRY: sprite(frame, f, f->mouth_x, f->mouth_y, mouth_wobble, 2); break;
    case OK_POSE_SLEEPY:
        if (f->open) sprite(frame, f, f->mouth_x, f->mouth_y + 1, mouth_yawn, 4);
        else sprite(frame, f, f->mouth_x, f->mouth_y, mouth_small, 1);
        break;
    case OK_POSE_ASLEEP: sprite(frame, f, f->mouth_x, f->mouth_y, mouth_small, 1); break;
    default: sprite(frame, f, f->mouth_x, f->mouth_y, mouth_smile, 2);
    }
}
