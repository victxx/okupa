#ifndef OKUPA_DSI_ACCOUNT_H
#define OKUPA_DSI_ACCOUNT_H

#include "okupa_ui.h"
#include <stddef.h>
#include <stdint.h>

/* The console's own Ethereum account. The key is made on the console from
 * the player's scribbles (the DSi has no random generator) and kept in a file
 * on the SD card; it never leaves the console otherwise. */
void ok_dsi_account_open(const char *path, ok_ui *u);
/* Real entropy for a new key, mixed with the strokes (never used alone). */
void ok_dsi_account_seed(const uint8_t s[32]);
/* One stylus sample while scribbling; creates the key once there is enough. */
void ok_dsi_account_scribble(int x, int y, ok_ui *u);

/* Signs a drawing on the console: a zero-value transaction to its own
 * address carrying the 64x64 drawing (512 bytes, 1 bpp) as an OKPA act. Fees
 * are capped: at level 0 a 0.1 gwei tip and at most 2 gwei a unit of gas (only
 * the base fee is paid); each level up (a speed-up: same nonce, new bytes)
 * doubles both, up to OK_DSI_FEE_LEVELS. 1 on success. */
#define OK_DSI_FEE_LEVELS 3
int ok_dsi_account_act(uint64_t chain_id, uint64_t nonce, const uint8_t *pixels, int level, uint8_t *out, size_t cap,
                       size_t *len, uint8_t hash[32]);
/* The same with a 64x64 photo in 4 tones (1024 bytes, okupa_photo.h). */
int ok_dsi_account_photo(uint64_t chain_id, uint64_t nonce, const uint8_t *tones, int level, uint8_t *out, size_t cap,
                         size_t *len, uint8_t hash[32]);

#endif
