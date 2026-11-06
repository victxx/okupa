#ifndef OKUPA_ETH_H
#define OKUPA_ETH_H

#include "okupa.h"

/* The eth capability, version 69 (message ids are offsets from OK_P2P_ETH_OFFSET). */
enum { OK_ETH_STATUS = 0x00, OK_ETH_TRANSACTIONS = 0x02 };

/* A network as Okupa needs it: id, genesis and its forks (EIP-2124). Block
 * forks come first, then time forks, each ascending; zeros are skipped. */
typedef struct {
    uint64_t network_id;
    uint8_t genesis[32];
    const uint64_t *blocks;
    size_t n_blocks;
    const uint64_t *times;
    size_t n_times;
} ok_eth_network;

extern const ok_eth_network ok_sepolia;
extern const ok_eth_network ok_mainnet;

uint32_t ok_crc32(uint32_t crc, const void *data, size_t n);

/* EIP-2124 fork id at a head (block number and timestamp). */
void ok_forkid(const ok_eth_network *net, uint64_t head_block, uint64_t head_time, uint8_t hash[4], uint64_t *next);

typedef struct {
    uint64_t version, network_id, fork_next, earliest, latest;
    uint8_t genesis[32], fork_hash[4], latest_hash[32];
} ok_eth_status;

/* Our Status: we keep no blocks, so the range is genesis to genesis; the fork id is
 * the one in force at now (every block fork is long past). This is the payload;
 * send it with ok_p2p_write(OK_P2P_ETH_OFFSET + OK_ETH_STATUS, ...). */
ok_result ok_eth_status_write(const ok_eth_network *net, uint64_t now, uint8_t *out, size_t cap, size_t *len);
ok_result ok_eth_status_read(const uint8_t *payload, size_t n, ok_eth_status *st);
/* NULL if the peer is on our network and version, else what differs. A fork
 * hash is accepted if it matches any point of our fork history. */
const char *ok_eth_status_mismatch(const ok_eth_network *net, const ok_eth_status *st);

/* Transactions payload: a list of transactions as sent on the wire (a typed
 * transaction is an RLP byte string holding type || payload). */
ok_result ok_eth_transactions(const uint8_t *const *txs, const size_t *lens, size_t count,
                              uint8_t *out, size_t cap, size_t *len);

#endif
