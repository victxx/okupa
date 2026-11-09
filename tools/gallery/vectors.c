/* Acts made by the console's own code, for the gallery's decoder test:
 * one line each, "kind hex". Patterns: a drawing inks (x * y + x) % 5 == 0,
 * a photo has tone (x + 2 * y) % 4, a letter and a hello. */
#include "okupa_act.h"
#include <stdio.h>

static void line(const char *kind, const uint8_t *b, size_t n)
{
    size_t i;
    printf("%s ", kind);
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void)
{
    static uint8_t pixels[OK_DRAWING_BYTES], tones[OK_PHOTO_BYTES], out[OK_ACT_MAX];
    unsigned x, y;
    size_t n;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            ok_drawing_set(pixels, x, y, (x * y + x) % 5 == 0);
            ok_photo_set(tones, x, y, (x + 2 * y) % 4);
        }
    if (ok_act_drawing(pixels, out, sizeof out, &n) != OK_OK) return 1;
    line("drawing", out, n);
    if (ok_act_photo(tones, out, sizeof out, &n) != OK_OK) return 1;
    line("photo", out, n);
    if (ok_act_letter("Hi from the attic!", out, sizeof out, &n) != OK_OK) return 1;
    line("letter", out, n);
    if (ok_act_hello(out, sizeof out, &n) != OK_OK) return 1;
    line("hello", out, n);
    return 0;
}
