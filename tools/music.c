/* The okupa's music as a WAV file, made by the game's own synthesiser:
 * the day tune, a few sound effects, then night and the lullaby.
 *   build/okupa-music <out.wav> */
#include "okupa_music.h"
#include <stdio.h>

#define RATE 16384
static int16_t pcm[RATE * 100];

static void le(FILE *f, uint32_t v, int n)
{
    int i;
    for (i = 0; i < n; i++) fputc((int)(v >> (8 * i) & 255), f);
}

int main(int argc, char **argv)
{
    static ok_music m;
    const char *path = argc > 1 ? argv[1] : "build/preview/okupa-music.wav";
    int n = 0, s;
    FILE *f;
    ok_music_init(&m, RATE);
    ok_music_tune(&m, OK_TUNE_INTRO);
    ok_music_sfx(&m, OK_SFX_JINGLE);
    for (s = 0; s < 60; s++) { /* one second at a time */
        if (s == 16) ok_music_tune(&m, OK_TUNE_DAY);
        if (s == 12 + 16) ok_music_sfx(&m, OK_SFX_TAP);
        if (s == 29 || s == 30 || s == 31) ok_music_sfx(&m, OK_SFX_EAT);
        if (s == 32) ok_music_sfx(&m, OK_SFX_YAY);
        if (s == 34) ok_music_sfx(&m, OK_SFX_BOING);
        if (s == 35) ok_music_sfx(&m, OK_SFX_NOPE);
        if (s == 36) ok_music_sfx(&m, OK_SFX_POP);
        if (s == 37) ok_music_sfx(&m, OK_SFX_HELLO);
        if (s == 38) ok_music_tune(&m, OK_TUNE_NIGHT);
        if (s == 50) ok_music_tune(&m, OK_TUNE_SLEEP);
        ok_music_render(&m, pcm + n, RATE);
        n += RATE;
    }
    if (!(f = fopen(path, "wb"))) {
        perror(path);
        return 1;
    }
    fwrite("RIFF", 1, 4, f);
    le(f, 36 + (uint32_t)n * 2, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    le(f, 16, 4);
    le(f, 1, 2);
    le(f, 1, 2);
    le(f, RATE, 4);
    le(f, RATE * 2, 4);
    le(f, 2, 2);
    le(f, 16, 2);
    fwrite("data", 1, 4, f);
    le(f, (uint32_t)n * 2, 4);
    for (s = 0; s < n; s++) le(f, (uint16_t)pcm[s], 2);
    fclose(f);
    printf("okupa-music: %s (%d s)\n", path, n / RATE);
    return 0;
}
