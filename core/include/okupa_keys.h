#ifndef OKUPA_KEYS_H
#define OKUPA_KEYS_H

#include "okupa.h"

/* Account keys over libsecp256k1, with its context in a fixed buffer (no heap). */
struct secp256k1_context_struct;
/* The core's libsecp256k1 context, or NULL if it could not be built. */
const struct secp256k1_context_struct *ok_secp(void);

/* 64-byte uncompressed public key (x || y), without the 0x04 prefix. */
ok_result ok_keys_public(const uint8_t priv[32], uint8_t pub[64]);
/* Ethereum address: the last 20 bytes of keccak256(pub). */
void ok_keys_address(const uint8_t pub[64], uint8_t addr[20]);
/* EIP-55 mixed-case hex, "0x" + 40 characters + NUL. */
void ok_keys_checksum(const uint8_t addr[20], char out[43]);
/* "0x" + 40 hex digits. All lower or all upper case is taken as is; mixed
 * case must be the EIP-55 checksum (a typo is caught). OK_FORMAT otherwise. */
ok_result ok_keys_parse_address(const char *text, uint8_t addr[20]);

#endif
