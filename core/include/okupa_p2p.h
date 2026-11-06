#ifndef OKUPA_P2P_H
#define OKUPA_P2P_H

#include "okupa.h"

/* The "p2p" capability that opens every RLPx session. A frame's data is
 * msg-id (RLP integer) || msg-data; msg-data is Snappy-compressed for every
 * message after the two Hellos (p2p version 5). Okupa speaks eth/69 only. */
enum { OK_P2P_HELLO = 0x00, OK_P2P_DISCONNECT = 0x01, OK_P2P_PING = 0x02, OK_P2P_PONG = 0x03 };
#define OK_P2P_VERSION 5
#define OK_P2P_ETH_VERSION 69
#define OK_P2P_ETH_OFFSET 0x10 /* eth message ids start here: we share no other capability */
#define OK_P2P_MAX_MESSAGE (16u << 20) /* larger uncompressed messages are refused */

typedef struct {
    uint64_t version;
    const uint8_t *client; /* points into the message, not NUL-terminated */
    size_t client_len;
    uint64_t eth;          /* OK_P2P_ETH_VERSION if offered, else 0 */
    uint8_t id[64];
} ok_p2p_hello;

/* Our Hello: [5, client, [["eth", 69]], 0, node pub], uncompressed. */
ok_result ok_p2p_hello_write(const uint8_t node_pub[64], const char *client, uint8_t *out, size_t cap, size_t *len);
/* msg-data of a Hello; extra list elements and unknown capabilities are ignored. */
ok_result ok_p2p_hello_read(const uint8_t *data, size_t n, ok_p2p_hello *hello);

/* Any message: id, then payload (an RLP item), compressed if snappy is set.
 * With snappy, payload must not overlap out. */
ok_result ok_p2p_write(uint64_t id, const uint8_t *payload, size_t n, int snappy, uint8_t *out, size_t cap, size_t *len);
/* Splits a frame's data into id and payload, decompressing into out if snappy is set,
 * otherwise copying. */
ok_result ok_p2p_read(const uint8_t *data, size_t n, int snappy, uint64_t *id, uint8_t *out, size_t cap, size_t *len);

ok_result ok_p2p_ping(int snappy, uint8_t *out, size_t cap, size_t *len);
ok_result ok_p2p_pong(int snappy, uint8_t *out, size_t cap, size_t *len);
ok_result ok_p2p_disconnect(uint64_t reason, int snappy, uint8_t *out, size_t cap, size_t *len);
/* [reason], a bare reason, or nothing at all (reported as 0, "requested"). */
ok_result ok_p2p_disconnect_reason(const uint8_t *payload, size_t n, uint64_t *reason);
const char *ok_p2p_reason_name(uint64_t reason);

#endif
