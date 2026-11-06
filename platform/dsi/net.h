#ifndef OKUPA_DSI_NET_H
#define OKUPA_DSI_NET_H

/* The network test on the console: Wi-Fi from the console's saved settings,
 * the Sepolia node list from DNS, then devp2p sessions until a node has room.
 * Everything Ethereum is the shared core; this file owns sockets and Wi-Fi.
 * lines: the Dev screen's text, updated as it goes; show() redraws it. */
void ok_dsi_network_test(char lines[][33], int count, void (*show)(void));

#endif
