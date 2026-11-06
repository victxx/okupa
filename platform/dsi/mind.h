#ifndef OKUPA_DSI_MIND_H
#define OKUPA_DSI_MIND_H

#include "okupa_pet.h"
#include "okupa_ui.h"

/* The okupa's mind on 0G Compute from the console: one decision over Wi-Fi
 * with the shared core (game/src/think.c), in its own background thread so
 * the game keeps drawing. Settings on the SD card, in dir:
 *   mind.txt  host=, provider=, model=, signer= (TEE signer), optional ip=
 *             (if DNS fails) and tz= (minutes the RTC is ahead of UTC)
 *   mind.key  hex key that owns the 0G ledger paying the provider
 * Each episode is written to dir/episodes/<chat id or time>.json for the host
 * to put on 0G Storage. Without settings, Wi-Fi or a verified answer the
 * okupa naps: nothing is made up. */
void ok_dsi_mind_start(const char *dir, const ok_pet *p);
int ok_dsi_mind_busy(void);
/* Lines for the Dev screen. */
const char (*ok_dsi_mind_lines(void))[33];
/* The verified line once a decision has arrived (NULL otherwise); each
 * decision is handed out once. */
const char *ok_dsi_mind_take_line(void);

#endif
