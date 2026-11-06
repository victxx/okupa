/* The buttons' icons: little low-poly toys, built from facets in code and
 * drawn once by the game's renderer at three times their size, outlined,
 * then shrunk to 32x32 so their edges come out soft. */
#include "okupa_icons.h"
#include "okupa_r3d.h"
#include <string.h>

#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define DEG(d) ((d) * OK_TURN / 360)
#define U(v) ((ok_fx)((v) * 4096 / 100))
#define OUTLINE RGB(5, 3, 3)
#define S 3                     /* drawn at three times the size */
#define BIG (OK_ICON_SIZE * S)  /* 96 pixels */
#define X0 ((OK_R3D_W - BIG) / 2)
#define Y0 ((OK_R3D_H - BIG) / 2)

uint16_t ok_icon_colour[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE];
uint8_t ok_icon_cover[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE];
static uint16_t scratch[OK_R3D_W * OK_R3D_H];
static uint8_t inked[BIG * BIG];
static int id;

static ok_v3 P(int x, int y, int z)
{
    return ok_v3_make(U(x), U(y), U(z));
}

static void mat(uint16_t c)
{
    ok_r3d_material(c, ++id, OK_R3D_EDGE | OK_R3D_FLAT);
}

static int cosr(int deg, int r)
{
    return (int)((int64_t)ok_r3d_cos(DEG(deg)) * r / 4096);
}

static int sinr(int deg, int r)
{
    return (int)((int64_t)ok_r3d_sin(DEG(deg)) * r / 4096);
}

static void gem(int x, int y, int z, int rx, int ry, int rz, int slices, int stacks)
{
    ok_r3d_ellipsoid(P(x, y, z), P(rx, ry, rz), slices, stacks);
}

/* --------------------------------------------------------------- models */

static void banana(void)
{
    int i;
    ok_r3d_rotate_y(DEG(-25));
    ok_r3d_rotate_x(DEG(15));
    mat(RGB(31, 25, 5));
    for (i = 0; i < 6; i++) { /* a fat curved tube of six-sided segments */
        int a0 = 205 + i * 22, a1 = a0 + 22, t = i < 3 ? i : 5 - i;
        ok_r3d_capsule(P(cosr(a0, 82), sinr(a0, 82) + 58, 0), P(cosr(a1, 82), sinr(a1, 82) + 58, 0), U(22 + t * 7), 6);
    }
    mat(RGB(14, 9, 3));
    ok_r3d_capsule(P(cosr(205, 82), sinr(205, 82) + 58, 0), P(cosr(195, 98), sinr(195, 98) + 70, 0), U(9), 5);
    gem(cosr(337, 82), sinr(337, 82) + 58, 0, 10, 10, 10, 5, 3);
}

static void beach_ball(void)
{
    static uint16_t stripes[8];
    static const uint16_t c[4] = {RGB(29, 7, 8), RGB(31, 30, 27), RGB(6, 13, 28), RGB(31, 30, 27)};
    int i;
    for (i = 0; i < 8; i++) stripes[i] = c[i / 2];
    ok_r3d_rotate_z(DEG(-20));
    ok_r3d_rotate_x(DEG(25));
    ok_r3d_rotate_y(DEG(25));
    ok_r3d_texture(stripes, 8);
    ok_r3d_uv(0, 0, 8, 1);
    ok_r3d_material(RGB(29, 7, 8), ++id, OK_R3D_EDGE | OK_R3D_FLAT | OK_R3D_TEXTURED);
    gem(0, 0, 0, 92, 92, 92, 12, 8);
    mat(RGB(31, 30, 27));
    gem(0, 88, 0, 28, 10, 28, 6, 2);
}

static void heart(void)
{
    static int16_t pts[20][2];
    int i;
    for (i = 0; i < 20; i++) { /* x = 16 sin^3 t, y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t */
        int t = i * 18, s = sinr(t, 1000);
        pts[i][0] = (int16_t)(16 * s / 1000 * s / 1000 * s / 1000 * 62 / 10);
        pts[i][1] = (int16_t)((13 * cosr(t, 1000) - 5 * cosr(2 * t, 1000) - 2 * cosr(3 * t, 1000) - cosr(4 * t, 1000)) * 62 / 10000 + 12);
    }
    ok_r3d_rotate_y(DEG(-22));
    ok_r3d_rotate_x(DEG(10));
    mat(RGB(30, 6, 9));
    ok_r3d_extrude((const int16_t (*)[2])pts, 20, U(30), 0, 10);
    mat(RGB(31, 24, 25));
    gem(-50, 52, 30, 16, 11, 6, 6, 3);
}

static void star(int x, int y, int r)
{
    int16_t pts[10][2];
    int i;
    for (i = 0; i < 10; i++) {
        int rr = i % 2 ? r * 45 / 100 : r;
        pts[i][0] = (int16_t)(x + cosr(90 + i * 36, rr));
        pts[i][1] = (int16_t)(y + sinr(90 + i * 36, rr));
    }
    ok_r3d_extrude((const int16_t (*)[2])pts, 10, U(10), x, y);
}

static void moon(void)
{
    int16_t outer[12][2], inner[12][2];
    int i;
    for (i = 0; i < 12; i++) {
        int a = 60 + i * 240 / 11, k = i < 6 ? i : 11 - i;
        outer[i][0] = (int16_t)cosr(a, 88) - 10;
        outer[i][1] = (int16_t)sinr(a, 88);
        int thick = sinr(i * 180 / 11, 52); /* nothing at the tips, fattest in the middle */
        (void)k;
        inner[i][0] = (int16_t)(outer[i][0] + thick);
        inner[i][1] = (int16_t)(outer[i][1] * (100 - thick / 4) / 100);
    }
    ok_r3d_rotate_y(DEG(-25));
    mat(RGB(31, 29, 14));
    ok_r3d_ribbon((const int16_t (*)[2])outer, (const int16_t (*)[2])inner, 12, U(26));
    mat(RGB(31, 30, 20));
    star(62, 58, 22);
    star(82, 6, 14);
}

static void sun(void)
{
    static int16_t pts[16][2];
    int i;
    ok_r3d_rotate_y(DEG(-20));
    ok_r3d_rotate_x(DEG(12));
    mat(RGB(31, 18, 3));
    for (i = 0; i < 8; i++) { /* rays: little extruded triangles */
        int16_t ray[3][2];
        int a = i * 45;
        ray[0][0] = (int16_t)cosr(a - 12, 60), ray[0][1] = (int16_t)sinr(a - 12, 60);
        ray[1][0] = (int16_t)cosr(a, 100), ray[1][1] = (int16_t)sinr(a, 100);
        ray[2][0] = (int16_t)cosr(a + 12, 60), ray[2][1] = (int16_t)sinr(a + 12, 60);
        ok_r3d_extrude((const int16_t (*)[2])ray, 3, U(14), cosr(a, 72), sinr(a, 72));
    }
    for (i = 0; i < 16; i++) pts[i][0] = (int16_t)cosr(i * 360 / 16, 62), pts[i][1] = (int16_t)sinr(i * 360 / 16, 62);
    mat(RGB(31, 26, 5));
    ok_r3d_extrude((const int16_t (*)[2])pts, 16, U(26), 0, 0);
    mat(RGB(8, 4, 2));
    gem(-22, 12, 27, 7, 9, 4, 5, 3);
    gem(22, 12, 27, 7, 9, 4, 5, 3);
    ok_r3d_capsule(P(-16, -16, 27), P(16, -16, 27), U(5), 4);
    mat(RGB(31, 15, 12));
    gem(-38, -6, 26, 10, 6, 3, 5, 3);
    gem(38, -6, 26, 10, 6, 3, 5, 3);
}

static void pencil(void)
{
    ok_r3d_rotate_z(DEG(-45));
    ok_r3d_rotate_y(DEG(25));
    mat(RGB(31, 24, 4));
    ok_r3d_cylinder(P(0, -48, 0), U(26), U(108), 6);
    mat(RGB(29, 22, 14));
    ok_r3d_cone(P(0, -48, 0), U(26), U(8), U(-36), 6);
    mat(RGB(5, 5, 6));
    ok_r3d_cone(P(0, -84, 0), U(8), U(1), U(-16), 6);
    mat(RGB(21, 22, 24));
    ok_r3d_cylinder(P(0, 60, 0), U(27), U(16), 6);
    mat(RGB(31, 16, 19));
    ok_r3d_cylinder(P(0, 76, 0), U(26), U(26), 6);
}

static void camera(void)
{
    ok_r3d_rotate_y(DEG(-24));
    ok_r3d_rotate_x(DEG(12));
    mat(RGB(25, 22, 18));
    ok_r3d_box(P(-92, -10, -34), P(92, 56, 34));
    mat(RGB(9, 9, 11));
    ok_r3d_box(P(-92, -58, -34), P(92, -10, 34));
    mat(RGB(10, 10, 12));
    ok_r3d_box(P(-40, 56, -24), P(30, 76, 24));
    mat(RGB(29, 8, 7));
    ok_r3d_box(P(52, 56, -12), P(76, 66, 12));
    ok_r3d_push();
    ok_r3d_translate(P(0, 2, 34));
    ok_r3d_rotate_x(DEG(90));
    mat(RGB(5, 5, 6));
    ok_r3d_cylinder(P(0, 0, 0), U(48), U(24), 8);
    mat(RGB(12, 15, 24));
    ok_r3d_cone(P(0, 24, 0), U(34), U(26), U(6), 8);
    ok_r3d_pop();
    mat(RGB(31, 31, 28));
    gem(-12, 18, 66, 9, 9, 4, 5, 3);
}

static void envelope(void)
{
    static const int16_t flap[3][2] = {{-88, 58}, {88, 58}, {0, -6}};
    static int16_t seal[12][2];
    int i;
    ok_r3d_rotate_y(DEG(-24));
    ok_r3d_rotate_z(DEG(-6));
    mat(RGB(30, 28, 23));
    ok_r3d_box(P(-92, -62, -10), P(92, 62, 10));
    ok_r3d_push();
    ok_r3d_translate(P(0, 0, 14));
    mat(RGB(27, 24, 19));
    ok_r3d_extrude(flap, 3, U(4), 0, 30);
    for (i = 0; i < 12; i++) {
        int t = i * 30, s = sinr(t, 1000);
        seal[i][0] = (int16_t)(16 * s / 1000 * s / 1000 * s / 1000 * 18 / 10);
        seal[i][1] = (int16_t)((13 * cosr(t, 1000) - 5 * cosr(2 * t, 1000) - 2 * cosr(3 * t, 1000) - cosr(4 * t, 1000)) * 18 / 10000 - 8);
    }
    ok_r3d_translate(P(0, 0, 6));
    mat(RGB(29, 5, 8));
    ok_r3d_extrude((const int16_t (*)[2])seal, 12, U(5), 0, -6);
    ok_r3d_pop();
}

static void gear(void)
{
    static int16_t pts[32][2];
    int i;
    for (i = 0; i < 32; i++) { /* eight teeth: out, out, in, in */
        int r = i % 4 < 2 ? 92 : 68, a = i * 360 / 32 + (i % 4 == 0 || i % 4 == 2 ? 3 : -3);
        pts[i][0] = (int16_t)cosr(a, r);
        pts[i][1] = (int16_t)sinr(a, r);
    }
    ok_r3d_rotate_y(DEG(-28));
    ok_r3d_rotate_x(DEG(18));
    mat(RGB(19, 20, 22));
    ok_r3d_extrude((const int16_t (*)[2])pts, 32, U(24), 0, 0);
    ok_r3d_push();
    ok_r3d_rotate_x(DEG(90));
    mat(RGB(6, 6, 7));
    ok_r3d_cylinder(P(0, -26, 0), U(28), U(52), 8);
    ok_r3d_pop();
}

static void shirt(void)
{
    static const int16_t pts[10][2] = {{-34, 70}, {-96, 40}, {-74, -2}, {-52, 10}, {-52, -74},
                                       {52, -74}, {52, 10}, {74, -2}, {96, 40}, {34, 70}};
    ok_r3d_rotate_y(DEG(-22));
    ok_r3d_rotate_x(DEG(8));
    mat(RGB(31, 23, 4));
    ok_r3d_extrude(pts, 10, U(20), 0, 0);
    mat(RGB(22, 14, 2));
    gem(0, 66, 18, 30, 12, 6, 8, 3);
}

static void house(void)
{
    static const int16_t roof[3][2] = {{-90, 20}, {90, 20}, {0, 92}};
    ok_r3d_rotate_y(DEG(-30));
    ok_r3d_rotate_x(DEG(14));
    mat(RGB(24, 16, 9));
    ok_r3d_box(P(-70, -80, -55), P(70, 20, 55));
    mat(RGB(28, 11, 7));
    ok_r3d_extrude(roof, 3, U(64), 0, 44);
    mat(RGB(10, 6, 3));
    ok_r3d_box(P(-22, -80, 55), P(16, -22, 60));
    mat(RGB(30, 27, 12));
    ok_r3d_box(P(30, -36, 55), P(56, -10, 60));
}

static void note(void)
{
    ok_r3d_rotate_y(DEG(-20));
    ok_r3d_rotate_z(DEG(8));
    mat(RGB(31, 17, 5));
    gem(-48, -58, 0, 32, 24, 22, 8, 5);
    gem(54, -42, 0, 32, 24, 22, 8, 5);
    ok_r3d_box(P(-24, -58, -10), P(-10, 66, 10));
    ok_r3d_box(P(78, -42, -10), P(92, 80, 10));
    ok_r3d_push();
    ok_r3d_translate(P(34, 74, 0));
    ok_r3d_rotate_z(DEG(8));
    ok_r3d_box(P(-60, -12, -12), P(60, 12, 12));
    ok_r3d_pop();
}

/* ------------------------------------------------------------ the sheet */

void ok_icons_build(void)
{
    static void (*const model[OK_ICONS])(void) = {banana, beach_ball, heart, moon, sun, pencil,
                                                  camera, envelope, gear, shirt, house, note};
    int i, x, y, k, pass;
    for (i = 0; i < OK_ICONS; i++) {
        id = 0;
        ok_r3d_begin(scratch, 0x8000);
        /* 2.4 units fill the 96 pixels. */
        ok_r3d_camera(OK_V3(0, 0, 12), OK_V3(0, 0, 0), 480);
        ok_r3d_light(OK_V3(-0.55, 0.85, 0.75), OK_V3(0.5, 0.48, 0.42), OK_V3(0.66, 0.65, 0.7));
        ok_r3d_push();
        model[i]();
        ok_r3d_pop();
        ok_r3d_finish();
        /* A dark outline three pixels thick around the whole toy. */
        for (y = 0; y < BIG; y++)
            for (x = 0; x < BIG; x++) inked[y * BIG + x] = ok_r3d_id_at(X0 + x, Y0 + y) != 0;
        for (pass = 0; pass < S; pass++) {
            static uint8_t grow[BIG * BIG];
            memcpy(grow, inked, sizeof grow);
            for (y = 0; y < BIG; y++)
                for (x = 0; x < BIG; x++) {
                    k = y * BIG + x;
                    if (inked[k]) continue;
                    if ((x && inked[k - 1]) || (x < BIG - 1 && inked[k + 1]) || (y && inked[k - BIG]) ||
                        (y < BIG - 1 && inked[k + BIG])) {
                        grow[k] = 2;
                        scratch[(Y0 + y) * OK_R3D_W + X0 + x] = OUTLINE;
                    }
                }
            memcpy(inked, grow, sizeof grow);
        }
        for (y = 0; y < OK_ICON_SIZE; y++)
            for (x = 0; x < OK_ICON_SIZE; x++) {
                int r = 0, g = 0, b = 0, n = 0, dx, dy;
                for (dy = 0; dy < S; dy++)
                    for (dx = 0; dx < S; dx++) {
                        int sx = x * S + dx, sy = y * S + dy;
                        uint16_t c = scratch[(Y0 + sy) * OK_R3D_W + X0 + sx];
                        if (!inked[sy * BIG + sx]) continue;
                        r += c & 31, g += c >> 5 & 31, b += c >> 10 & 31, n++;
                    }
                ok_icon_cover[i][y * OK_ICON_SIZE + x] = (uint8_t)((n * 4 + S * S / 2) / (S * S));
                ok_icon_colour[i][y * OK_ICON_SIZE + x] = n ? RGB(r / n, g / n, b / n) : 0;
            }
    }
}

void ok_icons_draw(int icon, uint16_t *frame, int cx, int cy, int size)
{
    int x, y, step = size >= OK_ICON_SIZE ? 1 : 2;
    if (icon < 0 || icon >= OK_ICONS) return;
    size = OK_ICON_SIZE / step;
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++) {
            int fx = cx - size / 2 + x, fy = cy - size / 2 + y, n = 0, r = 0, g = 0, b = 0, dx, dy;
            uint16_t d;
            if (fx < 0 || fy < 0 || fx >= 256 || fy >= 192) continue;
            /* At half size, each pixel is the average of four. */
            for (dy = 0; dy < step; dy++)
                for (dx = 0; dx < step; dx++) {
                    int k = (y * step + dy) * OK_ICON_SIZE + x * step + dx, a = ok_icon_cover[icon][k];
                    uint16_t c = ok_icon_colour[icon][k];
                    n += a;
                    r += (c & 31) * a, g += (c >> 5 & 31) * a, b += (c >> 10 & 31) * a;
                }
            if (!n) continue;
            d = frame[fy * 256 + fx];
            r = r / n, g = g / n, b = b / n;
            n /= step * step;
            frame[fy * 256 + fx] = RGB((r * n + (d & 31) * (4 - n)) / 4, (g * n + (d >> 5 & 31) * (4 - n)) / 4,
                                       (b * n + (d >> 10 & 31) * (4 - n)) / 4);
        }
}
