#include "okupa_r3d.h"
#include <string.h>

#define W OK_R3D_W
#define H OK_R3D_H
#define NEAR 4096           /* nothing closer than 1.0 to the camera */
#define GUARD (4096 * 16)   /* screen coordinates beyond this many pixels are refused */
#define MAX_VERTS 400
#define STACK 16

/* sin over a quarter turn, 256 steps, Q12. */
static const int16_t quarter[257] = {
    0, 25, 50, 75, 101, 126, 151, 176, 201, 226, 251, 276, 301, 326, 351, 376,
    401, 426, 451, 476, 501, 526, 551, 576, 601, 626, 651, 675, 700, 725, 750, 774,
    799, 824, 848, 873, 897, 922, 946, 971, 995, 1020, 1044, 1068, 1092, 1117, 1141, 1165,
    1189, 1213, 1237, 1261, 1285, 1309, 1332, 1356, 1380, 1404, 1427, 1451, 1474, 1498, 1521, 1544,
    1567, 1591, 1614, 1637, 1660, 1683, 1706, 1729, 1751, 1774, 1797, 1819, 1842, 1864, 1886, 1909,
    1931, 1953, 1975, 1997, 2019, 2041, 2062, 2084, 2106, 2127, 2149, 2170, 2191, 2213, 2234, 2255,
    2276, 2296, 2317, 2338, 2359, 2379, 2399, 2420, 2440, 2460, 2480, 2500, 2520, 2540, 2559, 2579,
    2598, 2618, 2637, 2656, 2675, 2694, 2713, 2732, 2751, 2769, 2788, 2806, 2824, 2843, 2861, 2878,
    2896, 2914, 2932, 2949, 2967, 2984, 3001, 3018, 3035, 3052, 3068, 3085, 3102, 3118, 3134, 3150,
    3166, 3182, 3198, 3214, 3229, 3244, 3260, 3275, 3290, 3305, 3320, 3334, 3349, 3363, 3378, 3392,
    3406, 3420, 3433, 3447, 3461, 3474, 3487, 3500, 3513, 3526, 3539, 3551, 3564, 3576, 3588, 3600,
    3612, 3624, 3636, 3647, 3659, 3670, 3681, 3692, 3703, 3713, 3724, 3734, 3745, 3755, 3765, 3775,
    3784, 3794, 3803, 3812, 3822, 3831, 3839, 3848, 3857, 3865, 3873, 3881, 3889, 3897, 3905, 3912,
    3920, 3927, 3934, 3941, 3948, 3954, 3961, 3967, 3973, 3979, 3985, 3991, 3996, 4002, 4007, 4012,
    4017, 4022, 4027, 4031, 4036, 4040, 4044, 4048, 4052, 4055, 4059, 4062, 4065, 4068, 4071, 4074,
    4076, 4079, 4081, 4083, 4085, 4087, 4088, 4090, 4091, 4092, 4093, 4094, 4095, 4095, 4096, 4096,
    4096,
};

typedef struct {
    ok_fx m[3][3]; /* rows */
    ok_v3 t;
} xform;

typedef struct {
    int32_t x, y;  /* screen, 1/16 pixel */
    int32_t z;     /* 2^34 / depth: larger is nearer */
    int32_t l;     /* light, Q12 */
    int ok;
    ok_v3 w, n;    /* world position and normal */
} vert;

static uint16_t *fb;
static int32_t zbuf[W * H];
static uint8_t ids[W * H];
static uint8_t edge_on[256];
static uint16_t edge_colour[256];
static struct {
    ok_v3 eye, r, u, f;
    int32_t focal;
} cam;
static ok_v3 to_light, light_rgb, ambient_rgb;
static xform stack[STACK];
static int sp;
static uint16_t tone[4];
static int cur_id, cur_flags;
static vert vb[MAX_VERTS];
static int triangles;

ok_fx ok_fx_mul(ok_fx a, ok_fx b)
{
    return (ok_fx)(((int64_t)a * b) / 4096);
}

static ok_fx quarter_at(int i) /* i in 0..1024 */
{
    int k = i >> 2, f = i & 3;
    return k >= 256 ? 4096 : quarter[k] + (quarter[k + 1] - quarter[k]) * f / 4;
}

ok_fx ok_r3d_sin(int angle)
{
    int a = angle & (OK_TURN - 1), q = a >> 10, i = a & 1023;
    switch (q) {
    case 0: return quarter_at(i);
    case 1: return quarter_at(1024 - i);
    case 2: return -quarter_at(i);
    default: return -quarter_at(1024 - i);
    }
}

ok_fx ok_r3d_cos(int angle)
{
    return ok_r3d_sin(angle + OK_TURN / 4);
}

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

static ok_v3 add(ok_v3 a, ok_v3 b) { return ok_v3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static ok_v3 sub(ok_v3 a, ok_v3 b) { return ok_v3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static ok_v3 scale(ok_v3 a, ok_fx s) { return ok_v3_make(ok_fx_mul(a.x, s), ok_fx_mul(a.y, s), ok_fx_mul(a.z, s)); }
static ok_fx dot(ok_v3 a, ok_v3 b)
{
    return (ok_fx)(((int64_t)a.x * b.x + (int64_t)a.y * b.y + (int64_t)a.z * b.z) / 4096);
}
static ok_v3 cross(ok_v3 a, ok_v3 b)
{
    return ok_v3_make(ok_fx_mul(a.y, b.z) - ok_fx_mul(a.z, b.y), ok_fx_mul(a.z, b.x) - ok_fx_mul(a.x, b.z),
                      ok_fx_mul(a.x, b.y) - ok_fx_mul(a.y, b.x));
}
static ok_v3 norm(ok_v3 a)
{
    uint32_t len = isqrt64((uint64_t)((int64_t)a.x * a.x + (int64_t)a.y * a.y + (int64_t)a.z * a.z));
    if (!len) return ok_v3_make(0, 4096, 0);
    return ok_v3_make((ok_fx)((int64_t)a.x * 4096 / len), (ok_fx)((int64_t)a.y * 4096 / len),
                      (ok_fx)((int64_t)a.z * 4096 / len));
}

static ok_v3 rot(const xform *x, ok_v3 v)
{
    return ok_v3_make(dot(ok_v3_make(x->m[0][0], x->m[0][1], x->m[0][2]), v),
                      dot(ok_v3_make(x->m[1][0], x->m[1][1], x->m[1][2]), v),
                      dot(ok_v3_make(x->m[2][0], x->m[2][1], x->m[2][2]), v));
}

static uint16_t rgb(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 31 ? 31 : r;
    g = g < 0 ? 0 : g > 31 ? 31 : g;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (uint16_t)(0x8000u | r | g << 5 | b << 10);
}

void ok_r3d_begin(uint16_t *frame, uint16_t background)
{
    int i;
    fb = frame;
    for (i = 0; i < W * H; i++) fb[i] = background;
    memset(zbuf, 0, sizeof zbuf);
    memset(ids, 0, sizeof ids);
    memset(edge_on, 0, sizeof edge_on);
    memset(stack, 0, sizeof stack);
    sp = 0;
    stack[0].m[0][0] = stack[0].m[1][1] = stack[0].m[2][2] = 4096;
    triangles = 0;
    ok_r3d_light(ok_v3_make(0, 4096, 0), ok_v3_make(4096, 4096, 4096), ok_v3_make(1024, 1024, 1024));
    ok_r3d_material(0xffff, 1, 0);
}

void ok_r3d_camera(ok_v3 eye, ok_v3 target, int focal_px)
{
    cam.eye = eye;
    cam.f = norm(sub(target, eye));
    cam.r = norm(ok_v3_make(-cam.f.z, 0, cam.f.x));
    cam.u = cross(cam.r, cam.f);
    cam.focal = focal_px;
}

void ok_r3d_light(ok_v3 light_dir, ok_v3 light, ok_v3 ambient)
{
    to_light = norm(light_dir);
    light_rgb = light;
    ambient_rgb = ambient;
}

/* Four tones from shadow to full light; the outline is a darker shadow. */
void ok_r3d_material(uint16_t colour, int id, int flags)
{
    int k, r = colour & 31, g = colour >> 5 & 31, b = colour >> 10 & 31;
    for (k = 0; k < 4; k++) {
        if (flags & OK_R3D_UNLIT) {
            tone[k] = colour | 0x8000;
            continue;
        }
        tone[k] = rgb((int)((int64_t)r * (ambient_rgb.x + light_rgb.x * k / 3) / 4096),
                      (int)((int64_t)g * (ambient_rgb.y + light_rgb.y * k / 3) / 4096),
                      (int)((int64_t)b * (ambient_rgb.z + light_rgb.z * k / 3) / 4096));
    }
    cur_id = id & 255;
    cur_flags = flags;
    if (flags & OK_R3D_EDGE) {
        uint16_t t = tone[0];
        edge_on[cur_id] = 1;
        edge_colour[cur_id] = rgb((t & 31) * 9 / 20, (t >> 5 & 31) * 9 / 20, (t >> 10 & 31) * 9 / 20 + 1);
    }
}

void ok_r3d_push(void)
{
    if (sp + 1 < STACK) {
        stack[sp + 1] = stack[sp];
        sp++;
    }
}

void ok_r3d_pop(void)
{
    if (sp) sp--;
}

void ok_r3d_translate(ok_v3 t)
{
    stack[sp].t = add(stack[sp].t, rot(&stack[sp], t));
}

/* m = m * r, with r given by rows. */
static void mul_rot(const ok_fx r[3][3])
{
    ok_fx o[3][3];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            o[i][j] = ok_fx_mul(stack[sp].m[i][0], r[0][j]) + ok_fx_mul(stack[sp].m[i][1], r[1][j]) +
                      ok_fx_mul(stack[sp].m[i][2], r[2][j]);
    memcpy(stack[sp].m, o, sizeof o);
}

void ok_r3d_rotate_x(int a)
{
    ok_fx s = ok_r3d_sin(a), c = ok_r3d_cos(a);
    const ok_fx r[3][3] = {{4096, 0, 0}, {0, c, -s}, {0, s, c}};
    mul_rot(r);
}

void ok_r3d_rotate_y(int a)
{
    ok_fx s = ok_r3d_sin(a), c = ok_r3d_cos(a);
    const ok_fx r[3][3] = {{c, 0, s}, {0, 4096, 0}, {-s, 0, c}};
    mul_rot(r);
}

void ok_r3d_rotate_z(int a)
{
    ok_fx s = ok_r3d_sin(a), c = ok_r3d_cos(a);
    const ok_fx r[3][3] = {{c, -s, 0}, {s, c, 0}, {0, 0, 4096}};
    mul_rot(r);
}

/* Model space to screen, with the light at the vertex. */
static void put(int k, ok_v3 p, ok_v3 n)
{
    vert *v = &vb[k];
    ok_v3 d;
    ok_fx vx, vy, vz, ndl, up;
    int64_t sx, sy;
    v->w = add(rot(&stack[sp], p), stack[sp].t);
    v->n = rot(&stack[sp], n);
    d = sub(v->w, cam.eye);
    vx = dot(d, cam.r);
    vy = dot(d, cam.u);
    vz = dot(d, cam.f);
    v->ok = 0;
    if (vz < NEAR) return;
    sx = (int64_t)W * 8 + (int64_t)vx * cam.focal * 16 / vz;
    sy = (int64_t)H * 8 - (int64_t)vy * cam.focal * 16 / vz;
    if (sx < -GUARD || sx > GUARD || sy < -GUARD || sy > GUARD) return;
    v->x = (int32_t)sx;
    v->y = (int32_t)sy;
    v->z = (int32_t)(((int64_t)1 << 34) / vz);
    ndl = dot(v->n, to_light);
    up = (v->n.y + 4096) / 2;
    v->l = (ndl > 0 ? ndl * 4 / 5 : 0) + up / 5;
    v->ok = 1;
}

static int tone_of(int32_t l)
{
    return l < 1100 ? 0 : l < 2200 ? 1 : l < 3300 ? 2 : 3;
}

static uint16_t darker(uint16_t c)
{
    return (uint16_t)(0x8000u | ((c >> 1 & 0x3def) + (c >> 2 & 0x1ce7)));
}

/* Value of an attribute at the weights (wa, wb, wc), with 8 fraction bits. */
static int64_t lerp8(int64_t wa, int64_t wb, int64_t wc, int32_t a, int32_t b, int32_t c, int64_t area)
{
    int64_t s = wa * a + wb * b + wc * c;
    return (s / area) * 256 + (s % area) * 256 / area;
}

static void raster(const vert *a, const vert *b, const vert *c)
{
    int64_t area = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(b->y - a->y) * (c->x - a->x);
    int32_t minx, maxx, miny, maxy, x, y, x0, x1, y0, y1;
    int64_t dax, dbx, dcx, day, dby, dcy, wa0, wb0, wc0;
    int64_t dzdx, dldx;
    if (area == 0) return;
    if (area > 0) {
        if (!(cur_flags & OK_R3D_BOTH)) return;
    } else {
        const vert *t = b;
        b = c;
        c = t;
        area = -area;
    }
    minx = a->x < b->x ? (a->x < c->x ? a->x : c->x) : (b->x < c->x ? b->x : c->x);
    maxx = a->x > b->x ? (a->x > c->x ? a->x : c->x) : (b->x > c->x ? b->x : c->x);
    miny = a->y < b->y ? (a->y < c->y ? a->y : c->y) : (b->y < c->y ? b->y : c->y);
    maxy = a->y > b->y ? (a->y > c->y ? a->y : c->y) : (b->y > c->y ? b->y : c->y);
    x0 = (minx - 8 + 15) >> 4;
    x1 = (maxx - 8) >> 4;
    y0 = (miny - 8 + 15) >> 4;
    y1 = (maxy - 8) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    triangles++;
    /* Edge functions at the first pixel centre and their steps. */
    {
        int64_t px = (int64_t)x0 * 16 + 8, py = (int64_t)y0 * 16 + 8;
        wa0 = (int64_t)(c->x - b->x) * (py - b->y) - (int64_t)(c->y - b->y) * (px - b->x);
        wb0 = (int64_t)(a->x - c->x) * (py - c->y) - (int64_t)(a->y - c->y) * (px - c->x);
        wc0 = (int64_t)(b->x - a->x) * (py - a->y) - (int64_t)(b->y - a->y) * (px - a->x);
        dax = -(int64_t)(c->y - b->y) * 16;
        dbx = -(int64_t)(a->y - c->y) * 16;
        dcx = -(int64_t)(b->y - a->y) * 16;
        day = (int64_t)(c->x - b->x) * 16;
        dby = (int64_t)(a->x - c->x) * 16;
        dcy = (int64_t)(b->x - a->x) * 16;
    }
    dzdx = (dax * a->z + dbx * b->z + dcx * c->z) * 256 / area;
    dldx = (dax * a->l + dbx * b->l + dcx * c->l) * 256 / area;
    for (y = y0; y <= y1; y++) {
        int64_t wa = wa0 + day * (y - y0), wb = wb0 + dby * (y - y0), wc = wc0 + dcy * (y - y0), z, l;
        uint16_t *row = fb + y * W;
        int32_t *zrow = zbuf + y * W;
        uint8_t *irow = ids + y * W;
        /* Skip to the first pixel inside, then step the depth and light from there. */
        for (x = x0; x <= x1 && (wa | wb | wc) < 0; x++, wa += dax, wb += dbx, wc += dcx) {}
        if (x > x1) continue;
        z = lerp8(wa, wb, wc, a->z, b->z, c->z, area);
        l = lerp8(wa, wb, wc, a->l, b->l, c->l, area);
        for (; x <= x1; x++, wa += dax, wb += dbx, wc += dcx, z += dzdx, l += dldx) {
            if ((wa | wb | wc) < 0) break;
            if ((int32_t)(z >> 8) <= zrow[x]) continue;
            if (cur_flags & OK_R3D_SHADOW) {
                if (irow[x] != 255) row[x] = darker(row[x]);
                irow[x] = 255;
                continue;
            }
            zrow[x] = (int32_t)(z >> 8);
            irow[x] = (uint8_t)cur_id;
            row[x] = tone[tone_of((int32_t)(l >> 8))];
        }
    }
}

/* A triangle of vb, turned to face where its vertex normals point. */
static void tri(int ia, int ib, int ic)
{
    const vert *a = &vb[ia], *b = &vb[ib], *c = &vb[ic];
    ok_v3 g, n;
    int64_t s;
    if (!a->ok || !b->ok || !c->ok) return;
    g = cross(sub(b->w, a->w), sub(c->w, a->w));
    n = add(add(a->n, b->n), c->n);
    s = (int64_t)g.x * n.x + (int64_t)g.y * n.y + (int64_t)g.z * n.z;
    if (s < 0) raster(a, c, b);
    else raster(a, b, c);
}

static void quad_idx(int a, int b, int c, int d)
{
    tri(a, b, c);
    tri(a, c, d);
}

void ok_r3d_quad(ok_v3 a, ok_v3 b, ok_v3 c, ok_v3 d)
{
    ok_v3 n = norm(cross(sub(b, a), sub(c, a)));
    put(0, a, n);
    put(1, b, n);
    put(2, c, n);
    put(3, d, n);
    quad_idx(0, 1, 2, 3);
}

void ok_r3d_box(ok_v3 lo, ok_v3 hi)
{
    static const int8_t faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int f, k;
    for (f = 0; f < 6; f++) {
        ok_v3 n = ok_v3_make(faces[f][0] * 4096, faces[f][1] * 4096, faces[f][2] * 4096);
        for (k = 0; k < 4; k++) {
            /* The four corners of the face, around it. */
            int u = k == 1 || k == 2, v = k >= 2;
            ok_v3 p;
            if (faces[f][0]) p = ok_v3_make(faces[f][0] > 0 ? hi.x : lo.x, u ? hi.y : lo.y, v ? hi.z : lo.z);
            else if (faces[f][1]) p = ok_v3_make(u ? hi.x : lo.x, faces[f][1] > 0 ? hi.y : lo.y, v ? hi.z : lo.z);
            else p = ok_v3_make(u ? hi.x : lo.x, v ? hi.y : lo.y, faces[f][2] > 0 ? hi.z : lo.z);
            put(k, p, n);
        }
        quad_idx(0, 1, 2, 3);
    }
}

/* A unit sphere point on a latitude-longitude grid. */
static ok_v3 sphere_at(int i, int stacks, int j, int slices)
{
    int th = i * (OK_TURN / 2) / stacks, ph = j * OK_TURN / slices;
    ok_fx s = ok_r3d_sin(th);
    return ok_v3_make(ok_fx_mul(s, ok_r3d_cos(ph)), ok_r3d_cos(th), ok_fx_mul(s, ok_r3d_sin(ph)));
}

static void grid(int rows, int slices)
{
    int i, j;
    for (i = 0; i < rows; i++)
        for (j = 0; j < slices; j++) {
            int a = i * (slices + 1) + j;
            quad_idx(a, a + 1, a + slices + 2, a + slices + 1);
        }
}

void ok_r3d_ellipsoid(ok_v3 c, ok_v3 r, int slices, int stacks)
{
    int i, j;
    if ((stacks + 1) * (slices + 1) > MAX_VERTS) return;
    for (i = 0; i <= stacks; i++)
        for (j = 0; j <= slices; j++) {
            ok_v3 s = sphere_at(i, stacks, j, slices);
            ok_v3 p = add(c, ok_v3_make(ok_fx_mul(s.x, r.x), ok_fx_mul(s.y, r.y), ok_fx_mul(s.z, r.z)));
            ok_v3 n = norm(ok_v3_make((ok_fx)((int64_t)s.x * 4096 / r.x), (ok_fx)((int64_t)s.y * 4096 / r.y),
                                      (ok_fx)((int64_t)s.z * 4096 / r.z)));
            put(i * (slices + 1) + j, p, n);
        }
    grid(stacks, slices);
}

/* A sphere cut at the equator, its top half moved to b and its bottom to a. */
void ok_r3d_capsule(ok_v3 a, ok_v3 b, ok_fx radius, int slices)
{
    enum { HALF = 3 };
    ok_v3 w = norm(sub(b, a)), helper = w.y > 3000 || w.y < -3000 ? ok_v3_make(4096, 0, 0) : ok_v3_make(0, 4096, 0);
    ok_v3 u = norm(cross(w, helper)), v = cross(u, w);
    int i, j, rows = 2 * HALF + 1;
    if ((rows + 1) * (slices + 1) > MAX_VERTS) return;
    for (i = 0; i <= rows; i++)
        for (j = 0; j <= slices; j++) {
            ok_v3 s = sphere_at(i <= HALF ? i : i - 1, 2 * HALF, j, slices);
            ok_v3 n = add(add(scale(u, s.x), scale(w, s.y)), scale(v, s.z));
            put(i * (slices + 1) + j, add(i <= HALF ? b : a, scale(n, radius)), n);
        }
    grid(rows, slices);
}

void ok_r3d_cylinder(ok_v3 base, ok_fx radius, ok_fx height, int slices)
{
    int j, k = slices + 1;
    if (2 * k + 2 > MAX_VERTS) return;
    for (j = 0; j <= slices; j++) {
        ok_v3 n = ok_v3_make(ok_r3d_cos(j * OK_TURN / slices), 0, ok_r3d_sin(j * OK_TURN / slices));
        ok_v3 p = add(base, ok_v3_make(ok_fx_mul(n.x, radius), 0, ok_fx_mul(n.z, radius)));
        put(j, p, n);
        put(k + j, add(p, ok_v3_make(0, height, 0)), n);
    }
    grid(1, slices);
    /* Caps: the side vertices again, with flat normals. */
    for (j = 0; j <= slices; j++) {
        ok_v3 n = ok_v3_make(ok_r3d_cos(j * OK_TURN / slices), 0, ok_r3d_sin(j * OK_TURN / slices));
        ok_v3 p = add(base, ok_v3_make(ok_fx_mul(n.x, radius), height, ok_fx_mul(n.z, radius)));
        put(j, p, ok_v3_make(0, 4096, 0));
    }
    put(k, add(base, ok_v3_make(0, height, 0)), ok_v3_make(0, 4096, 0));
    for (j = 0; j < slices; j++) tri(k, j, j + 1);
}

void ok_r3d_disc(ok_v3 c, ok_fx rx, ok_fx rz, int slices)
{
    int j;
    ok_v3 up = ok_v3_make(0, 4096, 0);
    if (slices + 2 > MAX_VERTS) return;
    put(slices + 1, c, up);
    for (j = 0; j <= slices; j++)
        put(j, add(c, ok_v3_make(ok_fx_mul(ok_r3d_cos(j * OK_TURN / slices), rx), 0,
                                 ok_fx_mul(ok_r3d_sin(j * OK_TURN / slices), rz))), up);
    for (j = 0; j < slices; j++) tri(slices + 1, j, j + 1);
}

void ok_r3d_finish(void)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int i = y * W + x, id = ids[i];
            int32_t z = zbuf[i], gap = z >> 4;
            if (!edge_on[id]) continue;
            if ((x > 0 && ids[i - 1] != id && z - zbuf[i - 1] > gap) || (x < W - 1 && ids[i + 1] != id && z - zbuf[i + 1] > gap) ||
                (y > 0 && ids[i - W] != id && z - zbuf[i - W] > gap) || (y < H - 1 && ids[i + W] != id && z - zbuf[i + W] > gap))
                fb[i] = edge_colour[id];
        }
}

int ok_r3d_project(ok_v3 p, int *x, int *y)
{
    put(0, p, ok_v3_make(0, 4096, 0));
    if (!vb[0].ok) return 0;
    *x = vb[0].x >> 4;
    *y = vb[0].y >> 4;
    return 1;
}

int ok_r3d_id_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;
    return ids[y * W + x] == 255 ? 0 : ids[y * W + x];
}

int ok_r3d_triangles(void)
{
    return triangles;
}
