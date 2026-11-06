#ifndef OKUPA_DSI_NET_H
#define OKUPA_DSI_NET_H

#include "okupa_ui.h"
#include <stddef.h>
#include <stdint.h>

/* The network lives in a background thread, the "postman": it joins a
 * remembered Wi-Fi, finds Sepolia nodes (known peers, the DNS list, discovery
 * v4), keeps trying until one lets us in and then stays connected. The game
 * never waits for it. Ethereum itself is the shared core. */
void ok_dsi_net_start(void);
/* Seconds since the network started (the live feed's clock). */
uint32_t ok_dsi_uptime(void);
/* One short line for the home screen ("Online: ...", "Looking for a node"). */
const char *ok_dsi_net_summary(void);
/* Fourteen lines of detail for the Dev screen. */
const char (*ok_dsi_net_lines(void))[33];

/* The folder on the SD card for the network's files (before ok_dsi_net_start):
 * net-log.txt, wifi.txt (remembered networks), peers-<network>.txt (nodes
 * that let us in) and network.txt ("sepolia" to leave mainnet). */
void ok_dsi_net_files(const char *dir);
const char *ok_dsi_net_name(void);

/* A signed transaction to deliver: kept on the SD card and handed to every
 * node that reaches Status (even a full one), until replaced. */
void ok_dsi_net_send(const uint8_t *tx, size_t n, const uint8_t hash[32]);
int ok_dsi_net_pending(uint8_t hash[32]); /* 1 if one is waiting */
int ok_dsi_net_handed(void);               /* nodes it was handed to */
uint64_t ok_dsi_net_chain_id(void);
/* One-shot: the pending transaction was seen in a block (header hash and
 * transactions root recomputed on the console). Clears the pending one. */
int ok_dsi_net_take_included(uint8_t hash[32], uint64_t *block);
/* The account's next nonce, counted on the console (nonce-<network>.txt). */
uint64_t ok_dsi_net_take_nonce(void);

/* Wi-Fi set-up from the Dev screen; the postman stands back meanwhile. */
int ok_dsi_wifi_scan(ok_ui_ap *aps, int max, char status[33], void (*show)(void));
/* 1 once associated with an address; remembers the network. */
int ok_dsi_wifi_connect(const char *ssid, const char *password, char status[33], void (*show)(void));

#endif
