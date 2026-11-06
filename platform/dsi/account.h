#ifndef OKUPA_DSI_ACCOUNT_H
#define OKUPA_DSI_ACCOUNT_H

#include "okupa_ui.h"

/* The console's own Ethereum account. The key is made on the console from
 * the player's scribbles (the DSi has no random generator) and kept in a file
 * on the SD card; it never leaves the console otherwise. */
void ok_dsi_account_open(const char *path, ok_ui *u);
/* One stylus sample while scribbling; creates the key once there is enough. */
void ok_dsi_account_scribble(int x, int y, ok_ui *u);

#endif
