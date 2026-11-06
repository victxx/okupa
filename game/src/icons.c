#include "okupa_icons.h"
#include "okupa_r3d.h"
#include <string.h>

#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define DEG(d) ((d) * OK_TURN / 360)
#define U(v) ((ok_fx)((v) * 4096 / 100))

static uint16_t scratch[OK_R3D_W * OK_R3D_H];
static int id;

static ok_v3 P(int x, int y, int z)
{
    return ok_v3_make(U(x), U(y), U(z));
}

static void mat(uint16_t c, int flags)
{
    ok_r3d_material(c, ++id, flags | OK_R3D_EDGE);
}

static void ball(int x, int y, int z, int r)
{
    ok_r3d_ellipsoid(P(x, y, z), P(r, r, r), 12, 8);
}

static void stick(int x0, int y0, int x1, int y1, int r)
{
    ok_r3d_capsule(P(x0, y0, 0), P(x1, y1, 0), U(r), 8);
}

static void banana(void)
{
    int i;
    ok_r3d_rotate_z(DEG(-20));
    mat(RGB(31, 26, 6), 0);
    for (i = 0; i < 6; i++) {
        int a0 = DEG(200 + i * 24), a1 = DEG(200 + (i + 1) * 24), r0 = 18 + (i < 3 ? i : 5 - i) * 6, r1 = 18 + (i + 1 < 3 ? i + 1 : 4 - i) * 6;
        int x0 = (int)((int64_t)ok_r3d_cos(a0) * 80 / 4096), y0 = (int)((int64_t)ok_r3d_sin(a0) * 80 / 4096) + 60;
        int x1 = (int)((int64_t)ok_r3d_cos(a1) * 80 / 4096), y1 = (int)((int64_t)ok_r3d_sin(a1) * 80 / 4096) + 60;
        stick(x0, y0, x1, y1, r0 > r1 ? r0 : r1);
    }
    mat(RGB(12, 8, 3), 0);
    stick(-80, 60, -92, 78, 9);
    ball(72, 48, 0, 9);
}

static void beach_ball(void)
{
    ok_r3d_rotate_z(DEG(-25));
    ok_r3d_rotate_x(DEG(20));
    mat(RGB(29, 8, 8), 0);
    ball(0, 0, 0, 85);
    mat(RGB(31, 30, 28), 0);
    ok_r3d_ellipsoid(P(0, 0, 0), P(87, 28, 87), 14, 4);
    mat(RGB(7, 13, 28), 0);
    ok_r3d_ellipsoid(P(0, 66, 0), P(56, 24, 56), 12, 4);
}

static void heart(void)
{
    mat(RGB(30, 7, 10), 0);
    ball(-42, 24, 0, 52);
    ball(42, 24, 0, 52);
    ok_r3d_push();
    ok_r3d_translate(P(0, -18, 0));
    ok_r3d_rotate_z(DEG(45));
    ok_r3d_box(P(-58, -58, -34), P(58, 58, 34));
    ok_r3d_pop();
    mat(RGB(31, 25, 26), OK_R3D_UNLIT);
    ok_r3d_ellipsoid(P(-52, 42, 44), P(16, 12, 8), 6, 4);
}

static void moon(void)
{
    int i;
    mat(RGB(30, 27, 12), 0);
    for (i = 0; i < 8; i++) {
        int a0 = DEG(70 + i * 28), a1 = DEG(70 + (i + 1) * 28), t = i < 4 ? i : 7 - i;
        stick((int)((int64_t)ok_r3d_cos(a0) * 62 / 4096), (int)((int64_t)ok_r3d_sin(a0) * 70 / 4096),
              (int)((int64_t)ok_r3d_cos(a1) * 62 / 4096), (int)((int64_t)ok_r3d_sin(a1) * 70 / 4096), 12 + t * 9);
    }
    mat(RGB(31, 31, 26), OK_R3D_UNLIT);
    ball(62, 56, 0, 10);
    ball(80, 22, 0, 7);
}

static void sun(void)
{
    int i;
    mat(RGB(31, 20, 4), 0);
    for (i = 0; i < 8; i++) {
        int a = DEG(i * 45);
        stick((int)((int64_t)ok_r3d_cos(a) * 58 / 4096), (int)((int64_t)ok_r3d_sin(a) * 58 / 4096),
              (int)((int64_t)ok_r3d_cos(a) * 88 / 4096), (int)((int64_t)ok_r3d_sin(a) * 88 / 4096), 11);
    }
    mat(RGB(31, 27, 6), 0);
    ball(0, 0, 10, 54);
}

static void pencil(void)
{
    ok_r3d_rotate_z(DEG(-45));
    mat(RGB(31, 24, 4), 0);
    ok_r3d_cylinder(P(0, -55, 0), U(22), U(115), 6);
    mat(RGB(29, 22, 15), 0);
    ok_r3d_capsule(P(0, -55, 0), P(0, -80, 0), U(18), 6);
    mat(RGB(6, 6, 7), 0);
    ball(0, -96, 0, 9);
    mat(RGB(20, 21, 23), 0);
    ok_r3d_cylinder(P(0, 60, 0), U(24), U(16), 8);
    mat(RGB(31, 17, 20), 0);
    ok_r3d_cylinder(P(0, 76, 0), U(23), U(22), 8);
}

static void camera(void)
{
    ok_r3d_rotate_y(DEG(-18));
    ok_r3d_rotate_x(DEG(8));
    mat(RGB(9, 9, 11), 0);
    ok_r3d_box(P(-90, -55, -30), P(90, 52, 30));
    mat(RGB(20, 21, 23), 0);
    ok_r3d_box(P(-36, 52, -22), P(26, 70, 22));
    mat(RGB(29, 8, 7), 0);
    ok_r3d_box(P(52, 52, -10), P(74, 62, 10));
    ok_r3d_push();
    ok_r3d_translate(P(0, -2, 30));
    ok_r3d_rotate_x(DEG(90));
    mat(RGB(4, 4, 5), 0);
    ok_r3d_cylinder(P(0, 0, 0), U(40), U(22), 12);
    mat(RGB(10, 18, 29), OK_R3D_UNLIT);
    ok_r3d_disc(P(0, 23, 0), U(26), U(26), 12);
    ok_r3d_pop();
}

static void envelope(void)
{
    ok_r3d_rotate_y(DEG(-15));
    ok_r3d_rotate_z(DEG(-8));
    mat(RGB(31, 30, 27), 0);
    ok_r3d_box(P(-90, -60, -8), P(90, 60, 8));
    mat(RGB(25, 24, 21), OK_R3D_BOTH);
    ok_r3d_quad(P(-88, 58, 10), P(0, -8, 10), P(88, 58, 10), P(88, 58, 10));
    mat(RGB(29, 6, 9), 0);
    ok_r3d_ellipsoid(P(0, -6, 14), P(16, 16, 6), 8, 4);
}

static void gear(void)
{
    int i;
    ok_r3d_rotate_x(DEG(70));
    mat(RGB(18, 20, 22), 0);
    ok_r3d_cylinder(P(0, -14, 0), U(62), U(28), 12);
    for (i = 0; i < 8; i++) {
        ok_r3d_push();
        ok_r3d_rotate_y(DEG(i * 45));
        ok_r3d_box(P(54, -14, -14), P(86, 14, 14));
        ok_r3d_pop();
    }
    mat(RGB(6, 6, 7), 0);
    ok_r3d_disc(P(0, 15, 0), U(22), U(22), 10);
}

static void shirt(void)
{
    mat(RGB(31, 26, 3), 0);
    ok_r3d_box(P(-50, -70, -18), P(50, 50, 18));
    ok_r3d_push();
    ok_r3d_translate(P(-50, 40, 0));
    ok_r3d_rotate_z(DEG(-40));
    ok_r3d_box(P(-50, -22, -16), P(0, 22, 16));
    ok_r3d_pop();
    ok_r3d_push();
    ok_r3d_translate(P(50, 40, 0));
    ok_r3d_rotate_z(DEG(40));
    ok_r3d_box(P(0, -22, -16), P(50, 22, 16));
    ok_r3d_pop();
    mat(RGB(18, 14, 2), 0);
    ok_r3d_ellipsoid(P(0, 50, 18), P(24, 12, 4), 8, 3);
}

void ok_icons_build(ok_icons *icons)
{
    static void (*const model[OK_ICONS])(void) = {banana, beach_ball, heart, moon, sun, pencil, camera, envelope, gear, shirt};
    int i, x, y;
    memset(icons, 0, sizeof *icons);
    for (i = 0; i < OK_ICONS; i++) {
        id = 0;
        ok_r3d_begin(scratch, 0x8000);
        /* 2.4 units fill 64 pixels: the icon at twice its size, then halved. */
        ok_r3d_camera(OK_V3(0, 0, 10), OK_V3(0, 0, 0), 266);
        ok_r3d_light(OK_V3(-0.6, 0.9, 0.8), OK_V3(0.5, 0.48, 0.44), OK_V3(0.6, 0.6, 0.64));
        ok_r3d_push();
        model[i]();
        ok_r3d_pop();
        ok_r3d_finish();
        for (y = 0; y < OK_ICON_SIZE; y++)
            for (x = 0; x < OK_ICON_SIZE; x++) {
                int r = 0, g = 0, b = 0, n = 0, dx, dy;
                for (dy = 0; dy < 2; dy++)
                    for (dx = 0; dx < 2; dx++) {
                        int sx = 96 + x * 2 + dx, sy = 64 + y * 2 + dy;
                        uint16_t c = scratch[sy * OK_R3D_W + sx];
                        if (!ok_r3d_id_at(sx, sy)) continue;
                        r += c & 31, g += c >> 5 & 31, b += c >> 10 & 31, n++;
                    }
                icons->cover[i][y * OK_ICON_SIZE + x] = (uint8_t)n;
                if (n) icons->colour[i][y * OK_ICON_SIZE + x] = RGB(r / n, g / n, b / n);
            }
    }
}

void ok_icons_draw(const ok_icons *icons, int icon, uint16_t *frame, int cx, int cy, int size)
{
    int x, y, step = OK_ICON_SIZE / size;
    if (icon < 0 || icon >= OK_ICONS || step < 1) return;
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++) {
            int fx = cx - size / 2 + x, fy = cy - size / 2 + y, k = (y * step) * OK_ICON_SIZE + x * step, n = icons->cover[icon][k];
            uint16_t c = icons->colour[icon][k], d;
            if (!n || fx < 0 || fy < 0 || fx >= OK_R3D_W || fy >= OK_R3D_H) continue;
            d = frame[fy * OK_R3D_W + fx];
            frame[fy * OK_R3D_W + fx] =
                RGB(((c & 31) * n + (d & 31) * (4 - n)) / 4, ((c >> 5 & 31) * n + (d >> 5 & 31) * (4 - n)) / 4,
                    ((c >> 10 & 31) * n + (d >> 10 & 31) * (4 - n)) / 4);
        }
}
