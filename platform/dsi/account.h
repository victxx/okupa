#ifndef OKUPA_DSI_ACCOUNT_H
#define OKUPA_DSI_ACCOUNT_H

#include "okupa_ui.h"
#include <stddef.h>
#include <stdint.h>

/* The console's own Ethereum account. The key is made on the console from
 * the player's scribbles (the DSi has no random generator) and kept in a file
 * on the SD card; it never leaves the console otherwise. */
void ok_dsi_account_open(const char *path, ok_ui *u);
/* One stylus sample while scribbling; creates the key once there is enough. */
void ok_dsi_account_scribble(int x, int y, ok_ui *u);

/* Signs a drawing on the console: a zero-value transaction to its own
 * address carrying the 64x64 drawing (512 bytes, 1 bpp) as an OKPA act. Fees
 * are capped (2 gwei max, 0.1 gwei tip); only the base fee is paid. 1 on
 * success. */
int ok_dsi_account_act(uint64_t chain_id, uint64_t nonce, const uint8_t *pixels, uint8_t *out, size_t cap, size_t *len,
                       uint8_t hash[32]);

#endif
