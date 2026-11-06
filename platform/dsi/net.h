#ifndef OKUPA_DSI_NET_H
#define OKUPA_DSI_NET_H

#include "okupa_ui.h"

/* The network test on the console: Wi-Fi, the Sepolia node list from DNS,
 * then devp2p sessions until a node has room. Everything Ethereum is the
 * shared core; this file owns Wi-Fi and sockets.
 * lines: the Dev screen's text, updated as it goes; show() redraws it. */
void ok_dsi_network_test(char lines[][33], int count, void (*show)(void));

/* Wi-Fi chosen in the app. Without it, the console's own saved settings. */
void ok_dsi_wifi_use(const char *ssid, const char *password);
int ok_dsi_wifi_scan(ok_ui_ap *aps, int max, char status[33], void (*show)(void));
/* 1 once associated with an address; status says what happened. */
int ok_dsi_wifi_connect(const char *ssid, const char *password, char status[33], void (*show)(void));

#endif
