/* What each scene costs to draw, counted on the host: divisions, vertices,
 * triangles, rows and pixels per frame (r3d built with OK_R3D_STATS), and
 * the host's own time. Host only, not a measurement of the DSi.
 *   build/okupa-perf */
#include "okupa_font.h"
#include "okupa_intro.h"
#include "okupa_r3d.h"
#include "okupa_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint16_t top[OK_UI_W * OK_UI_H], bottom[OK_UI_W * OK_UI_H];

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* frames frames of the scene, one render per two ticks as on the console. */
static void scene(const char *name, ok_ui *u, ok_pet *p, int hour, int frames)
{
    uint32_t st[OK_R3D_STATS_N], sum[OK_R3D_STATS_N] = {0}, worst[OK_R3D_STATS_N] = {0};
    double t0, ms = 0;
    int f, k;
    ok_r3d_stats(st, 1);
    for (f = 0; f < frames; f++) {
        ok_ui_tick(u, p);
        ok_ui_tick(u, p);
        t0 = now_ms();
        ok_ui_render(u, p, hour, 10, ok_font(), top, bottom);
        ms += now_ms() - t0;
        ok_r3d_stats(st, 1);
        for (k = 0; k < OK_R3D_STATS_N; k++) {
            sum[k] += st[k];
            if (st[k] > worst[k]) worst[k] = st[k];
        }
    }
    printf("%-14s per frame: %6u divs %6u verts %5u tris %6u rows %7u px | worst %6u divs %5u tris %7u px | host %.3f ms\n", name,
           sum[0] / frames, sum[1] / frames, sum[2] / frames, sum[3] / frames, sum[4] / frames, worst[0], worst[2], worst[4], ms / frames);
}

int main(void)
{
    static ok_ui u;
    static ok_pet p;
    ok_pet_init(&p, 1800000000);
    ok_ui_init(&u);
    ok_ui_start_intro(&u);
    scene("splash", &u, &p, 20, 150);
    u.scene = OK_UI_FILM;
    u.intro_t = 0;
    scene("film", &u, &p, 20, OK_FILM_TICKS / 2 - 5);
    u.scene = OK_UI_HOME;
    scene("attic (day)", &u, &p, 11, 300);
    scene("attic (night)", &u, &p, 22, 300);
    ok_ui_direct(&u, &p, OK_MOVE_TO_DOOR, OK_PLACE_GARDEN, -1, NULL);
    while (ok_house_where() != OK_PLACE_GARDEN) ok_ui_tick(&u, &p);
    scene("garden", &u, &p, 11, 300);
    return 0;
}
