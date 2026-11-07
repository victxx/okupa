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
 *             seconds between looks through the camera (default 0: only when asked)
 *   mind.key  hex key that owns the 0G ledger paying the provider
 *   memory.txt  facts its mind chose to keep, one a line (it writes this)
 * Each episode is written to dir/episodes/ as a hash chain (chain.txt holds
 * the last link) for the host to put on 0G Storage. Without settings, Wi-Fi
 * or a verified answer the okupa decides nothing new: nothing is made up. */
void ok_dsi_mind_start(const char *dir);
/* What the okupa notices, from the game loop (cheap: call every frame).
 * did: something the player just did, or NULL; it is told to the mind once. */
void ok_dsi_mind_observe(const ok_pet *p, int outside, uint32_t seconds_since_player, const char *did);
/* Y on the Dev screen: pause or resume the mind. */
void ok_dsi_mind_toggle(void);

/* What the player asks of the mind, served before anything it does on its
 * own. Each request ends with a verified answer or a reason, never a
 * made-up one; while the mind is not running (no settings, paused, no
 * Wi-Fi) it fails at once, with ok_dsi_mind_why_not() as the reason. */
enum { OK_DSI_HEAR, OK_DSI_LOOK, OK_DSI_GUESS, OK_DSI_COMMENT, OK_DSI_DREAM, OK_DSI_ASKS };
/* Its stage, for the screen meanwhile: 0 nothing, then queued, preparing
 * (the camera, the picture), asking 0G, checking the TEE's signature. */
enum { OK_DSI_IDLE, OK_DSI_QUEUED, OK_DSI_PREPARING, OK_DSI_ASKING, OK_DSI_CHECKING };
int ok_dsi_mind_alive(void);
const char *ok_dsi_mind_why_not(void);
int ok_dsi_mind_stage(int kind);
/* Once per answer: 1 with the verified text (and the round trip in ms),
 * -1 with why it failed, 0 nothing new. text/why may be NULL. */
int ok_dsi_mind_result(int kind, char *text, size_t cap, char why[48], uint32_t *ms);
/* L released: the clip goes to 0G (Whisper) to become words; the clip must
 * stay put while ok_dsi_mind_ear_busy(). */
void ok_dsi_mind_hear(const uint8_t *wav, size_t len);
int ok_dsi_mind_ear_busy(void);
/* R or Photo: one photo from the camera to 0G's vision model. live: the
 * player is watching (the camera stays on between looks and the mind makes
 * no decisions meanwhile); look_photo: the photo behind the last verified
 * look, 128x96 DS colours. */
void ok_dsi_mind_look(void);
void ok_dsi_mind_live(int on);
void ok_dsi_mind_look_photo(uint16_t out[128 * 96]);
/* Pictionary: 0G's vision model guesses the 64x64 drawing. */
void ok_dsi_mind_guess(const uint8_t pixels[512]);
/* A drawing of the player's: 0G's vision model looks at it and the okupa
 * says one line about it (decoration only). */
void ok_dsi_mind_comment(const uint8_t pixels[512]);
/* What it makes, dreams and writes, once each: an object out of boxes, its
 * own 64x64 drawing, last night's dream picture (DS colours) and a letter to
 * send on Ethereum. ok_dsi_mind_letters: whether it may write one now (the
 * game loop keeps the cap: one a day, none while another is pending). */
#include "okupa_make.h"
int ok_dsi_mind_take_object(ok_object *o);
int ok_dsi_mind_take_drawing(uint8_t pixels[512], char name[21]);
const uint16_t *ok_dsi_mind_take_dream(int *w, int *h, const char **about);
int ok_dsi_mind_take_letter(char text[141]);
void ok_dsi_mind_letters(int allowed);
/* 1 once per new verified decision (with its line, thought, question). */
int ok_dsi_mind_take(ok_mind *d);
/* Lines for the Dev screen. */
const char (*ok_dsi_mind_lines(void))[33];

/* What the mind thread is doing right now (for the freeze watchdog). */
extern volatile const char *ok_dsi_mind_where;

#endif
