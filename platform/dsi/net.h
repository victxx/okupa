#ifndef OKUPA_DSI_NET_H
#define OKUPA_DSI_NET_H

#include "okupa_ui.h"

/* The network lives in a background thread, the "postman": it joins a
 * remembered Wi-Fi, finds Sepolia nodes (known peers, the DNS list, discovery
 * v4), keeps trying until one lets us in and then stays connected. The game
 * never waits for it. Ethereum itself is the shared core. */
void ok_dsi_net_start(void);
/* One short line for the home screen ("Online: ...", "Looking for a node"). */
const char *ok_dsi_net_summary(void);
/* Fourteen lines of detail for the Dev screen. */
const char (*ok_dsi_net_lines(void))[33];

/* The folder on the SD card for the network's files (before ok_dsi_net_start):
 * net-log.txt, wifi.txt (remembered networks), peers-<network>.txt (nodes
 * that let us in) and network.txt ("sepolia" to leave mainnet). */
void ok_dsi_net_files(const char *dir);
const char *ok_dsi_net_name(void);

/* Wi-Fi set-up from the Dev screen; the postman stands back meanwhile. */
int ok_dsi_wifi_scan(ok_ui_ap *aps, int max, char status[33], void (*show)(void));
/* 1 once associated with an address; remembers the network. */
int ok_dsi_wifi_connect(const char *ssid, const char *password, char status[33], void (*show)(void));

#endif
