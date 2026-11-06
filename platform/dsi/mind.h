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
 *   mind.key  hex key that owns the 0G ledger paying the provider
 * Each episode is written to dir/episodes/ as a hash chain (chain.txt holds
 * the last link) for the host to put on 0G Storage. Without settings, Wi-Fi
 * or a verified answer the okupa sleeps: nothing is made up. */
void ok_dsi_mind_start(const char *dir);
/* What the okupa notices, from the game loop (cheap: call every frame).
 * did: something the player just did, or NULL; it is told to the mind once. */
void ok_dsi_mind_observe(const ok_pet *p, int outside, uint32_t seconds_since_player, const char *did);
/* Y on the Dev screen: pause or resume the mind. */
void ok_dsi_mind_toggle(void);
/* 1 once per new verified decision. */
int ok_dsi_mind_take(ok_mind *d);
/* Lines for the Dev screen. */
const char (*ok_dsi_mind_lines(void))[33];

/* What the mind thread is doing right now (for the freeze watchdog). */
extern volatile const char *ok_dsi_mind_where;

#endif
