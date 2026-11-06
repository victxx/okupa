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

const uint8_t *ok_dsi_ear_stop(size_t *len)
{
    uint32_t n;
    if (!recording) return NULL;
    soundMicOff();
    recording = 0;
    n = samples;
    if (n < OK_EAR_RATE / 2) return NULL; /* under half a second: not speech */
    ok_wav_unsigned8(clip + OK_WAV_HEADER, n);
    ok_wav_header(clip, OK_EAR_RATE, n);
    *len = OK_WAV_HEADER + n;
    return clip;
}

/* ---- Eyes ----------------------------------------------------------------
 * The DSi cameras give 256x192 15-bit frames by NDMA; a few are thrown away
 * while the exposure settles, the next one is halved into a PNG. */
#define CAMERA_NDMA 1
#define WARMUP_FRAMES 12

TWL_BSS static uint16_t frame[256 * 192] __attribute__((aligned(32)));

static int grab(void)
{
    int frames;
    if (!cameraStartTransfer(frame, MCUREG_APT_SEQ_CMD_PREVIEW, CAMERA_NDMA)) return 0;
    for (frames = 0; frames < 30 && (ndmaBusy(CAMERA_NDMA) || cameraTransferActive()); frames++)
        cothread_yield_irq(IRQ_VBLANK);
    cameraStopTransfer();
    return frames < 30;
}

int ok_dsi_eye_snap(uint8_t *png, size_t cap, size_t *len, char why[33])
{
    static int ready;
    int i, ok = 1;
    if (!isDSiMode()) {
        snprintf(why, 33, "camera: DSi mode only");
        return 0;
    }
    if (!ready && !(ready = cameraInit())) {
        snprintf(why, 33, "camera: init failed");
        return 0;
    }
    if (!cameraSelect(CAMERA_INNER)) {
        snprintf(why, 33, "camera: select failed");
        return 0;
    }
    for (i = 0; i < WARMUP_FRAMES && ok; i++) ok = grab();
    if (ok) ok = grab();
    cameraSelect(CAMERA_NONE); /* the camera light goes off between looks */
    if (!ok) {
        snprintf(why, 33, "camera: no frame");
        return 0;
    }
    DC_InvalidateRange(frame, sizeof frame);
    if (ok_png_rgb15(frame, 256, 192, 2, png, cap, len) != OK_OK) {
        snprintf(why, 33, "camera: PNG too big");
        return 0;
    }
    why[0] = 0;
    return 1;
}
