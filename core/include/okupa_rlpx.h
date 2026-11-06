#ifndef OKUPA_RLPX_H
#define OKUPA_RLPX_H

#include "okupa.h"
#include "okupa_aes.h"
#include "okupa_keccak.h"

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

/* Session secrets once auth and ack are exchanged (nonce = recipient's):
 *   shared = keccak(ephemeral ECDH || keccak(recipient nonce || initiator nonce))
 *   aes    = keccak(ephemeral ECDH || shared)
 *   mac    = keccak(ephemeral ECDH || aes) */
ok_result ok_rlpx_derive(const uint8_t eph_priv[32], const uint8_t remote_eph[64], const uint8_t init_nonce[32],
                         const uint8_t resp_nonce[32], uint8_t aes_secret[32], uint8_t mac_secret[32]);

/* Frame state: AES-256-CTR (zero IV) each way, AES-256 for the MAC seeds and
 * a running keccak per direction, started from (mac ^ nonce) || packet. */
typedef struct {
    ok_aes_ctr enc, dec;
    ok_aes mac;
    ok_keccak egress, ingress;
} ok_rlpx_session;

/* auth and ack are the packets exactly as sent on the wire (still encrypted):
 * keep a copy, since the readers decrypt in place. */
ok_result ok_rlpx_session_init(ok_rlpx_session *s, int initiator, const uint8_t eph_priv[32],
                               const uint8_t own_nonce[32], const ok_rlpx_remote *remote,
                               const uint8_t *auth, size_t auth_len, const uint8_t *ack, size_t ack_len);

/* Frames: header (16, encrypted) || header MAC (16) || data padded to 16
 * (encrypted) || frame MAC (16). Header = 3-byte size || RLP [0, 0] || zeros.
 * MACs are checked before anything is decrypted. Any error leaves the session
 * out of step: drop the connection. */
#define OK_RLPX_HEADER 32
#define OK_RLPX_MAX_FRAME 0xffffff

/* Bytes a frame of n data bytes takes on the wire. */
size_t ok_rlpx_frame_len(size_t n);
/* data may overlap out. */
ok_result ok_rlpx_frame_write(ok_rlpx_session *s, const uint8_t *data, size_t n, uint8_t *out, size_t cap, size_t *len);
/* Checks and decrypts the first 32 bytes of a frame in place; *size is the data length. */
ok_result ok_rlpx_header_read(ok_rlpx_session *s, uint8_t header[OK_RLPX_HEADER], size_t *size);
/* The rest of the frame, ok_rlpx_frame_len(size) - 32 bytes, decrypted in place:
 * the data is then in body[0..size). */
ok_result ok_rlpx_body_read(ok_rlpx_session *s, uint8_t *body, size_t size);

#endif
