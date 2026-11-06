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
 * if it was too short to be speech. */
const uint8_t *ok_dsi_ear_stop(size_t *len);
int ok_dsi_ear_recording(void);

/* One photo from the camera facing the player, as a 128x96 PNG. Waits a few
 * frames for the exposure to settle (yielding to the other threads). 0 and
 * a reason if the camera did not deliver. */
int ok_dsi_eye_snap(uint8_t *png, size_t cap, size_t *len, char why[33]);

#endif
