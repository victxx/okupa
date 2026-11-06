#ifndef OKUPA_RLPX_H
#define OKUPA_RLPX_H

#include "okupa.h"

/* RLPx handshake packets in the EIP-8 format: a 2-byte big-endian size, then
 * ECIES(body || padding) with the size as authenticated data.
 *   auth = [sig, initiator static pub, initiator nonce, version]
 *   ack  = [recipient ephemeral pub, recipient nonce, version]
 * sig signs (static ECDH secret XOR nonce) with the ephemeral key, so the
 * recipient recovers the initiator's ephemeral pub from it.
 * Readers accept higher versions and extra list elements, as EIP-8 asks. */

/* Fresh secrets for one handshake packet; the core has no RNG of its own. */
typedef struct {
    uint8_t eph[32];   /* handshake ephemeral key */
    uint8_t nonce[32];
    uint8_t ecies[32]; /* ECIES ephemeral key for this packet */
    uint8_t iv[16];
} ok_rlpx_entropy;

/* What a packet told us about the other side. pub is only set by auth. */
typedef struct {
    uint8_t pub[64];
    uint8_t eph[64];
    uint8_t nonce[32];
    uint64_t version;
} ok_rlpx_remote;

#define OK_RLPX_PADDING 100
#define OK_RLPX_MAX_PACKET 2048

/* Total packet length announced by its first two bytes. */
size_t ok_rlpx_packet_len(const uint8_t prefix[2]);

ok_result ok_rlpx_auth_write(const uint8_t priv[32], const uint8_t remote_pub[64], const ok_rlpx_entropy *e,
                             uint8_t *out, size_t cap, size_t *len);
ok_result ok_rlpx_ack_write(const uint8_t remote_pub[64], const ok_rlpx_entropy *e,
                            uint8_t *out, size_t cap, size_t *len);
/* The whole packet, prefix included; it is decrypted in place. */
ok_result ok_rlpx_auth_read(const uint8_t priv[32], uint8_t *packet, size_t n, ok_rlpx_remote *remote);
ok_result ok_rlpx_ack_read(const uint8_t priv[32], uint8_t *packet, size_t n, ok_rlpx_remote *remote);

#endif
