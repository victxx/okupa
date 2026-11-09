#ifndef OKUPA_BLOCK_H
#define OKUPA_BLOCK_H

#include "okupa.h"
#include "okupa_rlp.h"

/* Just enough of a block to see a transaction inside it, checked by hashing:
 * the header's hash is recomputed from its bytes and the transactions root
 * in the header is recomputed from the body. That proves "this header
 * commits to our transaction"; it does not prove the header is canonical or
 * final (that needs consensus, which the console does not follow). */

/* eth/69 messages (offsets from OK_P2P_ETH_OFFSET), each wrapped with a request id. */
enum { OK_ETH_GET_BLOCK_HEADERS = 0x03, OK_ETH_BLOCK_HEADERS = 0x04, OK_ETH_GET_BLOCK_BODIES = 0x05,
       OK_ETH_BLOCK_BODIES = 0x06 };

#define OK_BLOCK_MAX_TXS 4096

typedef struct {
    uint8_t hash[32];    /* keccak of the header's RLP, computed here */
    uint8_t parent[32];
    uint8_t tx_root[32];
    uint64_t number, time;
} ok_header;

/* Reads a header (an RLP list) and hashes it. */
ok_result ok_header_read(const uint8_t *rlp, size_t n, ok_header *h);

/* Root of the ordered trie Ethereum uses for a block's transactions: keys
 * are rlp(index), values are each transaction as it appears in the block
 * (type || payload, or the RLP list of a legacy one). */
ok_result ok_trie_ordered_root(const uint8_t *const *vals, const size_t *lens, size_t n, uint8_t root[32]);

typedef struct {
    uint32_t count;     /* transactions in the body */
    int32_t found;      /* index of a wanted transaction, -1 if none is there */
    int32_t which;      /* which of the wanted hashes it was, -1 if none */
    uint8_t root[32];   /* transactions root recomputed from the body */
    int root_ok;        /* equals the header's */
} ok_body_check;

/* body: one item of a BlockBodies list ([transactions, ommers, withdrawals]).
 * want: wants hashes (the versions of one transaction: a fee replaced keeps
 * its nonce, so any of them may be the one a block holds). */
ok_result ok_body_check_tx(const ok_rlp_item *body, const ok_header *h, const uint8_t (*want)[32], size_t wants,
                           ok_body_check *out);

/* Requests (payloads; send with OK_P2P_ETH_OFFSET + the id). */
ok_result ok_eth_get_headers(uint64_t request, uint64_t origin, uint64_t amount, uint8_t *out, size_t cap, size_t *len);
ok_result ok_eth_get_bodies(uint64_t request, const uint8_t (*hashes)[32], size_t count, uint8_t *out, size_t cap,
                            size_t *len);
/* A response: its request id and the list it carries (headers or bodies). */
ok_result ok_eth_response(const uint8_t *payload, size_t n, uint64_t *request, ok_rlp_item *list);

#endif
