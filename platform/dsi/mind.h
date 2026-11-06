#ifndef OKUPA_DSI_MIND_H
#define OKUPA_DSI_MIND_H

#include "okupa_mind.h"
#include "okupa_pet.h"

/* The okupa's mind on 0G Compute from the console. A background thread
 * keeps it alive: every few seconds it tells 0G what the okupa notices and
 * gets back what it does, checked against the TEE's signature with the
 * shared core (game/src/think.c) over one kept-open TLS 1.2 connection.
 * Settings on the SD card, in dir:
 *   mind.txt  host=, provider=, model=, signer= (TEE signer), optional ip=
 *             (if DNS fails) and tz= (minutes the RTC is ahead of UTC)
 *             ear_host=, ear_provider=, ear_model=, ear_signer= (and ear_ip=)
 *             for speech to text; city= where the console is; look_every=
 *             seconds between looks through the camera (0: only with R)
 *   mind.key  hex key that owns the 0G ledger paying the provider
 *   memory.txt  facts its mind chose to keep, one a line (it writes this)
 * Each episode is written to dir/episodes/ as a hash chain (chain.txt holds
 * the last link) for the host to put on 0G Storage. Without settings, Wi-Fi
 * or a verified answer the okupa sleeps: nothing is made up. */
void ok_dsi_mind_start(const char *dir);
/* What the okupa notices, from the game loop (cheap: call every frame).
 * did: something the player just did, or NULL; it is told to the mind once. */
void ok_dsi_mind_observe(const ok_pet *p, int outside, uint32_t seconds_since_player, const char *did);
/* Y on the Dev screen: pause or resume the mind. */
void ok_dsi_mind_toggle(void);
/* L released: the clip goes to 0G (Whisper) to become words; the clip must
 * stay put while ok_dsi_mind_ear_busy(). R: look through the camera. */
void ok_dsi_mind_hear(const uint8_t *wav, size_t len);
int ok_dsi_mind_ear_busy(void);
int ok_dsi_mind_ear_state(void); /* 0 idle, 2 the clip is with 0G */
void ok_dsi_mind_look(void);
/* Pictionary: 0G's vision model guesses the 64x64 drawing. take_guess: 1
 * with the verified guess, -1 if it could not, 0 not yet. */
void ok_dsi_mind_guess(const uint8_t pixels[512]);
int ok_dsi_mind_take_guess(char *out, size_t cap);
/* 1 once per new verified decision (with its line, thought, question). */
int ok_dsi_mind_take(ok_mind *d);
/* Lines for the Dev screen. */
const char (*ok_dsi_mind_lines(void))[33];

#endif
