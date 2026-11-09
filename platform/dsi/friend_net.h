#ifndef OKUPA_DSI_FRIEND_NET_H
#define OKUPA_DSI_FRIEND_NET_H

#include "okupa_friend.h"

/* The friend game's web server on the console (port OK_FRIEND_PORT), once
 * the Wi-Fi has an address: 0 if it could not listen. */
int ok_dsi_friend_open(void);
void ok_dsi_friend_close(void);
/* Once a frame: accepts, reads, answers, sends, without ever waiting. */
void ok_dsi_friend_step(ok_friend *f, uint32_t now);
/* "192.168.1.9:8080", what the friend types in their browser. */
void ok_dsi_friend_where(char out[24]);

#endif
