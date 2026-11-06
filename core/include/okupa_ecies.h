#ifndef OKUPA_ECIES_H
#define OKUPA_ECIES_H

#include "okupa.h"

/* ECIES as RLPx uses it: 0x04 || R (64) || iv (16) || AES-128-CTR(m) || HMAC-SHA256 (32).
 * Keys come from the concat KDF (SHA-256) over the raw ECDH x; the MAC key is
 * sha256(kM) and also covers auth (authenticated, not sent: the EIP-8 size prefix).
 * The core has no RNG: the caller supplies the ephemeral key and the IV. */
#define OK_ECIES_OVERHEAD (65 + 16 + 32)

ok_result ok_ecies_encrypt(const uint8_t pub[64], const uint8_t eph_priv[32], const uint8_t iv[16],
                           const uint8_t *msg, size_t n, const uint8_t *auth, size_t auth_len,
                           uint8_t *out, size_t cap, size_t *len);
/* Checks the MAC before decrypting; OK_CRYPTO if it does not match. */
ok_result ok_ecies_decrypt(const uint8_t priv[32], const uint8_t *in, size_t n,
                           const uint8_t *auth, size_t auth_len, uint8_t *out, size_t cap, size_t *len);

#endif
