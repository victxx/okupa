#include "senses.h"
#include "okupa_png.h"
#include "okupa_wav.h"
#include <nds.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---- Ears ----------------------------------------------------------------
 * libnds records into a ring buffer and hands over each half as it fills;
 * the halves are copied after a WAV header. */
#define HALF 1600 /* 0.1 s */
#define MAX_SAMPLES (OK_EAR_RATE * OK_EAR_SECONDS)

static uint8_t ring[2 * HALF] __attribute__((aligned(32)));
TWL_BSS static uint8_t clip_twl[OK_WAV_HEADER + MAX_SAMPLES]; /* in the DSi's own RAM */
static uint8_t *clip;
static uint32_t clip_max; /* samples: 5 s in DSi mode, 3 s from the heap in DS mode */
static volatile uint32_t samples;
static volatile int recording;

static void heard(void *completed, int length)
{
    uint32_t n = (uint32_t)length;
    if (!recording) return;
    if (samples + n > clip_max) n = clip_max - samples;
    DC_InvalidateRange(completed, (uint32_t)length);
    memcpy(clip + OK_WAV_HEADER + samples, completed, n);
    samples += n;
}

void ok_dsi_ear_start(void)
{
    if (recording) return;
    if (!clip) {
        if (isDSiMode()) {
            clip = clip_twl, clip_max = MAX_SAMPLES;
        } else {
            clip_max = OK_EAR_RATE * 3;
            if (!(clip = malloc(OK_WAV_HEADER + clip_max))) return;
        }
    }
    samples = 0;
    recording = 1;
    soundMicRecord(ring, sizeof ring, MicFormat_8Bit, OK_EAR_RATE, heard);
}

int ok_dsi_ear_recording(void) { return recording; }

const uint8_t *ok_dsi_ear_stop(size_t *len, int *level)
{
    uint32_t n;
    *level = -1;
    if (!recording) return NULL;
    soundMicOff();
    recording = 0;
    n = samples;
    if (n < OK_EAR_RATE / 2) return NULL; /* under half a second: not speech */
    ok_wav_unsigned8(clip + OK_WAV_HEADER, n);
    /* Near silence is not sent: speech to text makes up words ("Thank
     * you.") out of nothing. */
    if ((*level = ok_wav_level8(clip + OK_WAV_HEADER, n)) < OK_EAR_QUIET) return NULL;
    ok_wav_header(clip, OK_EAR_RATE, n);
    *len = OK_WAV_HEADER + n;
    return clip;
}

/* ---- Eyes ----------------------------------------------------------------
 * The DSi cameras give 256x192 15-bit frames by NDMA; a few are thrown away
 * while the exposure settles, the next one is halved into a PNG. The camera
 * stays selected (its light on) while the player keeps looking, and is let
 * go with ok_dsi_eye_close. */
#define CAMERA_NDMA 1
#define WARMUP_FRAMES 5
#define BUS_PER_MS 33514u

TWL_BSS static uint16_t frame[256 * 192] __attribute__((aligned(32)));
TWL_BSS static uint16_t thumb[128 * 96];
static int selected;

/* One frame: 1, or 0 if the NDMA never finished (within half a second of
 * real time, however slowly the game draws), -1 if the camera overran. The
 * camera's transfer bit is not reliably cleared by the hardware at the end
 * of a frame (libnds clears it itself before the next one), so only the
 * NDMA is waited on, then the transfer is stopped. */
static int grab(void)
{
    uint32_t start;
    int overrun;
    if (!cameraStartTransfer(frame, MCUREG_APT_SEQ_CMD_PREVIEW, CAMERA_NDMA)) return 0;
    start = cpuGetTiming();
    while (ndmaBusy(CAMERA_NDMA) && cpuGetTiming() - start < 500u * BUS_PER_MS) cothread_yield_irq(IRQ_VBLANK);
    overrun = (REG_CAM_CNT & CAM_CNT_TRANSFER_ERROR) != 0;
    if (ndmaBusy(CAMERA_NDMA)) {
        REG_NDMA_CR(CAMERA_NDMA) &= ~NDMA_ENABLE; /* never finished: let the channel go */
        cameraStopTransfer();
        return 0;
    }
    cameraStopTransfer();
    return overrun ? -1 : 1;
}

void ok_dsi_eye_close(void)
{
    if (!selected) return;
    cameraSelect(CAMERA_NONE); /* the camera light goes off */
    selected = 0;
}

const uint16_t *ok_dsi_eye_thumb(void) { return thumb; }

int ok_dsi_eye_snap(uint8_t *png, size_t cap, size_t *len, char why[33])
{
    static int ready;
    int i, got = 0, tries;
    if (!isDSiMode()) {
        snprintf(why, 33, "camera: DSi mode only");
        return 0;
    }
    if (!ready && !(ready = cameraInit())) {
        snprintf(why, 33, "camera: init failed");
        return 0;
    }
    if (!selected) {
        if (!cameraSelect(CAMERA_INNER)) {
            snprintf(why, 33, "camera: select failed");
            return 0;
        }
        selected = 1;
        for (i = 0; i < WARMUP_FRAMES; i++) /* the exposure settles */
            if (!grab()) break;
    }
    for (tries = 0; tries < 3 && got != 1; tries++) got = grab();
    if (got != 1) {
        ok_dsi_eye_close();
        snprintf(why, 33, got < 0 ? "camera: overrun" : "camera: no frame");
        return 0;
    }
    DC_InvalidateRange(frame, sizeof frame);
    { /* the picture the player will see beside 0G's words: the same frame, halved */
        int x, y;
        for (y = 0; y < 96; y++) {
            for (x = 0; x < 128; x++) {
                const uint16_t *a = frame + (2 * y) * 256 + 2 * x, *b = a + 256;
                uint32_t r = (a[0] & 31) + (a[1] & 31) + (b[0] & 31) + (b[1] & 31);
                uint32_t g = (a[0] >> 5 & 31) + (a[1] >> 5 & 31) + (b[0] >> 5 & 31) + (b[1] >> 5 & 31);
                uint32_t bl = (a[0] >> 10 & 31) + (a[1] >> 10 & 31) + (b[0] >> 10 & 31) + (b[1] >> 10 & 31);
                thumb[y * 128 + x] = (uint16_t)(0x8000u | (r / 4) | (g / 4) << 5 | (bl / 4) << 10);
            }
            if ((y & 15) == 15) cothread_yield();
        }
    }
    if (ok_png_rgb15(frame, 256, 192, 2, png, cap, len) != OK_OK) {
        snprintf(why, 33, "camera: PNG too big");
        return 0;
    }
    why[0] = 0;
    return 1;
}
