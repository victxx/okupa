#ifndef OKUPA_R3D_H
#define OKUPA_R3D_H

#include <stdint.h>

/* A tiny 3D renderer in fixed point: triangles, a depth buffer, toon shading
 * in four tones and outlines between objects, drawn into a 256x192 frame of
 * DS colours. Integer maths only (the DSi has no FPU), no heap, so the
 * console, the host preview and a phone draw exactly the same pixels.
 *
 * Numbers are Q12 (4096 = 1.0); in the house, 1.0 is about 10 cm. Angles are
 * in 1/4096 of a turn. y is up; the camera looks from eye towards a target. */
typedef int32_t ok_fx;
#define OK_FX(v) ((ok_fx)((v) * 4096.0 + ((v) < 0 ? -0.5 : 0.5)))
#define OK_TURN 4096
#define OK_R3D_W 256
#define OK_R3D_H 192

typedef struct {
    ok_fx x, y, z;
} ok_v3;

/* Material flags. */
#define OK_R3D_UNLIT 1  /* its own colour, whatever the light */
#define OK_R3D_EDGE 2   /* outlined where it stands in front of another object */
#define OK_R3D_SHADOW 4 /* darkens what is drawn by (red of colour)/32, once per id 0..4; layers stack */
#define OK_R3D_BOTH 8   /* both sides visible */
#define OK_R3D_TEXTURED 16 /* colours from the texture, lit like the rest */
#define OK_R3D_FLAT 32 /* one light per triangle: low-poly facets */
#define OK_R3D_CUTOUT 64 /* textured: texels without bit 15 are see-through */

static inline ok_v3 ok_v3_make(ok_fx x, ok_fx y, ok_fx z)
{
    ok_v3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
#define OK_V3(x, y, z) ok_v3_make(OK_FX(x), OK_FX(y), OK_FX(z))

ok_fx ok_fx_mul(ok_fx a, ok_fx b);
ok_fx ok_r3d_sin(int angle);
ok_fx ok_r3d_cos(int angle);

/* Starts a frame: everything is background colour and far away. */
void ok_r3d_begin(uint16_t *frame, uint16_t background);
void ok_r3d_camera(ok_v3 eye, ok_v3 target, int focal_px);
/* to_light points at the light; colours are Q12 per channel (1.0 = as is). */
void ok_r3d_light(ok_v3 to_light, ok_v3 light, ok_v3 ambient);
/* colour is a DS colour; id names the object for outlines (1..254). */
void ok_r3d_material(uint16_t colour, int id, int flags);

/* The texture for TEXTURED materials: texels of DS colours, stride texels
 * per row. Each ellipsoid or capsule wraps around one rectangle of it: left
 * to right once around (the front in the middle, the seam at the back), top
 * to bottom from its top pole to its bottom one. w must be a power of two. */
void ok_r3d_texture(const uint16_t *texels, int stride);
void ok_r3d_uv(int x, int y, int w, int h);
/* With texels_per_unit > 0, boxes, quads and cylinder sides tile the
 * rectangle (both sides powers of two) across their faces instead of
 * stretching it (discs and caps lie flat, like a lawn); untiled, a disc
 * shows its rectangle once. 0 turns tiling off. */
void ok_r3d_tile(int texels_per_unit);
/* 0..256: how far textured pixels lean towards their rectangle's average
 * colour (set before ok_r3d_uv); a flat look with only a hint of texture. */
void ok_r3d_flatten(int amount);

/* The model transform, a small stack. */
void ok_r3d_push(void);
void ok_r3d_pop(void);
void ok_r3d_translate(ok_v3 t);
void ok_r3d_rotate_x(int angle);
void ok_r3d_rotate_y(int angle);
void ok_r3d_rotate_z(int angle);
void ok_r3d_scale(ok_fx sx, ok_fx sy, ok_fx sz);
/* atan2(y, x) in 1/4096 turns. */
int ok_r3d_atan2(int32_t y, int32_t x);

void ok_r3d_box(ok_v3 lo, ok_v3 hi);
void ok_r3d_ellipsoid(ok_v3 centre, ok_v3 radius, int slices, int stacks);
void ok_r3d_capsule(ok_v3 a, ok_v3 b, ok_fx radius, int slices);
/* Upright cylinder from y0 to y1, centred on x, z. */
void ok_r3d_cylinder(ok_v3 base, ok_fx radius, ok_fx height, int slices);
/* Upright cone or frustum: radius at the base, top at the top. */
void ok_r3d_cone(ok_v3 base, ok_fx radius, ok_fx top, ok_fx height, int slices);
/* A flat shape in the xy plane (points in hundredths of a unit), extruded
 * to z = +-depth; its faces are fans from (cx, cy), which must see the
 * whole outline. */
void ok_r3d_extrude(const int16_t (*pts)[2], int n, ok_fx depth, int cx, int cy);
/* A band between two outlines of n points each (a crescent, an arc), extruded. */
void ok_r3d_ribbon(const int16_t (*outer)[2], const int16_t (*inner)[2], int n, ok_fx depth);
/* A flat ellipse facing up (shadows, rugs). */
void ok_r3d_disc(ok_v3 centre, ok_fx rx, ok_fx rz, int slices);
/* A flat quad, corners counterclockwise as seen from its front; textured,
 * a..d take the uv rectangle's corners from its bottom left. */
void ok_r3d_quad(ok_v3 a, ok_v3 b, ok_v3 c, ok_v3 d);

/* Draws the outlines of what was drawn; the frame is then complete. */
void ok_r3d_finish(void);
/* Keeps the frame drawn so far (colours, depth, objects), and starts a new
 * frame from it: a still room is drawn once, then only what moves. */
void ok_r3d_keep(void);
void ok_r3d_restore(uint16_t *frame);
/* Where a point (in the current transform) lands on screen; 0 if behind the
 * camera. depth (may be NULL) is comparable with ok_r3d_depth_at. */
int ok_r3d_project(ok_v3 p, int *x, int *y, int32_t *depth);
/* Depth drawn at a pixel: larger is nearer, 0 = nothing. */
int32_t ok_r3d_depth_at(int x, int y);
/* The object drawn at a pixel (0 = background). */
int ok_r3d_id_at(int x, int y);
/* The frame being drawn. */
uint16_t *ok_r3d_frame(void);
/* Triangles drawn since ok_r3d_begin (the DSi hardware takes about 2048). */
int ok_r3d_triangles(void);
/* On the DS's 3D hardware (built with OK_R3D_GL, after ok_r3d_hardware(1))
 * the frame passed to ok_r3d_begin becomes the 2D layer drawn over the 3D,
 * a kept frame is a recorded command list replayed each frame, and
 * ok_r3d_depth_at/id_at answer 0. ok_r3d_retains() is 1 for both. */
int ok_r3d_retains(void);
/* Turns the hardware renderer on (builds with OK_R3D_GL; nothing otherwise). */
void ok_r3d_hardware(int on);
/* 1 once the 3D goes to the hardware: no depth or objects to read back,
 * and the 2D layer is see-through over the 3D. */
int ok_r3d_on_hardware(void);
/* The 3D layer's background is see-through this frame (a sky behind it). */
void ok_r3d_see_through(void);
/* On the hardware: keep what was drawn since the camera was set, and draw
 * it again under the current camera (0 if nothing is kept); forget it. */
void ok_r3d_world_keep(void);
int ok_r3d_world_again(void);
void ok_r3d_world_forget(void);
/* Shows the frame drawn on the hardware (once a frame; nothing in software). */
void ok_r3d_swap(void);
/* The texels behind a texture changed (the hardware keeps uploaded copies). */
void ok_r3d_texture_changed(const uint16_t *texels);
/* A faster 64-bit division, if the platform has one (the DS's hardware divider). */
void ok_r3d_divider(int64_t (*divide)(int64_t n, int64_t d));
/* Work counted since the last clear, when built with OK_R3D_STATS (zeros otherwise). */
enum { OK_R3D_STAT_DIVS, OK_R3D_STAT_VERTS, OK_R3D_STAT_TRIS, OK_R3D_STAT_ROWS, OK_R3D_STAT_PIXELS, OK_R3D_STATS_N };
void ok_r3d_stats(uint32_t out[OK_R3D_STATS_N], int clear);

#endif
