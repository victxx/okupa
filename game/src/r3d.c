#include "okupa_r3d.h"
#include <string.h>
#ifdef OK_R3D_GL
#include <nds.h>
#endif

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
    int scaled;    /* normals need normalising */
} xform;

typedef struct {
    int32_t x, y;  /* screen, 1/16 pixel */
    int32_t z;     /* 2^34 / depth: larger is nearer */
    int32_t l;     /* light, Q12 */
    int32_t u, v;  /* texel, 1/16 */
    int ok;
    ok_v3 w, n;    /* world position and normal */
    int32_t cx, cy, cz; /* camera space, Q12 (for the DS's 3D hardware) */
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
static int cur_id, cur_flags, shadow_k;
static vert vb[MAX_VERTS];
static int triangles;
static const uint16_t *tex;
static int tex_stride, uv_x, uv_y, uv_w = 1, uv_h = 1, tile, flatten, avg_r, avg_g, avg_b;
static uint8_t lut[4][3][32]; /* channel value under each tone */
static int lut_stale = 1;    /* the light changed since lut was made */
/* A kept frame (ok_r3d_keep), and the rectangle drawn since it was kept. */
static uint16_t keep_fb[W * H];
#ifdef OK_R3D_GL /* the hardware keeps no depth or objects; the software renderer here only draws icons, unkept */
static int32_t keep_z[1];
static uint8_t keep_ids[1], keep_edge_on[256];
#else
static int32_t keep_z[W * H];
static uint8_t keep_ids[W * H], keep_edge_on[256];
#endif
static uint16_t keep_edge_colour[256];
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
static int projecting; /* ok_r3d_project wants the screen position even on the hardware */
#ifdef OK_R3D_GL
/* Everything drawn after ok_r3d_begin is recorded as GPU commands; a kept
 * frame's list is replayed by DMA each frame (cheap), and what is drawn
 * after ok_r3d_restore goes to the GPU directly. */
#define GX_WORDS 40000
#ifndef GL_CULL_SIDE
#define GL_CULL_SIDE POLY_CULL_BACK /* which side is the back, for the triangles tri() orients */
#endif
static uint32_t gx_rec[GX_WORDS + 1], gx_kept[GX_WORDS + 1];
static int gx_n, gx_recording, gx_kept_ok, gx_kept_polys, gx_kept_alpha = 31;
static uint16_t gx_kept_clear = 0x8000;
static void gx(uint32_t cmd, int n, uint32_t p1, uint32_t p2)
{
    uint32_t pack = cmd; /* one command a packet; the rest of the packet is NOPs */
    if (gx_recording) {
        if (gx_n + 3 > GX_WORDS) return;
        gx_rec[1 + gx_n++] = pack;
        if (n > 0) gx_rec[1 + gx_n++] = p1;
        if (n > 1) gx_rec[1 + gx_n++] = p2;
        return;
    }
    GFX_FIFO = pack;
    if (n > 0) GFX_FIFO = p1;
    if (n > 1) GFX_FIFO = p2;
}
static void gx_matrix(const int32_t m[12])
{
    int i;
    if (gx_recording) {
        if (gx_n + 13 > GX_WORDS) return;
        gx_rec[1 + gx_n++] = 0x17; /* MTX_LOAD_4x3 */
        for (i = 0; i < 12; i++) gx_rec[1 + gx_n++] = (uint32_t)m[i];
        return;
    }
    GFX_FIFO = 0x17;
    for (i = 0; i < 12; i++) GFX_FIFO = (uint32_t)m[i];
}
static int gx_world_n, gx_after_camera;
static void gx_play(uint32_t *list, int n)
{
    int i;
    /* Straight into the FIFO from the CPU (it waits by itself when the FIFO
     * is full); no DMA, nothing left running behind our back. */
    for (i = 1; i <= n; i++) GFX_FIFO = list[i];
}
static int gl_on; /* off until the platform turns it on: icons are drawn in software first */
static int gl_cpu_cull = 1; /* drop what this camera cannot see before it reaches the hardware */
static int32_t gl_kx, gl_ky;
static int gl_dirty = 1, gl_open, gl_polys, gl_clear_alpha = 31;
static uint16_t gl_clear = 0x8000;
#endif

/* Divisions: the DSi's CPU has no divide instruction, so a 64-bit one in C
 * costs hundreds of cycles. The platform can hand in its hardware divider. */
static int64_t (*hw_divide)(int64_t n, int64_t d);
void ok_r3d_divider(int64_t (*divide)(int64_t n, int64_t d))
{
    hw_divide = divide;
}

#ifdef OK_R3D_STATS
static uint32_t stats[OK_R3D_STATS_N];
#define STAT(k, n) (stats[k] += (uint32_t)(n))
#else
#define STAT(k, n) ((void)0)
#endif
void ok_r3d_stats(uint32_t out[OK_R3D_STATS_N], int clear)
{
#ifdef OK_R3D_STATS
    memcpy(out, stats, sizeof stats);
    if (clear) memset(stats, 0, sizeof stats);
#else
    memset(out, 0, sizeof(uint32_t) * OK_R3D_STATS_N);
    (void)clear;
#endif
}

static int64_t divide(int64_t n, int64_t d)
{
    STAT(OK_R3D_STAT_DIVS, 1);
    return hw_divide ? hw_divide(n, d) : n / d;
}

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

/* Within about 0.3 degrees: atan on [-1, 1] by a short polynomial, then the octant. */
int ok_r3d_atan2(int32_t y, int32_t x)
{
    int64_t ax = x < 0 ? -(int64_t)x : x, ay = y < 0 ? -(int64_t)y : y, r;
    int a;
    if (!ax && !ay) return 0;
    r = ax >= ay ? divide(ay * 4096, ax) : divide(ax * 4096, ay); /* 0..1, Q12 */
    /* turns: r/8 - r(r-1)(0.2447 + 0.0663 r)/(2 pi) */
    a = (int)(r / 8 + r * (4096 - r) / 4096 * (1002 + 272 * r / 4096) / 25736);
    if (ay > ax) a = 1024 - a;
    if (x < 0) a = 2048 - a;
    if (y < 0) a = -a;
    return a & (OK_TURN - 1);
}

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    if (!(v >> 32)) { /* most lengths fit 32 bits: the same steps, cheaper on a 32-bit CPU */
        uint32_t v32 = (uint32_t)v, r32 = 0, b32 = (uint32_t)1 << 30;
        while (b32 > v32) b32 >>= 2;
        while (b32) {
            if (v32 >= r32 + b32) {
                v32 -= r32 + b32;
                r32 = (r32 >> 1) + b32;
            } else {
                r32 >>= 1;
            }
            b32 >>= 2;
        }
        return r32;
    }
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
    int64_t inv;
    if (!len) return ok_v3_make(0, 4096, 0);
    inv = divide((int64_t)1 << 30, len);
    return ok_v3_make((ok_fx)(a.x * inv >> 18), (ok_fx)(a.y * inv >> 18), (ok_fx)(a.z * inv >> 18));
}

static ok_v3 rot(const xform *x, ok_v3 v)
{
    return ok_v3_make(dot(ok_v3_make(x->m[0][0], x->m[0][1], x->m[0][2]), v),
                      dot(ok_v3_make(x->m[1][0], x->m[1][1], x->m[1][2]), v),
                      dot(ok_v3_make(x->m[2][0], x->m[2][1], x->m[2][2]), v));
}

static uint16_t col555(int r, int g, int b)
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
#ifdef OK_R3D_GL
    if (gl_on) {
        memset(fb, 0, W * H * 2); /* the 2D layer above the 3D: see-through */
        gl_clear = background;
        gl_clear_alpha = 31;
        gl_dirty = 1;
        gl_open = 0;
        gx_n = 0;
        gx_recording = 1;
        gl_polys = 0;
    } else
#endif
        for (i = 0; i < W * H; i++) fb[i] = background;
    memset(zbuf, 0, sizeof zbuf);
    memset(ids, 0, sizeof ids);
    memset(edge_on, 0, sizeof edge_on);
    memset(stack, 0, sizeof stack);
    sp = 0;
    stack[0].m[0][0] = stack[0].m[1][1] = stack[0].m[2][2] = 4096;
    triangles = 0;
    tile = 0;
    flatten = 0;
    dirty_x0 = dirty_y0 = 0;
    dirty_x1 = W - 1;
    dirty_y1 = H - 1;
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
#ifdef OK_R3D_GL
    /* Vertices come in camera space, x and y already scaled by the focal
     * length, a 64th of the size: a plain 90 degree frustum does the rest. */
    gl_kx = focal_px * 8;            /* focal / 128 / 64, times 65536 */
    gl_ky = focal_px * 65536 / 6144; /* focal / 96 / 64 */
    if (gl_on) { /* the camera as the hardware's matrix: world (a 64th) to the fixed frustum */
        int32_t m[12], sx = focal_px * 4096 / 128, sy = focal_px * 4096 / 96;
        m[0] = (int32_t)((int64_t)cam.r.x * sx >> 12), m[1] = (int32_t)((int64_t)cam.u.x * sy >> 12), m[2] = -cam.f.x;
        m[3] = (int32_t)((int64_t)cam.r.y * sx >> 12), m[4] = (int32_t)((int64_t)cam.u.y * sy >> 12), m[5] = -cam.f.y;
        m[6] = (int32_t)((int64_t)cam.r.z * sx >> 12), m[7] = (int32_t)((int64_t)cam.u.z * sy >> 12), m[8] = -cam.f.z;
        m[9] = (int32_t)(-((int64_t)dot(cam.r, eye) >> 6) * sx >> 12);
        m[10] = (int32_t)(-((int64_t)dot(cam.u, eye) >> 6) * sy >> 12);
        m[11] = (int32_t)((int64_t)dot(cam.f, eye) >> 6);
        if (gl_open) gx(FIFO_END, 0, 0, 0);
        gl_open = 0;
        gl_dirty = 1;
        gx_matrix(m);
        gx_after_camera = gx_n;
    }
#endif
}

void ok_r3d_light(ok_v3 light_dir, ok_v3 light, ok_v3 ambient)
{
    if (light.x != light_rgb.x || light.y != light_rgb.y || light.z != light_rgb.z || ambient.x != ambient_rgb.x ||
        ambient.y != ambient_rgb.y || ambient.z != ambient_rgb.z)
        lut_stale = 1;
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
        tone[k] = col555((int)((int64_t)r * (ambient_rgb.x + light_rgb.x * k / 3) / 4096),
                      (int)((int64_t)g * (ambient_rgb.y + light_rgb.y * k / 3) / 4096),
                      (int)((int64_t)b * (ambient_rgb.z + light_rgb.z * k / 3) / 4096));
    }
    for (k = 0; k < 4 && (flags & OK_R3D_TEXTURED) && lut_stale; k++) { /* the same for every texture under one light */
        int c, i;
        for (c = 0; c < 3; c++) {
            ok_fx f = c == 0 ? ambient_rgb.x + light_rgb.x * k / 3 : c == 1 ? ambient_rgb.y + light_rgb.y * k / 3
                                                                      : ambient_rgb.z + light_rgb.z * k / 3;
            for (i = 0; i < 32; i++) {
                int v = (int)((int64_t)i * f / 4096);
                lut[k][c][i] = (uint8_t)(v > 31 ? 31 : v);
            }
        }
    }
    if (flags & OK_R3D_TEXTURED) lut_stale = 0;
    if ((flags & OK_R3D_TEXTURED) && !tex) flags &= ~OK_R3D_TEXTURED;
#ifdef OK_R3D_GL
    gl_dirty = 1;
#endif
    cur_id = id & 255;
    cur_flags = flags;
    if (flags & OK_R3D_SHADOW) { /* shadows: the colour's red is how dark (k/32), the id picks the layer */
        shadow_k = colour & 31 ? colour & 31 : 8;
        cur_id = 250 + (id % 5);
    }
    if (flags & OK_R3D_EDGE) {
        uint16_t t = tone[0];
        edge_on[cur_id] = 1;
        edge_colour[cur_id] = col555((t & 31) * 9 / 20, (t >> 5 & 31) * 9 / 20, (t >> 10 & 31) * 9 / 20 + 1);
    }
}

void ok_r3d_texture(const uint16_t *texels, int stride)
{
    tex = texels;
    tex_stride = stride;
#ifdef OK_R3D_GL
    gl_dirty = 1;
#endif
}

void ok_r3d_uv(int x, int y, int w, int h)
{
#ifdef OK_R3D_GL
    gl_dirty = 1;
#endif
    uv_x = x;
    uv_y = y;
    uv_w = w > 0 ? w : 1;
    uv_h = h > 0 ? h : 1;
    if (flatten && tex) { /* the rectangle's average colour, to blend towards */
        int i, j, n = 0;
        avg_r = avg_g = avg_b = 0;
        for (j = 0; j < uv_h; j += 2)
            for (i = 0; i < uv_w; i += 2, n++) {
                uint16_t c = tex[(uv_y + j) * tex_stride + uv_x + i];
                avg_r += c & 31, avg_g += c >> 5 & 31, avg_b += c >> 10 & 31;
            }
        avg_r /= n, avg_g /= n, avg_b /= n;
    }
}

void ok_r3d_flatten(int amount)
{
    flatten = amount < 0 ? 0 : amount > 256 ? 256 : amount;
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

void ok_r3d_scale(ok_fx sx, ok_fx sy, ok_fx sz)
{
    int i;
    for (i = 0; i < 3; i++) {
        stack[sp].m[i][0] = ok_fx_mul(stack[sp].m[i][0], sx);
        stack[sp].m[i][1] = ok_fx_mul(stack[sp].m[i][1], sy);
        stack[sp].m[i][2] = ok_fx_mul(stack[sp].m[i][2], sz);
    }
    stack[sp].scaled = 1;
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

/* Key light plus a little from the sky above, Q12. */
static int32_t light_of(ok_v3 n)
{
    ok_fx ndl = dot(n, to_light), up = (n.y + 4096) / 2;
    return (ndl > 0 ? ndl * 4 / 5 : 0) + up / 5;
}

/* Model space to screen, with the light at the vertex. */
static void put(int k, ok_v3 p, ok_v3 n)
{
    vert *v = &vb[k];
    ok_v3 d;
    ok_fx vx, vy, vz, ndl, up;
    int64_t sx, sy, inv;
    v->w = add(rot(&stack[sp], p), stack[sp].t);
    v->n = rot(&stack[sp], n);
    if (stack[sp].scaled) { /* nearly unit already: one Newton step back to 1 */
        ok_fx k = (3 * 4096 - dot(v->n, v->n)) / 2;
        v->n = scale(v->n, k);
    }
#ifdef OK_R3D_GL
    if (gl_on && !projecting && !gl_cpu_cull) { /* a scene for other cameras: world position, normal and light are all it needs */
        v->l = light_of(v->n);
        v->u = v->v = 0;
        v->ok = 1;
        STAT(OK_R3D_STAT_VERTS, 1);
        return;
    }
#endif
    d = sub(v->w, cam.eye);
    vx = dot(d, cam.r);
    vy = dot(d, cam.u);
    vz = dot(d, cam.f);
    v->ok = 0;
    v->u = v->v = 0;
    v->cx = vx;
    v->cy = vy;
    v->cz = vz;
    if (vz < NEAR) return;
    inv = divide((int64_t)1 << 40, vz);
    STAT(OK_R3D_STAT_VERTS, 1);
    sx = (int64_t)W * 8 + ((int64_t)vx * cam.focal * 16 * inv >> 40);
    sy = (int64_t)H * 8 - ((int64_t)vy * cam.focal * 16 * inv >> 40);
    if (sx < -GUARD || sx > GUARD || sy < -GUARD || sy > GUARD) return;
    v->x = (int32_t)sx;
    v->y = (int32_t)sy;
    v->z = (int32_t)(inv >> 6);
    v->l = light_of(v->n);
    v->ok = 1;
    (void)ndl;
    (void)up;
}

static int tone_of(int32_t l)
{
    return l < 1100 ? 0 : l < 2200 ? 1 : l < 3300 ? 2 : 3;
}

/* c times (32 - k) / 32 per channel. */
static uint16_t shade_by(uint16_t c, int k)
{
    int r = (c & 31) * (32 - k) / 32, g = (c >> 5 & 31) * (32 - k) / 32, b = (c >> 10 & 31) * (32 - k) / 32;
    return (uint16_t)(0x8000u | r | g << 5 | b << 10);
}

#ifdef OK_R3D_GL
/* ---- The DS's own 3D hardware ----------------------------------------------
 * The shapes are built and lit here exactly as for the software renderer,
 * then each triangle goes to the GPU in camera space with its tone as the
 * vertex colour. Textures are the uv rectangles, uploaded once each, small. */
typedef struct {
    const uint16_t *src;
    int x, y, w, h, flat, tiled, name, tw, th;
} gl_texe;
#define GL_TEXES 96
static gl_texe texes[GL_TEXES];
static int ntex, gl_tex_cur = -1;
static uint16_t gl_buf[32 * 64];

static int pow2_at_most(int v, int cap)
{
    int p = 8;
    while (p * 2 <= v && p * 2 <= cap) p *= 2;
    return p;
}

/* The texture for the current rectangle, uploaded on first use. */
static int gl_texture(void)
{
    int i, x, y, cap = tile ? 16 : 32;
    gl_texe *e;
    for (i = 0; i < ntex; i++) {
        e = &texes[i];
        if (e->src == tex && e->x == uv_x && e->y == uv_y && e->w == uv_w && e->h == uv_h && e->flat == flatten &&
            e->tiled == (tile != 0))
            return e->name >= 0 ? i : -1;
    }
    if (ntex == GL_TEXES) return -1;
    e = &texes[ntex++];
    e->src = tex, e->x = uv_x, e->y = uv_y, e->w = uv_w, e->h = uv_h, e->flat = flatten, e->tiled = tile != 0;
    e->tw = pow2_at_most(uv_w, cap);
    e->th = pow2_at_most(uv_h, (cur_flags & OK_R3D_CUTOUT) ? 64 : cap);
    for (y = 0; y < e->th; y++)
        for (x = 0; x < e->tw; x++) {
            uint16_t t = tex[(uv_y + y * uv_h / e->th) * tex_stride + uv_x + x * uv_w / e->tw];
            if (flatten && (t & 0x8000u))
                t = (uint16_t)(0x8000u | (((t & 31) * (256 - flatten) + avg_r * flatten) >> 8) |
                               ((((t >> 5 & 31) * (256 - flatten) + avg_g * flatten) >> 8) << 5) |
                               ((((t >> 10 & 31) * (256 - flatten) + avg_b * flatten) >> 8) << 10));
            gl_buf[y * e->tw + x] = t;
        }
    DC_FlushRange(gl_buf, sizeof gl_buf);
    if (!glGenTextures(1, &e->name)) {
        e->name = -1;
        return -1;
    }
    glBindTexture(0, e->name);
    if (!glTexImage2D(0, 0, GL_RGBA, e->tw, e->th, 0, TEXGEN_OFF | GL_TEXTURE_WRAP_S | (e->tiled ? GL_TEXTURE_WRAP_T : 0),
                      gl_buf)) {
        glDeleteTextures(1, &e->name);
        e->name = -1;
        return -1;
    }
    return ntex - 1;
}

void ok_r3d_texture_changed(const uint16_t *texels)
{
    int i, j = 0;
    for (i = 0; i < ntex; i++) {
        if (texes[i].src == texels) {
            if (texes[i].name >= 0) glDeleteTextures(1, &texes[i].name);
            continue;
        }
        texes[j++] = texes[i];
    }
    ntex = j;
    gl_dirty = 1;
}

static void gl_state(void)
{
    int textured = cur_flags & OK_R3D_TEXTURED;
    if (gl_open) gx(FIFO_END, 0, 0, 0);
    gl_tex_cur = textured ? gl_texture() : -1;
    if (gl_tex_cur >= 0) {
        glBindTexture(0, texes[gl_tex_cur].name);
        gx(FIFO_TEX_FORMAT, 1, glGetTexParameter(), 0);
    } else {
        gx(FIFO_TEX_FORMAT, 1, 0, 0);
    }
    gx(FIFO_POLY_FORMAT, 1,
       POLY_ALPHA((cur_flags & OK_R3D_SHADOW) ? (uint32_t)(shadow_k < 31 ? shadow_k : 30) : 31) |
           POLY_ID((cur_flags & OK_R3D_EDGE) ? 1 + (uint32_t)(cur_id % 62) : 0) |
           ((cur_flags & OK_R3D_BOTH) ? POLY_CULL_NONE : GL_CULL_SIDE) | POLY_RENDER_FAR_POLYS |
           POLY_RENDER_1DOT_POLYS,
       0);
    gx(FIFO_BEGIN, 1, GL_TRIANGLES, 0);
    gl_open = 1;
    gl_dirty = 0;
}

void ok_r3d_cpu_cull(int on)
{
    gl_cpu_cull = on;
}

static void gl_tri(const vert *a, const vert *b, const vert *c)
{
    const vert *v[3];
    int i, textured;
    if (gl_cpu_cull) {
        /* Under this camera, as the software renderer would: facing away, off
         * the screen or smaller than a pixel, it is dropped here; on the
         * hardware it would still take room in its 2048 polygons. */
        int64_t area = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(b->y - a->y) * (c->x - a->x);
        int32_t minx = a->x < b->x ? (a->x < c->x ? a->x : c->x) : (b->x < c->x ? b->x : c->x);
        int32_t maxx = a->x > b->x ? (a->x > c->x ? a->x : c->x) : (b->x > c->x ? b->x : c->x);
        int32_t miny = a->y < b->y ? (a->y < c->y ? a->y : c->y) : (b->y < c->y ? b->y : c->y);
        int32_t maxy = a->y > b->y ? (a->y > c->y ? a->y : c->y) : (b->y > c->y ? b->y : c->y);
        int32_t x0 = (minx - 8 + 15) >> 4, x1 = (maxx - 8) >> 4, y0 = (miny - 8 + 15) >> 4, y1 = (maxy - 8) >> 4;
        if (area == 0 || (area > 0 && !(cur_flags & OK_R3D_BOTH))) return;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W - 1) x1 = W - 1;
        if (y1 > H - 1) y1 = H - 1;
        if (x0 > x1 || y0 > y1) return;
    }
    v[0] = a, v[1] = b, v[2] = c;
    for (i = 0; i < 3; i++) { /* world space, a 64th: it must fit the hardware's 16 bits */
        int32_t x = v[i]->w.x >> 6, y = v[i]->w.y >> 6, z = v[i]->w.z >> 6;
        if (x > 32767 || x < -32767 || y > 32767 || y < -32767 || z > 32767 || z < -32767) return;
    }
    if (gl_dirty) gl_state();
    textured = (cur_flags & OK_R3D_TEXTURED) && gl_tex_cur >= 0;
    triangles++;
    for (i = 0; i < 3; i++) {
        const vert *p = v[i];
        int k = tone_of(p->l);
        uint32_t colour = (cur_flags & OK_R3D_SHADOW) ? 0
                          : textured ? (uint32_t)RGB15(lut[k][0][31], lut[k][1][31], lut[k][2][31])
                                     : (uint32_t)(tone[k] & 0x7fff);
        /* One packet a vertex: colour, texture coordinate, position. */
        {
            uint32_t w[5], xy = ((uint32_t)(uint16_t)(p->w.y >> 6) << 16) | (uint16_t)(p->w.x >> 6), zz = (uint32_t)(uint16_t)(p->w.z >> 6);
            int n = 0, j;
            if (textured) {
                const gl_texe *e = &texes[gl_tex_cur];
                int32_t tu = (int32_t)(p->u * e->tw / e->w), tv = (int32_t)(p->v * e->th / e->h);
                w[n++] = FIFO_COMMAND_PACK(FIFO_COLOR, FIFO_TEX_COORD, FIFO_VERTEX16, FIFO_NOP);
                w[n++] = colour;
                w[n++] = TEXTURE_PACK((t16)tu, (t16)tv);
            } else {
                w[n++] = FIFO_COMMAND_PACK(FIFO_COLOR, FIFO_VERTEX16, FIFO_NOP, FIFO_NOP);
                w[n++] = colour;
            }
            w[n++] = xy;
            w[n++] = zz;
            if (gx_recording) {
                if (gx_n + n <= GX_WORDS)
                    for (j = 0; j < n; j++) gx_rec[1 + gx_n++] = w[j];
            } else {
                for (j = 0; j < n; j++) GFX_FIFO = w[j];
            }
        }
    }
}

/* The scene since the camera was set, kept to be drawn again under other
 * cameras (the opening film flies round a scene that does not change). */
void ok_r3d_world_keep(void)
{
    if (!gl_on) return;
    if (gl_open) gx(FIFO_END, 0, 0, 0);
    gl_open = 0;
    gl_dirty = 1;
    gx_world_n = gx_n - gx_after_camera;
    if (gx_world_n < 0) gx_world_n = 0;
    /* in the kept frame's buffer: the film and a kept room never share a frame */
    memcpy(gx_kept + 1, gx_rec + 1 + gx_after_camera, (size_t)gx_world_n * 4);
    gx_kept_ok = 0;
}

int ok_r3d_world_again(void)
{
    if (!gl_on || !gx_world_n || !gx_recording || gx_n + gx_world_n > GX_WORDS) return 0;
    if (gl_open) gx(FIFO_END, 0, 0, 0);
    gl_open = 0;
    gl_dirty = 1;
    memcpy(gx_rec + 1 + gx_n, gx_kept + 1, (size_t)gx_world_n * 4);
    gx_n += gx_world_n;
    return 1;
}

void ok_r3d_world_forget(void)
{
    gx_world_n = 0;
}

void ok_r3d_hardware(int on)
{
    gl_on = on;
    if (on) { /* vertices come pre-projected: one fixed frustum for good */
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf32(-64, 64, -64, 64, 64, 16384);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
}

int ok_r3d_on_hardware(void)
{
    return gl_on;
}

int ok_r3d_kept_triangles(void)
{
    return gx_kept_polys;
}

int ok_r3d_retains(void)
{
    return 1;
}

void ok_r3d_see_through(void)
{
    gl_clear_alpha = 0;
}

/* The 2D layer laid over the 3D and the sky at k/16, by the hardware's
 * blending (fades over the 3D, with no work per pixel). */
static int gl_blend_set;
void ok_r3d_layer_alpha(int k16)
{
    k16 = k16 < 0 ? 0 : k16 > 16 ? 16 : k16;
    REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG3 | BLEND_DST_BG0 | BLEND_DST_BG2 | BLEND_DST_BACKDROP;
    REG_BLDALPHA = (uint16_t)(k16 | (16 - k16) << 8);
    gl_blend_set = 1;
}

void ok_r3d_swap(void)
{
    if (!gl_on) return;
    if (!gl_blend_set) REG_BLDCNT = 0; /* no fade this frame: the 2D layer as it is */
    gl_blend_set = 0;
    if (gl_open) gx(FIFO_END, 0, 0, 0);
    if (gx_recording) { /* drawn since ok_r3d_begin and not kept: show it now */
        gx_recording = 0;
        gx_play(gx_rec, gx_n);
    }
    gl_open = 0;
    gl_dirty = 1;
    glClearColor(gl_clear & 31, gl_clear >> 5 & 31, gl_clear >> 10 & 31, (uint8_t)gl_clear_alpha);
    glFlush(GL_WBUFFERING);
    gl_polys = 0;
}
#else
void ok_r3d_layer_alpha(int k16)
{
    (void)k16;
}

void ok_r3d_cpu_cull(int on)
{
    (void)on;
}

void ok_r3d_world_keep(void)
{
}

int ok_r3d_world_again(void)
{
    return 0;
}

void ok_r3d_world_forget(void)
{
}

void ok_r3d_hardware(int on)
{
    (void)on;
}

int ok_r3d_on_hardware(void)
{
    return 0;
}

int ok_r3d_kept_triangles(void)
{
    return 0;
}

int ok_r3d_retains(void)
{
#ifdef OK_R3D_SIM_HARDWARE /* host counts of what the hardware gets: everything every frame */
    return 0;
#else
    return 1;
#endif
}

void ok_r3d_see_through(void)
{
}

void ok_r3d_swap(void)
{
}

void ok_r3d_texture_changed(const uint16_t *texels)
{
    (void)texels;
}
#endif

static void raster(const vert *a, const vert *b, const vert *c)
{
#ifdef OK_R3D_GL
    if (gl_on) {
        gl_tri(a, b, c);
        return;
    }
#endif
    int64_t area = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(b->y - a->y) * (c->x - a->x);
    int32_t minx, maxx, miny, maxy, x, y, x0, x1, y0, y1;
    int64_t dax, dbx, dcx, day, dby, dcy, wa0, wb0, wc0;
    int64_t dzdx, dldx, dudx = 0, dvdx = 0, dzdy, dldy, dudy = 0, dvdy = 0, z0, l0, u0 = 0, v0 = 0;
    int textured = cur_flags & OK_R3D_TEXTURED, speck;
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
    STAT(OK_R3D_STAT_TRIS, 1);
    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
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
    /* The derivatives, per pixel with 8 fraction bits. A speck of a
     * triangle takes one depth, light and texel for all of it. */
    speck = x1 - x0 < 2 && y1 - y0 < 2;
    dzdx = dldx = dzdy = dldy = dudx = dvdx = dudy = dvdy = 0;
    if (!speck) {
        dzdx = divide((dax * a->z + dbx * b->z + dcx * c->z) * 256, area);
        dldx = divide((dax * a->l + dbx * b->l + dcx * c->l) * 256, area);
        dzdy = divide((day * a->z + dby * b->z + dcy * c->z) * 256, area);
        dldy = divide((day * a->l + dby * b->l + dcy * c->l) * 256, area);
        if (textured) {
            dudx = divide((dax * a->u + dbx * b->u + dcx * c->u) * 256, area);
            dvdx = divide((dax * a->v + dbx * b->v + dcx * c->v) * 256, area);
            dudy = divide((day * a->u + dby * b->u + dcy * c->u) * 256, area);
            dvdy = divide((day * a->v + dby * b->v + dcy * c->v) * 256, area);
        }
    }
    /* At the first pixel centre: from vertex a along the derivatives (its
     * offset in sixteenths of a pixel), no more divisions. */
    {
        int64_t ox = (int64_t)x0 * 16 + 8 - a->x, oy = (int64_t)y0 * 16 + 8 - a->y;
        z0 = ((int64_t)a->z * 256) + ((dzdx * ox + dzdy * oy) >> 4);
        l0 = ((int64_t)a->l * 256) + ((dldx * ox + dldy * oy) >> 4);
        if (textured) {
            u0 = ((int64_t)a->u * 256) + ((dudx * ox + dudy * oy) >> 4);
            v0 = ((int64_t)a->v * 256) + ((dvdx * ox + dvdy * oy) >> 4);
        }
    }
    /* Divisions above are per triangle; per row and pixel it is all adds.
     * When every value in the box fits 31 bits (nearly always), the steps
     * run in 32 bits, which is half the work on a 32-bit CPU. */
    {
        int64_t ex = (int64_t)(x1 - x0 + 1), ey = (int64_t)(y1 - y0 + 1), lim = (int64_t)1 << 30;
        int fast = 1, e;
        const int64_t w0[3] = {wa0, wb0, wc0}, wdx[3] = {dax, dbx, dcx}, wdy[3] = {day, dby, dcy};
        for (e = 0; e < 3 && fast; e++) {
            int64_t c0 = w0[e], c1 = w0[e] + wdx[e] * ex, c2 = w0[e] + wdy[e] * ey, c3 = c1 + wdy[e] * ey;
            fast = c0 < lim && c0 > -lim && c1 < lim && c1 > -lim && c2 < lim && c2 > -lim && c3 < lim && c3 > -lim;
        }
        if (fast) {
            int32_t big = (int32_t)1 << 22; /* attributes stay under 2^30 with their 8 fraction bits */
            fast = a->z < big && b->z < big && c->z < big && a->z >= 0 && b->z >= 0 && c->z >= 0 &&
                   (!textured || (a->u < big && a->u > -big && b->u < big && b->u > -big && c->u < big && c->u > -big &&
                                  a->v < big && a->v > -big && b->v < big && b->v > -big && c->v < big && c->v > -big));
        }
#define PIXEL(Z, L, U, V)                                                                                                    \
    do {                                                                                                                     \
        STAT(OK_R3D_STAT_PIXELS, 1);                                                                                         \
        if ((int32_t)((Z) >> 8) <= zrow[x]) break;                                                                           \
        if (cur_flags & OK_R3D_SHADOW) { /* each layer darkens a pixel once; layers stack */                                 \
            if (irow[x] != cur_id) row[x] = shade_by(row[x], shadow_k);                                                       \
            irow[x] = (uint8_t)cur_id;                                                                                       \
            break;                                                                                                           \
        }                                                                                                                    \
        if (textured) {                                                                                                      \
            int k = tone_of((int32_t)((L) >> 8)), tu = (int)((U) >> 12), tv = (int)((V) >> 12);                              \
            uint16_t t;                                                                                                      \
            tv = tile ? tv & (uv_h - 1) : tv < 0 ? 0 : tv >= uv_h ? uv_h - 1 : tv;                                           \
            t = tex[(uv_y + tv) * tex_stride + uv_x + (tu & (uv_w - 1))];                                                    \
            if (!(t & 0x8000u) && (cur_flags & OK_R3D_CUTOUT)) break; /* see-through texel */                                \
            zrow[x] = (int32_t)((Z) >> 8);                                                                                   \
            irow[x] = (uint8_t)cur_id;                                                                                       \
            if (flatten) /* only a hint of the texture over its own flat colour */                                           \
                t = (uint16_t)(((t & 31) * (256 - flatten) + avg_r * flatten) >> 8 |                                         \
                               (((t >> 5 & 31) * (256 - flatten) + avg_g * flatten) >> 8) << 5 |                             \
                               (((t >> 10 & 31) * (256 - flatten) + avg_b * flatten) >> 8) << 10);                           \
            row[x] = (uint16_t)(0x8000u | lut[k][0][t & 31] | lut[k][1][t >> 5 & 31] << 5 | lut[k][2][t >> 10 & 31] << 10); \
        } else {                                                                                                             \
            zrow[x] = (int32_t)((Z) >> 8);                                                                                   \
            irow[x] = (uint8_t)cur_id;                                                                                       \
            row[x] = tone[tone_of((int32_t)((L) >> 8))];                                                                     \
        }                                                                                                                    \
    } while (0)
        for (y = y0; y <= y1; y++) {
            int64_t wa = wa0 + day * (y - y0), wb = wb0 + dby * (y - y0), wc = wc0 + dcy * (y - y0), z, l, u = 0, v = 0;
            uint16_t *row = fb + y * W;
            int32_t *zrow = zbuf + y * W;
            uint8_t *irow = ids + y * W;
            /* Skip to the first pixel inside, then step the depth and light from there. */
            for (x = x0; x <= x1 && (wa | wb | wc) < 0; x++, wa += dax, wb += dbx, wc += dcx) {}
            if (x > x1) continue;
            z = z0 + dzdy * (y - y0) + dzdx * (x - x0);
            l = l0 + dldy * (y - y0) + dldx * (x - x0);
            if (textured) {
                u = u0 + dudy * (y - y0) + dudx * (x - x0);
                v = v0 + dvdy * (y - y0) + dvdx * (x - x0);
            }
            STAT(OK_R3D_STAT_ROWS, 1);
            if (fast) { /* unsigned steps: one step past the edge may wrap, harmlessly */
                uint32_t wa32 = (uint32_t)wa, wb32 = (uint32_t)wb, wc32 = (uint32_t)wc, z32 = (uint32_t)z, l32 = (uint32_t)l;
                uint32_t u32 = (uint32_t)u, v32 = (uint32_t)v, sa = (uint32_t)dax, sb = (uint32_t)dbx, sc = (uint32_t)dcx;
                uint32_t sz = (uint32_t)dzdx, sl = (uint32_t)dldx, su = (uint32_t)dudx, sv = (uint32_t)dvdx;
                for (; x <= x1; x++, wa32 += sa, wb32 += sb, wc32 += sc, z32 += sz, l32 += sl, u32 += su, v32 += sv) {
                    if ((int32_t)(wa32 | wb32 | wc32) < 0) break;
                    PIXEL((int32_t)z32, (int32_t)l32, (int32_t)u32, (int32_t)v32);
                }
                continue;
            }
            for (; x <= x1; x++, wa += dax, wb += dbx, wc += dcx, z += dzdx, l += dldx, u += dudx, v += dvdx) {
                if ((wa | wb | wc) < 0) break;
                PIXEL(z, l, u, v);
            }
        }
#undef PIXEL
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
    if (cur_flags & OK_R3D_FLAT) { /* one light for the whole face: the facets show */
        vert fa = *a, fb = *b, fc = *c;
        ok_v3 e1 = sub(b->w, a->w), e2 = sub(c->w, a->w), fn;
        int64_t x = (int64_t)e1.y * e2.z - (int64_t)e1.z * e2.y, y = (int64_t)e1.z * e2.x - (int64_t)e1.x * e2.z,
                z = (int64_t)e1.x * e2.y - (int64_t)e1.y * e2.x;
        while (x > 1 << 20 || x < -(1 << 20) || y > 1 << 20 || y < -(1 << 20) || z > 1 << 20 || z < -(1 << 20))
            x /= 2, y /= 2, z /= 2;
        fn = norm(ok_v3_make((ok_fx)x, (ok_fx)y, (ok_fx)z));
        if (s < 0) fn = ok_v3_make(-fn.x, -fn.y, -fn.z);
        fa.l = fb.l = fc.l = light_of(fn);
        if (s < 0) raster(&fa, &fc, &fb);
        else raster(&fa, &fb, &fc);
        return;
    }
    if (s < 0) raster(a, c, b);
    else raster(a, b, c);
}

static void quad_idx(int a, int b, int c, int d)
{
    tri(a, b, c);
    tri(a, c, d);
}

void ok_r3d_tile(int texels_per_unit)
{
#ifdef OK_R3D_GL
    gl_dirty = 1;
#endif
    tile = texels_per_unit;
}

/* Tiling: the texture laid flat on the plane facing axis, so neighbouring
 * faces and boxes line up. */
static void planar(int k, ok_v3 p, int axis)
{
    ok_fx u = axis == 0 ? p.z : p.x, v = axis == 1 ? p.z : -p.y;
    vb[k].u = (int32_t)((int64_t)u * tile * 16 / 4096);
    vb[k].v = (int32_t)((int64_t)v * tile * 16 / 4096);
}

static int axis_of(ok_v3 n)
{
    ok_fx x = n.x < 0 ? -n.x : n.x, y = n.y < 0 ? -n.y : n.y, z = n.z < 0 ? -n.z : n.z;
    return x >= y && x >= z ? 0 : y >= z ? 1 : 2;
}

void ok_r3d_quad(ok_v3 a, ok_v3 b, ok_v3 c, ok_v3 d)
{
    ok_v3 n = norm(cross(sub(b, a), sub(c, a)));
    put(0, a, n);
    put(1, b, n);
    put(2, c, n);
    put(3, d, n);
    if (tile) {
        int ax = axis_of(n);
        planar(0, a, ax);
        planar(1, b, ax);
        planar(2, c, ax);
        planar(3, d, ax);
    } else {
        vb[1].u = vb[2].u = uv_w * 16;
        vb[0].v = vb[1].v = uv_h * 16;
    }
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
            if (tile) planar(k, p, f / 2);
        }
        quad_idx(0, 1, 2, 3);
    }
}

/* A latitude-longitude grid's rings, worked out once per shape: the sines
 * and cosines, and the texture coordinates (once around from the back, so
 * the front lands in the middle of the strip). */
#define GRID_MAX 40
static ok_fx th_s[GRID_MAX], th_c[GRID_MAX], ph_s[GRID_MAX], ph_c[GRID_MAX];
static int32_t grid_u[GRID_MAX], grid_v[GRID_MAX];

static int grid_setup(int stacks, int slices, int rows)
{
    int i, j;
    if (stacks >= GRID_MAX || slices >= GRID_MAX || rows >= GRID_MAX || stacks < 1 || slices < 1) return 0;
    for (i = 0; i <= stacks; i++) {
        int th = i * (OK_TURN / 2) / stacks;
        th_s[i] = ok_r3d_sin(th);
        th_c[i] = ok_r3d_cos(th);
    }
    for (j = 0; j <= slices; j++) {
        int ph = j * OK_TURN / slices;
        ph_s[j] = ok_r3d_sin(ph);
        ph_c[j] = ok_r3d_cos(ph);
        grid_u[j] = (int32_t)(((int64_t)slices * 3 - (int64_t)j * 4) * uv_w * 16 / (4 * slices));
    }
    for (i = 0; i <= rows; i++) grid_v[i] = (int32_t)((int64_t)i * uv_h * 16 / rows);
    return 1;
}

static ok_v3 sphere_at(int i, int j)
{
    return ok_v3_make(ok_fx_mul(th_s[i], ph_c[j]), th_c[i], ok_fx_mul(th_s[i], ph_s[j]));
}

static void set_uv(int k, int i, int j)
{
    vb[k].u = grid_u[j];
    vb[k].v = grid_v[i];
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
    int i, j, round_shape = r.x == r.y && r.y == r.z; /* a sphere's normal is its point */
    int64_t ix, iy, iz;
    if ((stacks + 1) * (slices + 1) > MAX_VERTS || !r.x || !r.y || !r.z || !grid_setup(stacks, slices, stacks)) return;
    /* The normal leans by 1/radius on each axis; the smallest radius gets 1. */
    {
        ok_fx m = r.x < r.y ? (r.x < r.z ? r.x : r.z) : (r.y < r.z ? r.y : r.z);
        ix = divide((int64_t)m * 4096, r.x);
        iy = divide((int64_t)m * 4096, r.y);
        iz = divide((int64_t)m * 4096, r.z);
    }
    for (i = 0; i <= stacks; i++)
        for (j = 0; j <= slices; j++) {
            ok_v3 s = sphere_at(i, j);
            ok_v3 p = add(c, ok_v3_make(ok_fx_mul(s.x, r.x), ok_fx_mul(s.y, r.y), ok_fx_mul(s.z, r.z)));
            ok_v3 n = round_shape ? s : norm(ok_v3_make((ok_fx)(s.x * ix >> 12), (ok_fx)(s.y * iy >> 12), (ok_fx)(s.z * iz >> 12)));
            put(i * (slices + 1) + j, p, n);
            set_uv(i * (slices + 1) + j, i, j);
        }
    grid(stacks, slices);
}

/* A sphere cut at the equator, its top half moved to b and its bottom to a. */
void ok_r3d_capsule(ok_v3 a, ok_v3 b, ok_fx radius, int slices)
{
    enum { HALF = 2 }; /* rings per half: low poly, cheap */
    ok_v3 w = norm(sub(b, a)), helper = w.y > 3000 || w.y < -3000 ? ok_v3_make(4096, 0, 0) : ok_v3_make(0, 4096, 0);
    ok_v3 u = norm(cross(w, helper)), v = cross(u, w);
    int i, j, rows = 2 * HALF + 1;
    if ((rows + 1) * (slices + 1) > MAX_VERTS || !grid_setup(2 * HALF, slices, rows)) return;
    for (i = 0; i <= rows; i++)
        for (j = 0; j <= slices; j++) {
            ok_v3 s = sphere_at(i <= HALF ? i : i - 1, j);
            ok_v3 n = add(add(scale(u, s.x), scale(w, s.y)), scale(v, s.z));
            put(i * (slices + 1) + j, add(i <= HALF ? b : a, scale(n, radius)), n);
            set_uv(i * (slices + 1) + j, i, j);
        }
    grid(rows, slices);
}

void ok_r3d_cylinder(ok_v3 base, ok_fx radius, ok_fx height, int slices)
{
    ok_r3d_cone(base, radius, radius, height, slices);
}

void ok_r3d_cone(ok_v3 base, ok_fx radius, ok_fx top, ok_fx height, int slices)
{
    int j, k = slices + 1;
    if (2 * k + 2 > MAX_VERTS || !height) return;
    for (j = 0; j <= slices; j++) {
        ok_v3 ring = ok_v3_make(ok_r3d_cos(j * OK_TURN / slices), 0, ok_r3d_sin(j * OK_TURN / slices));
        ok_v3 n = norm(ok_v3_make(ring.x, (ok_fx)divide((int64_t)(radius - top) * 4096, height), ring.z));
        ok_v3 p = add(base, ok_v3_make(ok_fx_mul(ring.x, radius), 0, ok_fx_mul(ring.z, radius)));
        put(j, p, n);
        put(k + j, add(base, ok_v3_make(ok_fx_mul(ring.x, top), height, ok_fx_mul(ring.z, top))), n);
        vb[j].u = vb[k + j].u = j * uv_w * 16 / slices;
        vb[j].v = tile ? (int32_t)((int64_t)height * tile * 16 / 4096) : uv_h * 16;
        vb[k + j].v = 0;
    }
    grid(1, slices);
    /* Caps: the side vertices again, with flat normals. */
    for (j = 0; j <= slices; j++) {
        ok_v3 n = ok_v3_make(ok_r3d_cos(j * OK_TURN / slices), 0, ok_r3d_sin(j * OK_TURN / slices));
        ok_v3 p = add(base, ok_v3_make(ok_fx_mul(n.x, top), height, ok_fx_mul(n.z, top)));
        put(j, p, ok_v3_make(0, 4096, 0));
        vb[j].u = (n.x + 4096) * uv_w * 8 / 4096;
        vb[j].v = (n.z + 4096) * uv_h * 8 / 4096;
        if (tile) planar(j, p, 1);
    }
    put(k, add(base, ok_v3_make(0, height, 0)), ok_v3_make(0, 4096, 0));
    vb[k].u = uv_w * 8;
    vb[k].v = uv_h * 8;
    if (tile) planar(k, add(base, ok_v3_make(0, height, 0)), 1);
    for (j = 0; j < slices; j++) tri(k, j, j + 1);
}

/* Side walls between two outlines at z = +-depth, normals pointing away from
 * (cx, cy). */
static void walls(const int16_t (*pts)[2], int n, ok_fx depth, int cx, int cy, int closed)
{
    int i;
    for (i = 0; i < n - !closed; i++) {
        const int16_t *p = pts[i], *q = pts[(i + 1) % n];
        int mx = (p[0] + q[0]) / 2 - cx, my = (p[1] + q[1]) / 2 - cy;
        ok_v3 nn = norm(ok_v3_make((q[1] - p[1]) * 64, -(q[0] - p[0]) * 64, 0));
        if ((int64_t)nn.x * mx + (int64_t)nn.y * my < 0) nn = ok_v3_make(-nn.x, -nn.y, 0);
        put(MAX_VERTS - 4, ok_v3_make(p[0] * 4096 / 100, p[1] * 4096 / 100, depth), nn);
        put(MAX_VERTS - 3, ok_v3_make(q[0] * 4096 / 100, q[1] * 4096 / 100, depth), nn);
        put(MAX_VERTS - 2, ok_v3_make(q[0] * 4096 / 100, q[1] * 4096 / 100, -depth), nn);
        put(MAX_VERTS - 1, ok_v3_make(p[0] * 4096 / 100, p[1] * 4096 / 100, -depth), nn);
        quad_idx(MAX_VERTS - 4, MAX_VERTS - 3, MAX_VERTS - 2, MAX_VERTS - 1);
    }
}

void ok_r3d_extrude(const int16_t (*pts)[2], int n, ok_fx depth, int cx, int cy)
{
    int i, f;
    if (n < 3 || 2 * (n + 1) > MAX_VERTS - 4) return;
    for (f = 0; f < 2; f++) {
        ok_fx z = f ? -depth : depth;
        ok_v3 nn = ok_v3_make(0, 0, f ? -4096 : 4096);
        int base = f * (n + 1);
        put(base + n, ok_v3_make(cx * 4096 / 100, cy * 4096 / 100, z), nn);
        for (i = 0; i < n; i++) put(base + i, ok_v3_make(pts[i][0] * 4096 / 100, pts[i][1] * 4096 / 100, z), nn);
        for (i = 0; i < n; i++) tri(base + n, base + i, base + (i + 1) % n);
    }
    walls(pts, n, depth, cx, cy, 1);
}

void ok_r3d_ribbon(const int16_t (*outer)[2], const int16_t (*inner)[2], int n, ok_fx depth)
{
    int i, f;
    if (n < 2 || 4 * n > MAX_VERTS - 4) return;
    for (f = 0; f < 2; f++) {
        ok_fx z = f ? -depth : depth;
        ok_v3 nn = ok_v3_make(0, 0, f ? -4096 : 4096);
        int base = f * 2 * n;
        for (i = 0; i < n; i++) {
            put(base + i, ok_v3_make(outer[i][0] * 4096 / 100, outer[i][1] * 4096 / 100, z), nn);
            put(base + n + i, ok_v3_make(inner[i][0] * 4096 / 100, inner[i][1] * 4096 / 100, z), nn);
        }
        for (i = 0; i + 1 < n; i++) quad_idx(base + i, base + i + 1, base + n + i + 1, base + n + i);
    }
    /* The outer wall faces away from the inner edge, the inner one away from the outer. */
    for (i = 0; i + 1 < n; i++) {
        int k;
        for (k = 0; k < 2; k++) {
            const int16_t *p = k ? inner[i] : outer[i], *q = k ? inner[i + 1] : outer[i + 1], *o = k ? outer[i] : inner[i];
            int16_t two[2][2];
            two[0][0] = p[0], two[0][1] = p[1], two[1][0] = q[0], two[1][1] = q[1];
            walls((const int16_t (*)[2])two, 2, depth, o[0], o[1], 0);
        }
    }
}

void ok_r3d_disc(ok_v3 c, ok_fx rx, ok_fx rz, int slices)
{
    int j;
    ok_v3 up = ok_v3_make(0, 4096, 0);
    if (slices + 2 > MAX_VERTS) return;
    put(slices + 1, c, up);
    vb[slices + 1].u = uv_w * 8; /* a textured disc shows its rectangle once, like a rug, */
    vb[slices + 1].v = uv_h * 8;
    if (tile) planar(slices + 1, c, 1); /* or, tiling, lies flat across it like a lawn */
    for (j = 0; j <= slices; j++) {
        ok_fx cs = ok_r3d_cos(j * OK_TURN / slices), sn = ok_r3d_sin(j * OK_TURN / slices);
        put(j, add(c, ok_v3_make(ok_fx_mul(cs, rx), 0, ok_fx_mul(sn, rz))), up);
        vb[j].u = (cs + 4096) * uv_w * 8 / 4096;
        vb[j].v = (sn + 4096) * uv_h * 8 / 4096;
        if (tile) planar(j, add(c, ok_v3_make(ok_fx_mul(cs, rx), 0, ok_fx_mul(sn, rz))), 1);
    }
    for (j = 0; j < slices; j++) tri(slices + 1, j, j + 1);
}

void ok_r3d_keep(void)
{
#ifdef OK_R3D_GL
    if (gl_on) { /* the 2D layer, and the commands recorded since ok_r3d_begin */
        if (gl_open) gx(FIFO_END, 0, 0, 0);
        gl_open = 0;
        gl_dirty = 1;
        memcpy(gx_kept + 1, gx_rec + 1, (size_t)gx_n * 4);
        gx_kept_ok = gx_n;
        gx_world_n = 0; /* the buffer is the kept frame's now */
        gx_kept_polys = triangles;
        gx_kept_clear = gl_clear;
        gx_kept_alpha = gl_clear_alpha;
        gx_recording = 0;
        return;
    }
#endif
    memcpy(keep_fb, fb, sizeof keep_fb);
    memcpy(keep_z, zbuf, sizeof keep_z);
    memcpy(keep_ids, ids, sizeof keep_ids);
    memcpy(keep_edge_on, edge_on, sizeof keep_edge_on);
    memcpy(keep_edge_colour, edge_colour, sizeof keep_edge_colour);
    dirty_x0 = dirty_y0 = W;
    dirty_x1 = dirty_y1 = -1;
}

/* The colours come back whole (the caller may have drawn over any of them);
 * depth and objects only where something was drawn since. */
void ok_r3d_restore(uint16_t *frame)
{
    int y;
    fb = frame;
#ifdef OK_R3D_GL
    if (gl_on) { /* the 2D layer see-through again, and the kept commands replayed; then live */
        memset(fb, 0, sizeof keep_fb);
        if (gl_open) gx(FIFO_END, 0, 0, 0);
        gl_open = 0;
        gl_dirty = 1;
        gx_recording = 0;
        gx_play(gx_kept, gx_kept_ok);
        gl_polys = gx_kept_polys;
        gl_clear = gx_kept_clear;
        gl_clear_alpha = gx_kept_alpha;
        triangles = 0;
        memset(stack, 0, sizeof stack);
        sp = 0;
        stack[0].m[0][0] = stack[0].m[1][1] = stack[0].m[2][2] = 4096;
        tile = 0;
        flatten = 0;
        return;
    }
#endif
    memcpy(fb, keep_fb, sizeof keep_fb);
    for (y = dirty_y0; y <= dirty_y1; y++) {
        memcpy(zbuf + y * W + dirty_x0, keep_z + y * W + dirty_x0, (size_t)(dirty_x1 - dirty_x0 + 1) * sizeof *zbuf);
        memcpy(ids + y * W + dirty_x0, keep_ids + y * W + dirty_x0, (size_t)(dirty_x1 - dirty_x0 + 1));
    }
    memcpy(edge_on, keep_edge_on, sizeof edge_on);
    memcpy(edge_colour, keep_edge_colour, sizeof edge_colour);
    dirty_x0 = dirty_y0 = W;
    dirty_x1 = dirty_y1 = -1;
    memset(stack, 0, sizeof stack);
    sp = 0;
    stack[0].m[0][0] = stack[0].m[1][1] = stack[0].m[2][2] = 4096;
    triangles = 0;
    tile = 0;
    flatten = 0;
}

void ok_r3d_finish(void)
{
    int x, y, x0 = dirty_x0 - 1, y0 = dirty_y0 - 1, x1 = dirty_x1 + 1, y1 = dirty_y1 + 1;
#ifdef OK_R3D_GL
    if (gl_on) return; /* the hardware marks the edges */
#endif
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int i = y * W + x, id = ids[i];
            int32_t z = zbuf[i], gap = z >> 4;
            if (!edge_on[id]) continue;
            if ((x > 0 && ids[i - 1] != id && z - zbuf[i - 1] > gap) || (x < W - 1 && ids[i + 1] != id && z - zbuf[i + 1] > gap) ||
                (y > 0 && ids[i - W] != id && z - zbuf[i - W] > gap) || (y < H - 1 && ids[i + W] != id && z - zbuf[i + W] > gap))
                fb[i] = edge_colour[id];
        }
}

int ok_r3d_project(ok_v3 p, int *x, int *y, int32_t *depth)
{
    projecting = 1;
    put(0, p, ok_v3_make(0, 4096, 0));
    projecting = 0;
    if (!vb[0].ok) return 0;
    *x = vb[0].x >> 4;
    *y = vb[0].y >> 4;
    if (depth) *depth = vb[0].z;
    return 1;
}

int32_t ok_r3d_depth_at(int x, int y)
{
#ifdef OK_R3D_GL
    if (gl_on) return 0;
#endif
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;
    return zbuf[y * W + x];
}

int ok_r3d_id_at(int x, int y)
{
#ifdef OK_R3D_GL
    if (gl_on) return 0;
#endif
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;
    return ids[y * W + x] >= 250 ? 0 : ids[y * W + x]; /* shadows are not objects */
}

uint16_t *ok_r3d_frame(void)
{
    return fb;
}

int ok_r3d_triangles(void)
{
    return triangles;
}
