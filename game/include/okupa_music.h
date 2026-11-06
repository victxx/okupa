#ifndef OKUPA_MUSIC_H
#define OKUPA_MUSIC_H

#include <stdint.h>

/* The okupa's music and sounds: a small synthesiser and sequencer in
 * integer C, so the console and the host make the same samples. One
 * original tune in three arrangements (day, night, a lullaby while it
 * sleeps) and short sound effects mixed over it. Mono, 16-bit. */
enum { OK_TUNE_DAY, OK_TUNE_NIGHT, OK_TUNE_SLEEP, OK_TUNE_QUIET, OK_TUNES };
enum { OK_SFX_TAP, OK_SFX_EAT, OK_SFX_YAY, OK_SFX_BOING, OK_SFX_NOPE, OK_SFX_POP, OK_SFX_HELLO, OK_SFXS };
#define OK_MUSIC_VOICES 12

typedef struct {
    uint32_t phase, inc, mphase;
    int32_t amp, peak, rise, decay, mod, mod_decay, sweep, ratio, prev;
    int kind;
} ok_voice;

typedef struct {
    ok_voice v[OK_MUSIC_VOICES];
    uint32_t rate, a4, step_len, until_step, swing, seed, sfx_t;
    int step, tune, next_tune, sfx;
    int32_t level, level_goal, hp;
} ok_music;

void ok_music_init(ok_music *m, uint32_t rate);
/* Changes arrangement at the next bar, with a short fade. */
void ok_music_tune(ok_music *m, int tune);
void ok_music_sfx(ok_music *m, int sfx);
void ok_music_render(ok_music *m, int16_t *out, int n);

#endif
