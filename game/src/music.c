#include "okupa_music.h"
#include <string.h>

/* sin over one turn, 256 steps (one extra for interpolation), Q15. */
static const int16_t sine[257] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
    0,
};

/* 2^(k/12), Q16. */
static const uint32_t semitone[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};

/* "Edge of the map", for the opening: A minor, held chords and a few bells
 * that never quite resolve. Am(add9), Fmaj7, Dm9, E7sus4 - E, twice. */
static const uint8_t intro_chords[4][4] = {{45, 52, 59, 60}, {41, 48, 52, 57}, {38, 45, 52, 53}, {40, 47, 50, 57}};
static const uint8_t intro_bells[64] = {
    76, 0, 0, 0, 0, 0, 71, 0, 72, 0, 0, 0, 69, 0, 0, 0,
    77, 0, 0, 0, 0, 0, 72, 0, 76, 0, 0, 0, 0, 0, 0, 0,
    74, 0, 0, 0, 0, 0, 69, 0, 72, 0, 0, 0, 77, 0, 76, 0,
    71, 0, 0, 0, 0, 0, 68, 0, 71, 0, 0, 0, 0, 0, 0, 0,
};

/* "Attic morning": eight bars in F, sixteen steps a bar; 0 = no new note. */
static const uint8_t melody[128] = {
    69, 0, 0, 72, 0, 0, 77, 0, 76, 0, 72, 0, 74, 0, 0, 0,
    76, 0, 0, 74, 0, 0, 71, 0, 67, 0, 0, 0, 69, 0, 0, 0,
    69, 0, 0, 72, 0, 0, 77, 0, 79, 0, 77, 0, 76, 0, 74, 0,
    76, 0, 0, 0, 72, 0, 0, 0, 67, 0, 0, 0, 0, 0, 67, 0,
    77, 0, 0, 74, 0, 0, 70, 0, 69, 0, 70, 0, 74, 0, 0, 0,
    72, 0, 0, 76, 0, 0, 81, 0, 79, 0, 0, 0, 76, 0, 0, 0,
    74, 0, 77, 0, 79, 0, 77, 0, 74, 0, 0, 70, 72, 0, 0, 0,
    67, 0, 0, 0, 70, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 67,
};
/* Fmaj7, Em7, Dm7, Cadd9, Bbmaj7, Am7, Gm7, C7: four notes each, and the bass root. */
static const uint8_t chords[8][4] = {{53, 57, 60, 64}, {52, 55, 59, 62}, {50, 53, 57, 60}, {52, 55, 60, 62},
                                     {53, 57, 58, 62}, {52, 55, 57, 60}, {50, 53, 55, 58}, {52, 55, 58, 60}};
static const uint8_t roots[8] = {41, 40, 38, 36, 34, 33, 31, 36};

enum { K_OFF, K_PLUCK, K_EPIANO, K_BASS, K_KICK, K_HAT, K_SNAP, K_BELL, K_SWEEP, K_CRUNCH };
enum { V_LEAD, V_CHORD, V_BASS = 5, V_KICK, V_HAT, V_SNAP, V_SFX };

static int32_t sin_at(uint32_t phase)
{
    uint32_t i = phase >> 24, f = phase >> 16 & 255;
    return sine[i] + (((int32_t)sine[i + 1] - sine[i]) * (int32_t)f >> 8);
}

static uint32_t note_inc(const ok_music *m, int n)
{
    int d = n - 69, oct = d >= 0 ? d / 12 : -((11 - d) / 12), k = d - oct * 12;
    uint64_t inc = (uint64_t)m->a4 * semitone[k] >> 16;
    return (uint32_t)(oct >= 0 ? inc << oct : inc >> -oct);
}

/* Per-sample factor that falls to about a third in ms milliseconds. */
static int32_t fall(const ok_music *m, int ms)
{
    int32_t n = (int32_t)((uint32_t)ms * m->rate / 1000);
    return n < 2 ? 0 : 65536 - 65536 / n;
}

static uint32_t noise(ok_music *m)
{
    m->seed = m->seed * 1664525u + 1013904223u;
    return m->seed;
}

/* Starts a voice: peak is its loudest (65536 = full scale), attack and decay in ms. */
static void strike(ok_music *m, int i, int kind, int note, int32_t peak, int attack, int decay)
{
    ok_voice *v = &m->v[i];
    int32_t n = (int32_t)((uint32_t)(attack > 0 ? attack : 1) * m->rate / 1000);
    v->kind = kind;
    v->inc = note ? note_inc(m, note) : 0;
    v->peak = peak;
    v->rise = peak / (n > 0 ? n : 1) + 1;
    v->decay = fall(m, decay);
    v->ratio = 256;
    v->mod = 0;
    v->mod_decay = 65536;
    v->sweep = 0;
    if (kind == K_PLUCK || kind == K_BELL) {
        v->ratio = kind == K_PLUCK ? 512 : 768; /* the overtone of a soft mallet or a tine */
        v->mod = kind == K_PLUCK ? 1400 : 1600;
        v->mod_decay = fall(m, kind == K_PLUCK ? 40 : 250);
    } else if (kind == K_EPIANO) {
        v->mod = 900;
        v->mod_decay = fall(m, 220);
    } else if (kind == K_KICK) {
        v->inc = note_inc(m, 52);
        v->sweep = fall(m, 45); /* the pitch drops into a thump */
    } else if (kind == K_SNAP) {
        v->inc = note_inc(m, 50);
    }
    if (v->amp > peak) v->amp = peak;
}

static void sweep(ok_music *m, int i, int from, int to, int ms, int32_t peak)
{
    ok_voice *v = &m->v[i];
    int32_t n = (int32_t)((uint32_t)ms * m->rate / 1000);
    strike(m, i, K_SWEEP, from, peak, 3, ms);
    v->sweep = (int32_t)(((int64_t)note_inc(m, to) - v->inc) / (n > 0 ? n : 1));
}

static void set_tempo(ok_music *m)
{
    static const uint8_t bpm[OK_TUNES] = {88, 72, 58, 70, 66};
    m->step_len = m->rate * 60 / bpm[m->tune] / 4;
    m->swing = m->tune == OK_TUNE_DAY ? m->step_len / 7 : m->step_len / 12;
}

void ok_music_init(ok_music *m, uint32_t rate)
{
    memset(m, 0, sizeof *m);
    m->rate = rate;
    m->a4 = (uint32_t)(((uint64_t)440 << 32) / rate);
    m->seed = 0x2545f491u;
    m->tune = m->next_tune = OK_TUNE_DAY;
    m->level = 0;
    m->level_goal = 65536;
    m->sfx = -1;
    set_tempo(m);
}

void ok_music_tune(ok_music *m, int tune)
{
    if (tune >= 0 && tune < OK_TUNES) m->next_tune = tune;
}

void ok_music_sfx(ok_music *m, int sfx)
{
    m->sfx = sfx;
    m->sfx_t = 0;
}

/* A little life in each hit: velocity wanders a few percent. */
static int32_t human(ok_music *m, int32_t peak)
{
    return peak - peak / 16 + (int32_t)(noise(m) >> 16) % (peak / 8 + 1);
}

static void step(ok_music *m)
{
    int s = m->step, bar = s / 16, pos = s % 16, i;
    if (pos == 0 && m->next_tune != m->tune) {
        static const int32_t loud[OK_TUNES] = {65536, 56000, 40000, 0, 60000};
        m->tune = m->next_tune;
        m->level_goal = loud[m->tune];
        set_tempo(m);
    }
    switch (m->tune) {
    case OK_TUNE_DAY:
        if (melody[s]) strike(m, V_LEAD, K_PLUCK, melody[s] - 12, human(m, 13000), 4, 520);
        if (pos == 0 || pos == 6 || pos == 10)
            for (i = 0; i < 4; i++) strike(m, V_CHORD + i, K_EPIANO, chords[bar][i], human(m, pos ? 2600 : 4200), 6, pos ? 260 : 900);
        if (pos == 0) strike(m, V_BASS, K_BASS, roots[bar], 15000, 4, 700);
        if (pos == 8) strike(m, V_BASS, K_BASS, roots[bar] + 7, 11000, 4, 400);
        if (pos == 14) strike(m, V_BASS, K_BASS, roots[bar] + 12, 7000, 4, 160);
        if (pos == 0 || pos == 8) strike(m, V_KICK, K_KICK, 0, 17000, 1, 180);
        if (pos == 4 || pos == 12) strike(m, V_SNAP, K_SNAP, 0, 3200, 2, 80);
        if (pos % 4 == 3) strike(m, V_HAT, K_HAT, 0, human(m, 1100), 1, 30);
        break;
    case OK_TUNE_NIGHT:
        if (melody[s] && pos % 2 == 0) strike(m, V_LEAD, K_BELL, melody[s], human(m, 8000), 6, 900);
        if (pos == 0 || pos == 8)
            for (i = 0; i < 4; i++) strike(m, V_CHORD + i, K_EPIANO, chords[bar][i], human(m, pos ? 2000 : 3200), 25, 1400);
        if (pos == 0) strike(m, V_BASS, K_BASS, roots[bar], 10000, 10, 1200);
        if (pos % 8 == 6) strike(m, V_HAT, K_HAT, 0, human(m, 600), 1, 25);
        break;
    case OK_TUNE_INTRO: {
        int b = bar % 4;
        if (pos == 0)
            for (i = 0; i < 4; i++) strike(m, V_CHORD + i, K_EPIANO, intro_chords[b][i], 2600, 300, 2600);
        if (pos == 0) strike(m, V_BASS, K_BASS, intro_chords[b][0] - 12, 9000, 60, 2400);
        if (intro_bells[s % 64]) strike(m, V_LEAD, K_BELL, intro_bells[s % 64], human(m, 6500), 6, 1500);
        if (b == 3 && pos == 8) strike(m, V_CHORD + 3, K_EPIANO, 56, 2400, 200, 2000); /* the sus4 falls to E: G# */
        if (pos % 8 == 4) strike(m, V_HAT, K_HAT, 0, 500, 1, 60);
        break;
    }
    case OK_TUNE_SLEEP:
        if (pos == 0)
            for (i = 0; i < 4; i++) strike(m, V_CHORD + i, K_BELL, chords[bar][i], 1800, 80, 2200);
        if (melody[s] && pos % 4 == 0 && bar % 2 == 0) strike(m, V_LEAD, K_BELL, melody[s] - 12, 4500, 8, 1600);
        break;
    default: break;
    }
    m->step = (s + 1) % 128;
}

/* The parts of each sound effect, at their offsets in milliseconds. */
static void sfx_at(ok_music *m, int ms)
{
    static const uint8_t yay[4] = {84, 88, 91, 96};
    switch (m->sfx) {
    case OK_SFX_TAP:
        if (ms == 0) strike(m, V_SFX, K_PLUCK, 91, 9000, 1, 60);
        break;
    case OK_SFX_EAT:
        if (ms == 0 || ms == 140 || ms == 260) strike(m, V_SFX + 1, K_CRUNCH, 0, ms ? 7000 : 12000, 1, 60);
        if (ms == 30) strike(m, V_SFX + 2, K_CRUNCH, 0, 8000, 1, 50);
        if (ms >= 300) m->sfx = -1;
        break;
    case OK_SFX_YAY:
        if (ms % 70 == 0 && ms / 70 < 4) strike(m, V_SFX + ms / 70 % 3, K_PLUCK, yay[ms / 70], 9000, 1, 400);
        if (ms >= 400) m->sfx = -1;
        break;
    case OK_SFX_BOING:
        if (ms == 0) sweep(m, V_SFX, 55, 79, 220, 10000);
        break;
    case OK_SFX_NOPE:
        if (ms == 0) strike(m, V_SFX, K_EPIANO, 64, 9000, 2, 160);
        if (ms == 150) strike(m, V_SFX + 1, K_EPIANO, 60, 9000, 2, 260);
        if (ms >= 300) m->sfx = -1;
        break;
    case OK_SFX_POP:
        if (ms == 0) sweep(m, V_SFX, 79, 91, 70, 8000);
        break;
    case OK_SFX_JINGLE: { /* the splash: a rising bell arpeggio and a low note under it */
        static const uint8_t up[6] = {57, 64, 69, 72, 76, 81};
        if (ms == 0) strike(m, V_SFX + 2, K_BASS, 33, 9000, 10, 1400);
        if (ms % 110 == 0 && ms / 110 < 6) strike(m, V_SFX + (ms / 110) % 2, K_BELL, up[ms / 110], 8000, 2, 900);
        if (ms >= 700) m->sfx = -1;
        break;
    }
    case OK_SFX_HELLO:
        if (ms == 0) strike(m, V_SFX, K_BELL, 84, 8000, 2, 600);
        if (ms == 130) strike(m, V_SFX + 1, K_BELL, 91, 8000, 2, 800);
        if (ms >= 200) m->sfx = -1;
        break;
    default: break;
    }
}

void ok_music_render(ok_music *m, int16_t *out, int n)
{
    int k, i;
    for (k = 0; k < n; k++) {
        int32_t mix = 0;
        if (m->until_step == 0) {
            step(m);
            m->until_step = m->step % 2 ? m->step_len + m->swing : m->step_len - m->swing; /* swung: the off-beat lands late */
        }
        m->until_step--;
        if (m->sfx >= 0) {
            uint32_t per = m->rate / 1000;
            if (m->sfx_t % per == 0) sfx_at(m, (int)(m->sfx_t / per));
            if (++m->sfx_t > m->rate * 3) m->sfx = -1;
        }
        for (i = 0; i < OK_MUSIC_VOICES; i++) {
            ok_voice *v = &m->v[i];
            int32_t s;
            if (v->kind == K_OFF) continue;
            if (v->rise) {
                v->amp += v->rise;
                if (v->amp >= v->peak) v->amp = v->peak, v->rise = 0;
            } else {
                v->amp = (int32_t)((int64_t)v->amp * v->decay >> 16);
                if (v->amp < 4) {
                    v->kind = K_OFF;
                    continue;
                }
            }
            switch (v->kind) {
            case K_PLUCK:
            case K_BELL:
                s = sin_at(v->phase) + (sin_at(v->mphase) * v->mod >> 13);
                v->mphase += (uint32_t)((uint64_t)v->inc * (uint32_t)v->ratio >> 8);
                v->mod = (int32_t)((int64_t)v->mod * v->mod_decay >> 16);
                break;
            case K_EPIANO:
                s = sin_at(v->phase + (uint32_t)(sin_at(v->mphase) * v->mod * 16)); /* FM: the tine's bell-like attack */
                v->mphase += v->inc;
                v->mod = (int32_t)((int64_t)v->mod * v->mod_decay >> 16);
                break;
            case K_BASS:
                s = sin_at(v->phase) + sin_at(v->phase * 2) / 5;
                break;
            case K_KICK:
                s = sin_at(v->phase);
                if (v->inc > m->a4 / 10) /* down to about 44 Hz */
                    v->inc = m->a4 / 10 + (uint32_t)((uint64_t)(v->inc - m->a4 / 10) * (uint32_t)v->sweep >> 16);
                break;
            case K_HAT: {
                int32_t w = (int32_t)(noise(m) >> 17) - 16384;
                s = w - v->prev; /* white noise, its lows taken out */
                v->prev = w;
                break;
            }
            case K_SNAP: {
                int32_t w = (int32_t)(noise(m) >> 17) - 16384;
                s = (w - v->prev / 2) + sin_at(v->phase) / 3;
                v->prev = w;
                break;
            }
            case K_CRUNCH: {
                int32_t w = (int32_t)(noise(m) >> 17) - 16384;
                v->prev = (v->prev * 3 + w) / 4; /* softer, lower noise */
                s = v->prev * 2;
                break;
            }
            case K_SWEEP:
                s = sin_at(v->phase) + sin_at(v->phase * 2) / 4;
                v->inc += (uint32_t)v->sweep;
                break;
            default: s = 0;
            }
            v->phase += v->inc;
            mix += (int32_t)((int64_t)s * v->amp >> 16);
        }
        if (m->level < m->level_goal) m->level++;
        if (m->level > m->level_goal) m->level--;
        mix = (int32_t)((int64_t)mix * m->level >> 16);
        m->hp += (mix - m->hp) * 3 / 8; /* a gentle low-pass: warmer, less bright */
        mix = m->hp;
        if (mix > 20000) mix = 20000 + (mix - 20000) / 4; /* soft knee instead of a hard clip */
        if (mix < -20000) mix = -20000 + (mix + 20000) / 4;
        out[k] = (int16_t)(mix > 32767 ? 32767 : mix < -32767 ? -32767 : mix);
    }
}
