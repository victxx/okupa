#ifndef OKUPA_SESSION_H
#define OKUPA_SESSION_H

#include "okupa.h"
#include "okupa_eth.h"
#include "okupa_rlpx.h"

/* A whole devp2p session over a connected stream: RLPx handshake, Hello,
 * eth/69 Status, one Ping/Pong, then a clean Disconnect. Blocking, through
 * the platform's read/write functions; no sockets or entropy in here. Sends
 * no transactions. Uses static buffers: one session at a time. */
typedef struct {
    int (*read)(void *ctx, uint8_t *buf, size_t n);        /* >0 bytes read, <=0 closed or failed */
    int (*write)(void *ctx, const uint8_t *buf, size_t n); /* >0 bytes written */
    void (*log)(void *ctx, const char *line);              /* optional */
    void *ctx;
} ok_io;

enum { OK_SESSION_PASS, OK_SESSION_CLOSED, OK_SESSION_DISCONNECTED, OK_SESSION_MISMATCH, OK_SESSION_BROKEN };

typedef struct {
    int outcome;
    int handshake, hello, status, pong; /* how far it got */
    char client[48];
    uint64_t disconnect; /* reason, when outcome is OK_SESSION_DISCONNECTED */
    ok_eth_status remote;
} ok_session_report;

/* We dial a node we know by its public key. */
int ok_session_dial(const ok_io *io, const uint8_t priv[32], const uint8_t remote_pub[64], const ok_rlpx_entropy *e,
                    const ok_eth_network *net, uint64_t now, const char *client, ok_session_report *rep);
/* We answer a dial (for tests and the host lab). */
int ok_session_answer(const ok_io *io, const uint8_t priv[32], const ok_rlpx_entropy *e,
                      const ok_eth_network *net, uint64_t now, const char *client, ok_session_report *rep);
const char *ok_session_outcome_name(int outcome);

#endif
