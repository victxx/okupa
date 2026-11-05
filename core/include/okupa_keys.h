#ifndef OKUPA_KEYS_H
#define OKUPA_KEYS_H

#include "okupa.h"

/* Account keys. Uses libsecp256k1's static context: no heap, no global state. */

/* 64-byte uncompressed public key (x || y), without the 0x04 prefix. */
ok_result ok_keys_public(const uint8_t priv[32], uint8_t pub[64]);
/* Ethereum address: the last 20 bytes of keccak256(pub). */
void ok_keys_address(const uint8_t pub[64], uint8_t addr[20]);
/* EIP-55 mixed-case hex, "0x" + 40 characters + NUL. */
void ok_keys_checksum(const uint8_t addr[20], char out[43]);

#endif
