#ifndef OKUPA_SESSION_H
#define OKUPA_SESSION_H

#include "okupa.h"
#include "okupa_block.h"
#include "okupa_eth.h"
#include "okupa_rlpx.h"

/* A whole devp2p session over a connected stream: RLPx handshake, Hello,
 * eth/69 Status, one Ping/Pong, then a clean Disconnect. Blocking, through
 * the platform's read/write functions; no sockets or entropy in here. Sends
 * no transactions. Uses static buffers: one session at a time. */
typedef struct {
    /* >0 bytes read; OK_IO_IDLE if nothing came within the platform's wait;
     * anything else: closed or failed. */
    int (*read)(void *ctx, uint8_t *buf, size_t n);
    int (*write)(void *ctx, const uint8_t *buf, size_t n); /* >0 bytes written */
    void (*log)(void *ctx, const char *line);              /* optional */
    /* Optional: once admitted, stay connected (answering pings) while this
     * returns 1, so the next message goes out with no new handshake. */
    int (*hold)(void *ctx);
    /* Optional: signed transactions (wire form) to hand over the moment the
     * peer's Status arrives, before a busy peer hangs up on us. */
    const uint8_t *const *txs;
    const size_t *tx_lens;
    size_t tx_count;
    /* Optional: once admitted, follow the chain from block watch_from (0 =
     * the peer's head) and look for the transaction whose hash is watch in
     * each new block: headers, then bodies, each checked by hashing. seconds
     * paces the requests; block hears about every block checked. */
    const uint8_t *watch;
    uint64_t watch_from;
    uint32_t (*seconds)(void *ctx);
    void (*block)(void *ctx, const ok_header *h, const ok_body_check *c);
    /* Optional: each step reached (OK_STAGE_*), for timings and the screens. */
    void (*stage)(void *ctx, int stage);
    void *ctx;
} ok_io;

/* How far a dial got, in order. */
enum { OK_STAGE_NONE, OK_STAGE_TCP, OK_STAGE_AUTH, OK_STAGE_ACK, OK_STAGE_HELLO, OK_STAGE_STATUS, OK_STAGE_TX,
       OK_STAGE_HELD };

#define OK_IO_IDLE (-2)

enum { OK_SESSION_PASS, OK_SESSION_CLOSED, OK_SESSION_DISCONNECTED, OK_SESSION_MISMATCH, OK_SESSION_BROKEN };

typedef struct {
    int outcome;
    int handshake, hello, status, pong; /* how far it got */
    unsigned held;                      /* messages handled while holding */
    int tx_written;                     /* the Transactions message went out */
    uint64_t blocks;                    /* blocks checked for the watched transaction */
    int found;                          /* it is in one whose transactions root matched */
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
