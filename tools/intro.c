/* The splash and the opening film, frame by frame, drawn with the game's
 * own code. Host only.
 *   build/okupa-intro <frames dir>     one PNG (both screens) per 1/20 s */
#include "okupa_intro.h"
#include "png.h"

static uint16_t top[256 * 192], bottom[256 * 192], canvas[256 * 392];

static void shot(const char *dir, int n)
{
    char path[512];
    int i;
    for (i = 0; i < 256 * 392; i++) canvas[i] = 0x8000;
    memcpy(canvas, top, sizeof top);
    memcpy(canvas + 256 * 200, bottom, sizeof bottom);
    snprintf(path, sizeof path, "%s/i%04d.png", dir, n);
    png_write(path, canvas, 256, 392, 1);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/preview";
    uint32_t t;
    int n = 0;
    for (t = 0; t < OK_SPLASH_TICKS; t += 3) {
        ok_intro_splash(top, bottom, t);
        shot(dir, n++);
    }
    for (t = 0; t < OK_FILM_TICKS; t += 3) {
        ok_intro_film(top, bottom, t);
        shot(dir, n++);
    }
    return 0;
}
