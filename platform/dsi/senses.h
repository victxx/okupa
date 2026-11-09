#ifndef OKUPA_DSI_SENSES_H
#define OKUPA_DSI_SENSES_H

#include <stddef.h>
#include <stdint.h>

/* The okupa's ears and eyes on the DSi. What they pick up only becomes
 * something the okupa "heard" or "saw" once 0G has turned it into words and
 * the TEE's signature checks out (platform/dsi/mind.c). */

/* The microphone while L is held: up to 5 s, 16 kHz, 8-bit mono. */
#define OK_EAR_RATE 16000
#define OK_EAR_SECONDS 5
void ok_dsi_ear_start(void);
/* Stops and returns the clip as a WAV (valid until the next start), or NULL
 * if it was too short to be speech (level -1) or too quiet (its level, the
 * mean distance from silence of the 8-bit samples, under OK_EAR_QUIET). */
#define OK_EAR_QUIET 2
const uint8_t *ok_dsi_ear_stop(size_t *len, int *level);
int ok_dsi_ear_recording(void);

/* One photo from the camera facing the player, as a 128x96 PNG. The first
 * after the camera is selected waits a few frames for the exposure to
 * settle (yielding to the other threads). 0 and a reason if the camera did
 * not deliver. The camera stays selected until ok_dsi_eye_close. */
int ok_dsi_eye_snap(uint8_t *png, size_t cap, size_t *len, char why[33]);
void ok_dsi_eye_close(void);
/* The last photo halved to 128x96 DS colours (bit 15 set): what was sent. */
const uint16_t *ok_dsi_eye_thumb(void);
/* The viewfinder, driven by the main loop without ever waiting: start with
 * the front (0) or back (1) camera, step once a frame (1 when ok_dsi_eye_thumb
 * holds a new picture), stop. stop(1) keeps the frame on screen for the next
 * ok_dsi_eye_snap, so the photo sent is the one the player chose. While the
 * viewfinder is open the mind's snaps and closes wait. */
int ok_dsi_eye_preview_start(int outer, char why[33]);
int ok_dsi_eye_preview_step(void);
void ok_dsi_eye_preview_stop(int keep);
int ok_dsi_eye_outer(void);

#endif
