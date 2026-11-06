/* The sound channel plays a ring of samples forever; we keep writing the
 * part it is about to reach. Its rate (16756991 Hz / 1024) and the CPU timer
 * (33513982 Hz) come from the same crystal: 2048 timer ticks per sample,
 * exactly, so the two never drift apart. */
#include "audio.h"
#include "okupa_music.h"
#include <nds.h>
#include <string.h>

#define RATE 16384     /* what libnds turns into a timer of 1024 */
#define RING 8192      /* half a second */
#define AHEAD 3200     /* about 0.2 s written ahead of the speaker */
#define CHUNK 512

static int16_t ring[RING] __attribute__((aligned(32)));
static ok_music music;
static uint32_t last_ticks, rest, played, written;
static int on;

void ok_dsi_audio_start(void)
{
    soundEnable();
    ok_music_init(&music, RATE);
    memset(ring, 0, sizeof ring);
    ok_music_render(&music, ring, AHEAD);
    written = AHEAD;
    DC_FlushRange(ring, sizeof ring);
    soundPlaySample(ring, SoundFormat_16Bit, sizeof ring, RATE, 110, 64, true, 0);
    last_ticks = cpuGetTiming();
    rest = played = 0;
    on = 1;
}

void ok_dsi_audio_pump(void)
{
    uint32_t now, d;
    if (!on) return;
    now = cpuGetTiming();
    d = now - last_ticks + rest; /* the timer wraps every 128 s; the difference does not care */
    last_ticks = now;
    played += d / 2048;
    rest = d % 2048;
    if (written < played) written = played + 64; /* we fell behind (a long wait): skip ahead */
    while (written < played + AHEAD) {
        uint32_t at = written % RING, n = played + AHEAD - written;
        if (n > RING - at) n = RING - at;
        if (n > CHUNK) n = CHUNK;
        ok_music_render(&music, ring + at, (int)n);
        DC_FlushRange(ring + at, n * sizeof ring[0]);
        written += n;
    }
}

void ok_dsi_audio_tune(int tune)
{
    if (music.next_tune != tune) ok_music_tune(&music, tune);
}

void ok_dsi_audio_sfx(int sfx)
{
    ok_music_sfx(&music, sfx);
}
