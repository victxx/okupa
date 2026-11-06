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

/* Signs a hello on the console: a zero-value transaction to its own address
 * carrying the OKPA hello. Fees are capped generously (2 gwei max, 0.1 gwei
 * tip) so it lands without asking the chain; only the base fee is paid.
 * 1 on success. */
int ok_dsi_account_hello(uint64_t chain_id, uint64_t nonce, uint8_t *out, size_t cap, size_t *len, uint8_t hash[32]);

/* Same, carrying a 64x64 drawing (512 bytes, 1 bpp) when pixels is not NULL. */
int ok_dsi_account_act(uint64_t chain_id, uint64_t nonce, const uint8_t *pixels, uint8_t *out, size_t cap, size_t *len,
                       uint8_t hash[32]);

/* A letter from the okupa's mind (printable ASCII, up to 140 bytes), to the
 * address to (20 bytes; NULL = the console's own), signed
 * with capped fees: at most 40 000 gas x 2 gwei. */
int ok_dsi_account_letter(uint64_t chain_id, uint64_t nonce, const char *text, const uint8_t *to, uint8_t *out,
                          size_t cap, size_t *len, uint8_t hash[32]);

#endif
