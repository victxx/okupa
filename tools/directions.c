/* Four looks for the okupa and its house, drawn with the game's own 3D
 * renderer, for choosing one. Host only; the chosen look moves into game/.
 *   build/okupa-directions <out dir> */
#include "okupa_font.h"
#include "okupa_r3d.h"
#include "png.h"

#define W OK_R3D_W
#define H OK_R3D_H
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define CHARCOAL RGB(5, 5, 6)

static uint16_t frame[W * H];
static int next_id;

static ok_v3 V(double x, double y, double z)
{
    return ok_v3_make(OK_FX(x), OK_FX(y), OK_FX(z));
}

static int mat(uint16_t c, int flags)
{
    ok_r3d_material(c, ++next_id, flags);
    return next_id;
}

static void same(uint16_t c, int id, int flags)
{
    ok_r3d_material(c, id, flags);
}

static void box(double x0, double y0, double z0, double x1, double y1, double z1)
{
    ok_r3d_box(V(x0, y0, z0), V(x1, y1, z1));
}

static void blob(double x, double y, double z, double rx, double ry, double rz)
{
    ok_r3d_ellipsoid(V(x, y, z), V(rx, ry, rz), 10, 7);
}

static void limb(double x0, double y0, double z0, double x1, double y1, double z1, double r)
{
    ok_r3d_capsule(V(x0, y0, z0), V(x1, y1, z1), OK_FX(r), 8);
}

/* Light by the hour; at night the lamp is the key light. */
enum { DAY, EVENING, NIGHT };

static int when, iso;

/* The light comes from the upper left of the camera; iso scenes look from the corner. */
static void lights(int time)
{
    double fx = iso ? 0.7 : 0, fz = iso ? 0.7 : 1, lx = iso ? -0.7 : -1, lz = iso ? 0.7 : 0;
    when = time;
    if (time == DAY) ok_r3d_light(V(0.5 * lx + 0.7 * fx, 1.0, 0.5 * lz + 0.7 * fz), V(0.48, 0.46, 0.42), V(0.62, 0.62, 0.66));
    else if (time == EVENING) ok_r3d_light(V(0.9 * lx + 0.4 * fx, 0.5, 0.9 * lz + 0.4 * fz), V(0.55, 0.36, 0.22), V(0.5, 0.45, 0.55));
    else ok_r3d_light(V(0.8 * lx + 0.6 * fx, 0.7, 0.8 * lz + 0.6 * fz), V(0.58, 0.42, 0.22), V(0.24, 0.25, 0.42));
}

static uint16_t sky(void)
{
    return when == DAY ? RGB(15, 24, 31) : when == EVENING ? RGB(30, 17, 11) : RGB(3, 4, 12);
}

/* A face drawn in pixels over the head, only where the head was drawn. */
typedef struct {
    int ids[3];
} face_mask;

static void sprite(const face_mask *m, int cx, int cy, const char *rows[], int n, uint16_t ink, uint16_t light, uint16_t blush)
{
    int r, c, w = (int)strlen(rows[0]);
    for (r = 0; r < n; r++)
        for (c = 0; c < w; c++) {
            int x = cx - w / 2 + c, y = cy - n / 2 + r, id = ok_r3d_id_at(x, y);
            char k = rows[r][c];
            if (k == '.' || !id || (id != m->ids[0] && id != m->ids[1] && id != m->ids[2])) continue;
            frame[y * W + x] = k == '#' ? ink : k == 'o' ? light : blush;
        }
}

static void at(ok_v3 p, int *x, int *y)
{
    if (!ok_r3d_project(p, x, y, NULL)) *x = *y = -100;
}

/* ---------------------------------------------------------------- A: monkey */

static void monkey(double x, double z)
{
    static const char *eye[] = {".##.", "#oo#", "#o##", "####", "####", ".##."};
    static const char *mouth[] = {"#...#", ".###."};
    static const char *cheek[] = {"pp"};
    uint16_t fur = RGB(15, 9, 5), cream = RGB(30, 24, 18), hoodie = RGB(9, 19, 17), hat = RGB(29, 20, 6);
    face_mask m;
    int ex[2], ey[2], mx, my, i, head, mask;
    ok_r3d_push();
    ok_r3d_translate(V(x, 0, z));
    ok_r3d_rotate_y(-120);
    same(RGB(0, 0, 0), 0, OK_R3D_SHADOW);
    ok_r3d_disc(V(0.3, 0.05, 0), OK_FX(2.6), OK_FX(1.9), 12);
    /* Tail, curling up on its right. */
    mat(fur, OK_R3D_EDGE);
    limb(0.8, 1.6, -1.1, 2.2, 1.1, -1.5, 0.32);
    limb(2.2, 1.1, -1.5, 3.2, 1.9, -1.2, 0.32);
    limb(3.2, 1.9, -1.2, 3.4, 3.1, -0.7, 0.32);
    limb(3.4, 3.1, -0.7, 2.8, 3.9, -0.4, 0.32);
    limb(2.8, 3.9, -0.4, 2.35, 3.5, -0.3, 0.32);
    mat(fur, OK_R3D_EDGE);
    limb(-0.75, 0.7, 0, -0.75, 1.9, 0, 0.55);
    limb(0.75, 0.7, 0, 0.75, 1.9, 0, 0.55);
    mat(cream, OK_R3D_EDGE);
    blob(-0.85, 0.38, 0.35, 0.7, 0.4, 0.95);
    blob(0.85, 0.38, 0.35, 0.7, 0.4, 0.95);
    mat(hoodie, OK_R3D_EDGE);
    blob(0, 2.9, 0, 1.75, 1.7, 1.45);
    blob(0, 4.3, -0.75, 1.5, 0.75, 1.0);
    same(RGB(7, 15, 14), next_id, OK_R3D_EDGE);
    blob(0, 2.35, 1.2, 1.0, 0.5, 0.32);
    mat(hoodie, OK_R3D_EDGE);
    limb(-1.5, 3.9, 0.1, -2.1, 2.4, 0.5, 0.48);
    limb(1.5, 3.9, 0.1, 2.1, 2.4, 0.5, 0.48);
    mat(cream, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(-2.15, 2.15, 0.55), V(0.55, 0.55, 0.55), 8, 6);
    ok_r3d_ellipsoid(V(2.15, 2.15, 0.55), V(0.55, 0.55, 0.55), 8, 6);
    head = mat(fur, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 6.3, 0), V(2.4, 2.3, 2.3), 12, 9);
    blob(-2.35, 6.4, -0.1, 0.45, 0.85, 0.85);
    blob(2.35, 6.4, -0.1, 0.45, 0.85, 0.85);
    mat(RGB(28, 18, 15), 0);
    blob(-2.55, 6.4, 0.05, 0.25, 0.55, 0.55);
    blob(2.55, 6.4, 0.05, 0.25, 0.55, 0.55);
    mask = mat(cream, OK_R3D_EDGE);
    blob(-0.75, 6.45, 1.45, 1.05, 1.15, 0.95);
    blob(0.75, 6.45, 1.45, 1.05, 1.15, 0.95);
    blob(0, 5.55, 1.7, 1.35, 0.95, 0.85);
    mat(hat, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 7.55, -0.15), V(2.5, 1.65, 2.45), 12, 8);
    same(RGB(25, 16, 4), next_id, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 7.6, -0.1), V(2.62, 0.55, 2.6), 12, 5);
    mat(cream, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 9.25, -0.35), V(0.65, 0.6, 0.65), 8, 6);
    for (i = 0; i < 2; i++) at(V(i ? 0.75 : -0.75, 6.4, 2.3), &ex[i], &ey[i]);
    at(V(0, 5.4, 2.55), &mx, &my);
    ok_r3d_pop();
    ok_r3d_finish();
    m.ids[0] = mask;
    m.ids[1] = head;
    m.ids[2] = mask;
    for (i = 0; i < 2; i++) sprite(&m, ex[i], ey[i], eye, 6, RGB(3, 2, 2), RGB(31, 31, 31), 0);
    sprite(&m, mx, my, mouth, 2, RGB(9, 4, 3), 0, 0);
    sprite(&m, ex[0] - 4, ey[0] + 5, cheek, 1, 0, 0, RGB(30, 17, 15));
    sprite(&m, ex[1] + 4, ey[1] + 5, cheek, 1, 0, 0, RGB(30, 17, 15));
}

static void string_lights(double x0, double y0, double x1, double y1, double z, int n)
{
    static const uint16_t bulbs[] = {RGB(31, 26, 10), RGB(31, 15, 12), RGB(16, 28, 20), RGB(31, 26, 10), RGB(20, 22, 31)};
    int i;
    mat(RGB(4, 4, 4), 0);
    for (i = 0; i < n; i++) {
        double t0 = (double)i / n, t1 = (double)(i + 1) / n;
        double s0 = 0.9 * 4 * t0 * (1 - t0), s1 = 0.9 * 4 * t1 * (1 - t1);
        limb(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0 - s0, z, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1 - s1, z, 0.05);
    }
    for (i = 1; i < n; i++) {
        double t = (double)i / n, s = 0.9 * 4 * t * (1 - t);
        uint16_t c = bulbs[i % 5];
        if (when == DAY) c = (uint16_t)(0x8000 | ((c >> 1) & 0x3def) + RGB(8, 8, 8));
        mat(c, OK_R3D_UNLIT);
        ok_r3d_ellipsoid(V(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t - s - 0.35, z + 0.2), V(0.28, 0.36, 0.28), 6, 4);
    }
}

static void attic(void)
{
    uint16_t plank = RGB(20, 13, 8), plank2 = RGB(18, 11, 7), wall = RGB(25, 21, 17), beam = RGB(12, 7, 4);
    uint16_t roof = RGB(22, 18, 15);
    int i, id;
    ok_r3d_camera(V(0, 21, 46), V(0, 6.5, 0), 340);
    /* Floor: planks running to the back. */
    for (i = 0; i < 14; i++) {
        mat(i % 2 ? plank : plank2, 0);
        box(-14 + i * 2, -1, -11, -12 + i * 2, 0, 13);
    }
    /* Gable wall and the two slopes of the roof seen from inside. */
    id = mat(wall, 0);
    ok_r3d_quad(V(-14, 0, -11), V(14, 0, -11), V(14, 7, -11), V(-14, 7, -11));
    ok_r3d_quad(V(-14, 7, -11), V(14, 7, -11), V(0, 21, -11), V(0, 21, -11));
    same(roof, ++next_id, OK_R3D_BOTH);
    ok_r3d_quad(V(-14, 0, -11), V(-14, 7, -11), V(-14, 7, 13), V(-14, 0, 13));
    ok_r3d_quad(V(14, 0, -11), V(14, 7, -11), V(14, 7, 13), V(14, 0, 13));
    ok_r3d_quad(V(-14, 7, -11), V(0, 21, -11), V(0, 21, 13), V(-14, 7, 13));
    ok_r3d_quad(V(14, 7, -11), V(0, 21, -11), V(0, 21, 13), V(14, 7, 13));
    (void)id;
    mat(beam, 0);
    for (i = 0; i < 2; i++) {
        double z = -10.4 + i * 7;
        ok_r3d_push();
        ok_r3d_translate(V(-7, 14, z));
        ok_r3d_rotate_z(OK_TURN / 8);
        box(-9.6, -0.5, -0.4, 9.6, 0.5, 0.4);
        ok_r3d_pop();
        ok_r3d_push();
        ok_r3d_translate(V(7, 14, z));
        ok_r3d_rotate_z(-OK_TURN / 8);
        box(-9.6, -0.5, -0.4, 9.6, 0.5, 0.4);
        ok_r3d_pop();
    }
    box(-14, 6.6, -10.9, 14, 7.4, -10.2);
    /* Round window in the gable. */
    ok_r3d_push();
    ok_r3d_translate(V(0, 12.5, -10.95));
    ok_r3d_rotate_x(OK_TURN / 4);
    mat(beam, 0);
    ok_r3d_disc(V(0, 0, 0), OK_FX(3.3), OK_FX(3.3), 16);
    mat(sky(), OK_R3D_UNLIT);
    ok_r3d_disc(V(0, 0.05, 0), OK_FX(2.8), OK_FX(2.8), 16);
    mat(when == DAY ? RGB(31, 30, 18) : RGB(29, 29, 24), OK_R3D_UNLIT);
    ok_r3d_disc(when == DAY ? V(1.2, 0.08, -1.0) : V(-1.1, 0.08, -0.9), OK_FX(0.7), OK_FX(0.7), 10);
    mat(beam, 0);
    box(-2.8, 0.06, -0.2, 2.8, 0.2, 0.2);
    box(-0.2, 0.06, -2.8, 0.2, 0.2, 2.8);
    ok_r3d_pop();
    string_lights(-13, 7.2, 0, 19.5, -9.8, 9);
    string_lights(0, 19.5, 13, 7.2, -9.8, 9);
    /* Rug, mattress on the floor, boxes, a crate with a mug, a plant in a tin. */
    mat(RGB(27, 11, 9), 0);
    ok_r3d_disc(V(2, 0.04, 3), OK_FX(6.5), OK_FX(4.2), 16);
    mat(RGB(29, 22, 12), 0);
    ok_r3d_disc(V(2, 0.06, 3), OK_FX(5.2), OK_FX(3.2), 16);
    mat(RGB(24, 24, 26), OK_R3D_EDGE);
    box(-13, 0, -9.5, -4, 1.5, 1);
    mat(RGB(30, 29, 27), OK_R3D_EDGE);
    blob(-11, 1.9, -7.6, 1.6, 0.6, 1.1);
    mat(RGB(7, 13, 24), OK_R3D_EDGE);
    blob(-8.2, 1.9, -3, 4.4, 0.75, 4.0);
    mat(RGB(23, 17, 10), OK_R3D_EDGE);
    box(7, 0, -10, 11.5, 3.5, -6);
    mat(RGB(25, 19, 12), OK_R3D_EDGE);
    box(7.6, 3.5, -9.6, 11, 6.3, -6.6);
    mat(RGB(26, 20, 13), OK_R3D_EDGE);
    box(11.6, 0, -9.5, 13.6, 2.2, -6.5);
    mat(RGB(28, 25, 18), 0);
    box(7, 3.45, -8.3, 11.5, 3.56, -7.7);
    box(7.6, 6.25, -8.4, 11, 6.36, -7.8);
    mat(RGB(17, 11, 6), OK_R3D_EDGE);
    box(-3, 0, -9, 1, 2.6, -6);
    mat(RGB(28, 28, 27), OK_R3D_EDGE);
    ok_r3d_cylinder(V(-1.8, 2.6, -7.5), OK_FX(0.5), OK_FX(0.9), 8);
    mat(RGB(20, 22, 24), OK_R3D_EDGE);
    ok_r3d_cylinder(V(-11.5, 0, 6), OK_FX(0.9), OK_FX(1.6), 8);
    mat(RGB(7, 18, 8), OK_R3D_EDGE);
    blob(-11.5, 2.8, 6, 1.4, 1.5, 1.4);
    blob(-12.3, 3.9, 5.8, 0.8, 1.0, 0.8);
}

/* ------------------------------------------------------- B: little villager */

static void villager(double x, double z)
{
    static const char *eye[] = {"##", "##", "##"};
    static const char *mouth[] = {"#..#", ".##."};
    static const char *cheek[] = {"ppp"};
    uint16_t skin = RGB(31, 24, 18), hair = RGB(11, 6, 4), shirt = RGB(29, 12, 10), shorts = RGB(6, 10, 19);
    face_mask m;
    int ex[2], ey[2], mx, my, i, head;
    ok_r3d_push();
    ok_r3d_translate(V(x, 0, z));
    same(0, 0, OK_R3D_SHADOW);
    ok_r3d_disc(V(0, 0.05, 0), OK_FX(2.2), OK_FX(1.6), 12);
    mat(RGB(13, 8, 5), OK_R3D_EDGE);
    blob(-0.75, 0.35, 0.25, 0.6, 0.38, 0.85);
    blob(0.75, 0.35, 0.25, 0.6, 0.38, 0.85);
    mat(skin, OK_R3D_EDGE);
    limb(-0.7, 0.6, 0, -0.7, 1.6, 0, 0.42);
    limb(0.7, 0.6, 0, 0.7, 1.6, 0, 0.42);
    mat(shorts, OK_R3D_EDGE);
    blob(0, 1.9, 0, 1.35, 0.75, 1.15);
    mat(shirt, OK_R3D_EDGE);
    blob(0, 2.95, 0, 1.45, 1.25, 1.2);
    same(RGB(31, 28, 25), next_id, OK_R3D_EDGE);
    blob(0, 2.95, 0.02, 1.47, 0.22, 1.22);
    mat(shirt, OK_R3D_EDGE);
    limb(-1.3, 3.6, 0, -1.9, 2.5, 0.35, 0.4);
    limb(1.3, 3.6, 0, 1.9, 2.5, 0.35, 0.4);
    mat(skin, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(-1.95, 2.3, 0.4), V(0.45, 0.45, 0.45), 8, 6);
    ok_r3d_ellipsoid(V(1.95, 2.3, 0.4), V(0.45, 0.45, 0.45), 8, 6);
    head = mat(skin, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 6.0, 0), V(2.6, 2.45, 2.45), 12, 9);
    mat(hair, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 6.85, -0.45), V(2.78, 2.3, 2.6), 12, 9);
    blob(-1.0, 7.75, 1.45, 1.4, 0.75, 0.9);
    blob(0.9, 7.85, 1.5, 1.3, 0.7, 0.85);
    blob(-2.2, 6.2, 0.6, 0.6, 1.3, 0.9);
    blob(2.2, 6.2, 0.6, 0.6, 1.3, 0.9);
    for (i = 0; i < 2; i++) at(V(i ? 0.85 : -0.85, 5.9, 2.5), &ex[i], &ey[i]);
    at(V(0, 4.9, 2.4), &mx, &my);
    ok_r3d_pop();
    ok_r3d_finish();
    m.ids[0] = m.ids[1] = m.ids[2] = head;
    for (i = 0; i < 2; i++) sprite(&m, ex[i], ey[i], eye, 3, RGB(4, 3, 3), 0, 0);
    sprite(&m, mx, my, mouth, 2, RGB(12, 4, 4), 0, 0);
    sprite(&m, ex[0] - 3, ey[0] + 4, cheek, 1, 0, 0, RGB(31, 18, 16));
    sprite(&m, ex[1] + 3, ey[1] + 4, cheek, 1, 0, 0, RGB(31, 18, 16));
}

static void lamp(double x, double z, double h)
{
    mat(RGB(12, 8, 5), OK_R3D_EDGE);
    ok_r3d_cylinder(V(x, 0, z), OK_FX(0.9), OK_FX(0.3), 10);
    ok_r3d_cylinder(V(x, 0, z), OK_FX(0.15), OK_FX(h), 6);
    mat(when == NIGHT ? RGB(31, 27, 16) : RGB(29, 25, 19), when == NIGHT ? OK_R3D_UNLIT | OK_R3D_EDGE : OK_R3D_EDGE);
    ok_r3d_cylinder(V(x, h - 0.3, z), OK_FX(1.3), OK_FX(1.8), 10);
}

static void window_square(double x, double y, double w, double h, double z, uint16_t frame_c, uint16_t curtain)
{
    mat(frame_c, OK_R3D_EDGE);
    box(x - w / 2 - 0.5, y - 0.5, z, x + w / 2 + 0.5, y + h + 0.5, z + 0.3);
    mat(sky(), OK_R3D_UNLIT);
    ok_r3d_quad(V(x - w / 2, y, z + 0.35), V(x + w / 2, y, z + 0.35), V(x + w / 2, y + h, z + 0.35), V(x - w / 2, y + h, z + 0.35));
    mat(when == DAY ? RGB(31, 30, 18) : RGB(29, 29, 24), OK_R3D_UNLIT);
    ok_r3d_push();
    ok_r3d_translate(V(x + (when == DAY ? w / 4 : -w / 4), y + h * 0.7, z + 0.38));
    ok_r3d_rotate_x(OK_TURN / 4);
    ok_r3d_disc(V(0, 0, 0), OK_FX(0.75), OK_FX(0.75), 10);
    ok_r3d_pop();
    mat(frame_c, 0);
    box(x - 0.15, y, z + 0.3, x + 0.15, y + h, z + 0.5);
    box(x - w / 2, y + h / 2 - 0.15, z + 0.3, x + w / 2, y + h / 2 + 0.15, z + 0.5);
    mat(curtain, OK_R3D_EDGE);
    blob(x - w / 2 - 0.4, y + h / 2 + 0.2, z + 0.9, 0.8, h / 2 + 1, 0.5);
    blob(x + w / 2 + 0.4, y + h / 2 + 0.2, z + 0.9, 0.8, h / 2 + 1, 0.5);
}

static void cabin(void)
{
    int i;
    ok_r3d_camera(V(0, 21, 46), V(0, 6, 0), 330);
    for (i = 0; i < 9; i++) {
        mat(i % 2 ? RGB(25, 18, 11) : RGB(27, 20, 12), 0);
        box(-14, -1, -11 + i * 3, 14, 0, -8 + i * 3);
    }
    for (i = 0; i < 8; i++) {
        uint16_t c = i % 2 ? RGB(21, 13, 7) : RGB(23, 15, 8);
        mat(c, 0);
        box(-14, i * 2.6, -11.5, 14, i * 2.6 + 2.45, -11);
        box(-14.5, i * 2.6, -11.5, -14, i * 2.6 + 2.45, 14);
        box(14, i * 2.6, -11.5, 14.5, i * 2.6 + 2.45, 14);
    }
    window_square(3, 7, 7, 6, -11, RGB(29, 28, 25), RGB(28, 14, 12));
    mat(RGB(10, 18, 14), 0);
    ok_r3d_disc(V(1, 0.04, 2.5), OK_FX(7), OK_FX(4.5), 18);
    mat(RGB(16, 24, 18), 0);
    ok_r3d_disc(V(1, 0.06, 2.5), OK_FX(5.4), OK_FX(3.3), 18);
    /* Bed with a patchwork quilt. */
    mat(RGB(15, 9, 5), OK_R3D_EDGE);
    box(-13.5, 0, -10.5, -5, 2, 2);
    box(-13.5, 0, -10.8, -5, 5, -9.8);
    mat(RGB(30, 29, 27), OK_R3D_EDGE);
    box(-13, 2, -9.8, -5.5, 2.8, 1.5);
    blob(-9.2, 3.2, -8.2, 2.6, 0.7, 1.2);
    {
        static const uint16_t q[] = {RGB(28, 12, 10), RGB(29, 24, 10), RGB(9, 17, 25), RGB(14, 22, 12)};
        int a, b;
        for (a = 0; a < 3; a++)
            for (b = 0; b < 3; b++) {
                mat(q[(a + b * 2) % 4], OK_R3D_EDGE);
                box(-13.3 + a * 2.6, 2.6, -6.5 + b * 2.7, -10.7 + a * 2.6, 3.3, -3.8 + b * 2.7);
            }
    }
    /* Table with a mug, shelf, plant, lamp. */
    mat(RGB(19, 12, 6), OK_R3D_EDGE);
    ok_r3d_cylinder(V(9, 0, 3), OK_FX(0.35), OK_FX(3.2), 6);
    ok_r3d_cylinder(V(9, 3.2, 3), OK_FX(2.4), OK_FX(0.4), 12);
    mat(RGB(30, 30, 29), OK_R3D_EDGE);
    ok_r3d_cylinder(V(8.4, 3.6, 3.4), OK_FX(0.45), OK_FX(0.8), 8);
    mat(RGB(19, 12, 6), OK_R3D_EDGE);
    box(9, 0, -10.9, 13.5, 8, -9);
    mat(RGB(28, 12, 10), OK_R3D_EDGE);
    box(9.5, 4.2, -10.6, 10.5, 6.5, -9.2);
    mat(RGB(9, 16, 25), OK_R3D_EDGE);
    box(10.6, 4.2, -10.6, 11.4, 6.2, -9.2);
    mat(RGB(28, 24, 10), OK_R3D_EDGE);
    box(11.5, 4.2, -10.6, 12.6, 6.6, -9.2);
    mat(RGB(26, 15, 9), OK_R3D_EDGE);
    ok_r3d_cylinder(V(11, 8, -10), OK_FX(0.8), OK_FX(1.2), 8);
    mat(RGB(7, 18, 8), OK_R3D_EDGE);
    blob(11, 9.9, -10, 1.3, 1.1, 1.0);
    lamp(-3.5, -9.5, 6);
}

/* ------------------------------------------------------------ C: pocket sim */

static void sim(double x, double z)
{
    static const char *eye[] = {"#", "#"};
    static const char *mouth[] = {"###"};
    uint16_t skin = RGB(28, 20, 15), hair = RGB(6, 4, 4), top = RGB(26, 26, 27), jeans = RGB(8, 12, 21);
    face_mask m;
    int ex[2], ey[2], mx, my, i, head;
    ok_r3d_push();
    ok_r3d_translate(V(x, 0, z));
    ok_r3d_rotate_y(OK_TURN / 8);
    same(0, 0, OK_R3D_SHADOW);
    ok_r3d_disc(V(0, 0.05, 0), OK_FX(1.8), OK_FX(1.3), 12);
    mat(RGB(30, 30, 30), OK_R3D_EDGE);
    blob(-0.55, 0.3, 0.3, 0.45, 0.32, 0.8);
    blob(0.55, 0.3, 0.3, 0.45, 0.32, 0.8);
    mat(jeans, OK_R3D_EDGE);
    limb(-0.55, 0.6, 0, -0.5, 4.0, 0, 0.45);
    limb(0.55, 0.6, 0, 0.5, 4.0, 0, 0.45);
    blob(0, 4.2, 0, 1.15, 0.7, 0.75);
    mat(top, OK_R3D_EDGE);
    blob(0, 5.6, 0, 1.3, 1.65, 0.85);
    limb(-1.2, 6.6, 0, -1.55, 4.3, 0.2, 0.36);
    limb(1.2, 6.6, 0, 1.55, 4.3, 0.2, 0.36);
    same(RGB(20, 20, 22), next_id, OK_R3D_EDGE);
    blob(0, 4.9, 0.55, 0.8, 0.45, 0.4);
    mat(skin, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(-1.6, 4.0, 0.25), V(0.33, 0.38, 0.33), 6, 5);
    ok_r3d_ellipsoid(V(1.6, 4.0, 0.25), V(0.33, 0.38, 0.33), 6, 5);
    limb(0, 7.0, 0, 0, 7.6, 0, 0.35);
    head = mat(skin, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 8.6, 0.05), V(1.25, 1.45, 1.3), 10, 8);
    mat(hair, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 9.25, -0.25), V(1.4, 1.1, 1.4), 10, 7);
    blob(0.4, 9.65, 0.9, 0.9, 0.45, 0.55);
    for (i = 0; i < 2; i++) at(V(i ? 0.45 : -0.45, 8.65, 1.35), &ex[i], &ey[i]);
    at(V(0, 8.0, 1.3), &mx, &my);
    ok_r3d_pop();
    ok_r3d_finish();
    m.ids[0] = m.ids[1] = m.ids[2] = head;
    for (i = 0; i < 2; i++) sprite(&m, ex[i], ey[i], eye, 2, RGB(3, 2, 2), 0, 0);
    sprite(&m, mx, my, mouth, 1, RGB(16, 7, 6), 0, 0);
}

static void studio(void)
{
    int i;
    ok_r3d_camera(V(30, 24, 30), V(-1, 5, -1), 300);
    for (i = 0; i < 12; i++) {
        mat(i % 2 ? RGB(27, 22, 16) : RGB(28, 23, 17), 0);
        box(-12, -1, -12 + i * 2, 12, 0, -10 + i * 2);
    }
    mat(RGB(21, 27, 23), 0);
    box(-12.5, 0, -12.5, 12, 18, -12);
    mat(RGB(28, 26, 22), 0);
    box(-12.5, 0, -12, -12, 18, 12);
    mat(RGB(30, 30, 29), 0);
    box(-12, 0, -12, 12, 1, -11.7);
    box(-12, 0, -12, -11.7, 1, 12);
    /* Window on the left wall. */
    ok_r3d_push();
    ok_r3d_translate(V(-12, 0, 0));
    ok_r3d_rotate_y(OK_TURN / 4);
    window_square(0, 7, 6, 6.5, 0, RGB(30, 30, 29), RGB(26, 22, 28));
    ok_r3d_pop();
    /* Sofa, coffee table, TV, plant, floor lamp. */
    mat(RGB(26, 15, 22), 0);
    box(-11.5, 0, -1, 11.5, 0.05, 0);
    mat(RGB(23, 12, 10), OK_R3D_EDGE);
    box(-6, 0, -11.6, 4, 2.6, -7.5);
    box(-6, 2.6, -11.6, 4, 5.4, -10);
    box(-6.6, 0, -11.6, -5.4, 3.8, -7.5);
    box(3.4, 0, -11.6, 4.6, 3.8, -7.5);
    mat(RGB(27, 16, 13), OK_R3D_EDGE);
    blob(-3.2, 3.1, -9.2, 2.2, 0.6, 1.5);
    blob(1.2, 3.1, -9.2, 2.2, 0.6, 1.5);
    mat(RGB(30, 30, 29), OK_R3D_EDGE);
    box(-4, 0, -3.5, 2, 1.6, -0.5);
    mat(RGB(28, 24, 10), OK_R3D_EDGE);
    ok_r3d_cylinder(V(-2.5, 1.6, -2), OK_FX(0.4), OK_FX(0.6), 8);
    mat(RGB(23, 18, 13), OK_R3D_EDGE);
    box(-11.8, 0, 6, -9, 2.4, 11);
    mat(RGB(4, 4, 5), OK_R3D_EDGE);
    box(-11.4, 2.4, 6.5, -10.9, 6.5, 10.5);
    mat(when == NIGHT ? RGB(9, 15, 25) : RGB(7, 9, 12), OK_R3D_UNLIT);
    ok_r3d_quad(V(-10.85, 2.8, 6.8), V(-10.85, 2.8, 10.2), V(-10.85, 6.2, 10.2), V(-10.85, 6.2, 6.8));
    mat(RGB(30, 30, 29), OK_R3D_EDGE);
    ok_r3d_cylinder(V(8, 0, -10), OK_FX(1.1), OK_FX(2.2), 10);
    mat(RGB(8, 19, 11), OK_R3D_EDGE);
    blob(8, 4.2, -10, 1.6, 2.2, 1.6);
    lamp(-9.5, -9.5, 9);
}

/* -------------------------------------------------------------- D: peg doll */

static void peg(double x, double z)
{
    static const char *eye[] = {"##", "##"};
    static const char *mouth[] = {"#.#", ".#."};
    static const char *cheek[] = {"pp"};
    uint16_t wood = RGB(30, 23, 17), body = RGB(28, 14, 11), hair = RGB(9, 6, 5);
    face_mask m;
    int ex[2], ey[2], mx, my, i, head;
    ok_r3d_push();
    ok_r3d_translate(V(x, 0, z));
    ok_r3d_rotate_y(OK_TURN / 8);
    same(0, 0, OK_R3D_SHADOW);
    ok_r3d_disc(V(0, 0.05, 0), OK_FX(2.0), OK_FX(1.5), 12);
    mat(body, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 2.2, 0), V(1.8, 2.4, 1.8), 12, 8);
    ok_r3d_cylinder(V(0, 0, 0), OK_FX(1.55), OK_FX(1.0), 12);
    same(RGB(31, 28, 23), next_id, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 1.3, 0), V(1.72, 0.25, 1.72), 12, 4);
    mat(RGB(9, 18, 22), OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 4.25, 0), V(1.55, 0.55, 1.55), 12, 5);
    blob(0.8, 3.4, 1.15, 0.45, 0.9, 0.3);
    head = mat(wood, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 6.2, 0), V(2.05, 2.0, 2.0), 12, 9);
    mat(hair, OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(0, 6.75, -0.35), V(2.2, 1.9, 2.15), 12, 9);
    blob(0, 7.8, 1.1, 1.6, 0.7, 0.8);
    for (i = 0; i < 2; i++) at(V(i ? 0.7 : -0.7, 6.0, 2.0), &ex[i], &ey[i]);
    at(V(0, 5.2, 1.9), &mx, &my);
    ok_r3d_pop();
    ok_r3d_finish();
    m.ids[0] = m.ids[1] = m.ids[2] = head;
    for (i = 0; i < 2; i++) sprite(&m, ex[i], ey[i], eye, 2, RGB(4, 3, 3), 0, 0);
    sprite(&m, mx, my, mouth, 2, RGB(12, 4, 4), 0, 0);
    sprite(&m, ex[0] - 3, ey[0] + 3, cheek, 1, 0, 0, RGB(31, 17, 15));
    sprite(&m, ex[1] + 3, ey[1] + 3, cheek, 1, 0, 0, RGB(31, 17, 15));
}

static void diorama(void)
{
    ok_r3d_camera(V(34, 30, 34), V(0, 3, 0), 330);
    mat(RGB(29, 25, 21), OK_R3D_EDGE);
    box(-10, -2.5, -10, 10, 0, 10);
    mat(RGB(30, 21, 19), OK_R3D_EDGE);
    box(-10, 0, -10, 10, 14, -9);
    mat(RGB(28, 18, 17), OK_R3D_EDGE);
    box(-10, 0, -9, -9, 14, 10);
    /* Arched window. */
    ok_r3d_push();
    ok_r3d_translate(V(-8.95, 0, 1));
    ok_r3d_rotate_y(OK_TURN / 4);
    mat(sky(), OK_R3D_UNLIT);
    ok_r3d_quad(V(-2.5, 6, 0), V(2.5, 6, 0), V(2.5, 10, 0), V(-2.5, 10, 0));
    ok_r3d_push();
    ok_r3d_translate(V(0, 10, 0));
    ok_r3d_rotate_x(OK_TURN / 4);
    ok_r3d_disc(V(0, 0, 0), OK_FX(2.5), OK_FX(2.5), 16);
    ok_r3d_pop();
    ok_r3d_pop();
    mat(RGB(26, 26, 29), 0);
    ok_r3d_disc(V(2, 0.04, 0.5), OK_FX(4.5), OK_FX(4.5), 20);
    mat(RGB(30, 29, 27), OK_R3D_EDGE);
    ok_r3d_cylinder(V(-5, 0, 5), OK_FX(3.3), OK_FX(1.6), 16);
    mat(RGB(10, 16, 26), OK_R3D_EDGE);
    ok_r3d_ellipsoid(V(-4.6, 1.7, 5.4), V(3.1, 0.6, 2.6), 14, 5);
    mat(RGB(30, 30, 29), OK_R3D_EDGE);
    blob(-6.6, 2.2, 3.4, 0.9, 0.55, 1.5);
    mat(RGB(30, 30, 29), OK_R3D_EDGE);
    ok_r3d_cylinder(V(6.5, 0, -6.5), OK_FX(1.0), OK_FX(1.8), 10);
    mat(RGB(8, 19, 11), OK_R3D_EDGE);
    blob(6.5, 3.6, -6.5, 1.5, 2.0, 1.5);
    mat(RGB(24, 18, 12), OK_R3D_EDGE);
    box(-8, 0, -8.9, -4, 6, -6.6);
    lamp(5, 4.5 - 11, 5.5);
}

/* ---------------------------------------------------------------- the sheet */

static uint16_t sheet[(W * 2 + 24) * (H + 32) * 1];

static void label(uint16_t *canvas, int cw, int x, int y, const char *s, uint16_t c)
{
    const uint8_t (*font)[8] = ok_font();
    int row, col;
    for (; *s; s++, x += 8)
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (font[*s & 127][row] >> col & 1) canvas[(y + row) * cw + x + col] = c;
}

static void render(int look, int time)
{
    next_id = 0;
    iso = look >= 2;
    ok_r3d_begin(frame, CHARCOAL);
    lights(time);
    switch (look) {
    case 0: attic(); monkey(2.5, 3); break;
    case 1: cabin(); villager(2, 3); break;
    case 2: studio(); sim(2, 2); break;
    default: diorama(); peg(2.5, 0.5); break;
    }
}

int main(int argc, char **argv)
{
    static const char *names[4] = {"A  Monkey squatter, attic", "B  Little villager, log cabin",
                                   "C  Pocket sim, studio flat", "D  Peg doll, floating diorama"};
    static const char *files[4] = {"a-monkey", "b-villager", "c-sim", "d-peg"};
    const char *dir = argc > 1 ? argv[1] : "build/preview";
    int look, t, y, cw = W * 2 + 24, ch = H + 32;
    char path[512];
    for (look = 0; look < 4; look++) {
        int i;
        for (i = 0; i < cw * ch; i++) sheet[i] = CHARCOAL;
        for (t = 0; t < 2; t++) {
            render(look, t ? NIGHT : DAY);
            for (y = 0; y < H; y++) memcpy(sheet + (y + 24) * cw + 8 + t * (W + 8), frame + y * W, W * 2);
            printf("%s %s: %d triangles\n", files[look], t ? "night" : "day", ok_r3d_triangles());
        }
        label(sheet, cw, 8, 8, names[look], RGB(28, 27, 25));
        label(sheet, cw, cw - 8 - 8 * 11, 8, "day / night", RGB(16, 16, 16));
        snprintf(path, sizeof path, "%s/direction-%s.png", dir, files[look]);
        png_write(path, sheet, cw, ch, 2);
    }
    return 0;
}
