#ifndef OKUPA_TX_H
#define OKUPA_TX_H

#include "okupa.h"

/* EIP-1559 (type 2) transaction with an empty access list. Amounts in wei;
 * 64 bits is plenty for a game that only ever sends zero-value acts. */
typedef struct {
    uint64_t chain_id, nonce;
    uint64_t max_priority_fee, max_fee, gas_limit;
    uint8_t to[20];
    uint64_t value;
    const uint8_t *data;
    size_t data_len;
} ok_tx;

/* 0x02 || rlp([chain_id, nonce, tip, max_fee, gas, to, value, data, []]). */
ok_result ok_tx_unsigned(const ok_tx *tx, uint8_t *out, size_t cap, size_t *len);
/* keccak256 of the unsigned bytes: what the key signs. */
ok_result ok_tx_signing_hash(const ok_tx *tx, uint8_t *scratch, size_t cap, uint8_t hash[32]);

/* Signs with RFC 6979 nonces (deterministic) and writes
 * 0x02 || rlp([...nine fields, y_parity, r, s]); hash = keccak256 of those bytes.
 * The private key is only read; the caller owns and wipes it. */
ok_result ok_tx_sign(const ok_tx *tx, const uint8_t priv[32], uint8_t *out, size_t cap,
                     size_t *len, uint8_t hash[32]);

#endif
