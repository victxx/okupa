#include "okupa_rlpx.h"
#include "okupa_ecdh.h"
#include "okupa_ecies.h"
#include "okupa_keys.h"
#include "okupa_rlp.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"
#include <string.h>

#define BODY (2 + 65 + 16) /* where the plaintext sits inside a packet */

size_t ok_rlpx_packet_len(const uint8_t prefix[2])
{
    return 2 + ((size_t)prefix[0] << 8 | prefix[1]);
}

/* static ECDH secret XOR nonce: what the auth signature covers. */
static ok_result signed_message(const uint8_t priv[32], const uint8_t pub[64], const uint8_t nonce[32], uint8_t msg[32])
{
    int i;
    ok_result r = ok_ecdh(priv, pub, msg);
    if (r == OK_OK)
        for (i = 0; i < 32; i++) msg[i] ^= nonce[i];
    return r;
}

/* Pads the body at out + BODY, then encrypts it with the size prefix. */
static ok_result seal(const uint8_t remote_pub[64], const ok_rlpx_entropy *e, ok_rlp_writer *w,
                      uint8_t *out, size_t cap, size_t *len)
{
    size_t body, size;
    ok_result r = ok_rlp_done(w, &body);
    if (r != OK_OK) return r;
    if (cap < BODY + body + OK_RLPX_PADDING + 32) return OK_RANGE;
    memset(out + BODY + body, 0, OK_RLPX_PADDING);
    body += OK_RLPX_PADDING;
    size = body + OK_ECIES_OVERHEAD;
    out[0] = (uint8_t)(size >> 8);
    out[1] = (uint8_t)size;
    r = ok_ecies_encrypt(remote_pub, e->ecies, e->iv, out + BODY, body, out, 2, out + 2, cap - 2, len);
    if (r == OK_OK) *len += 2;
    return r;
}

ok_result ok_rlpx_auth_write(const uint8_t priv[32], const uint8_t remote_pub[64], const ok_rlpx_entropy *e,
                             uint8_t *out, size_t cap, size_t *len)
{
    secp256k1_ecdsa_recoverable_signature sig;
    const secp256k1_context *ctx = ok_secp();
    uint8_t msg[32], rsv[65], pub[64];
    ok_rlp_writer w;
    size_t mark;
    int recid;
    ok_result r;
    if (!priv || !remote_pub || !e || !out || !len) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    if (cap < BODY) return OK_RANGE;
    if ((r = ok_keys_public(priv, pub)) != OK_OK) return r;
    if ((r = signed_message(priv, remote_pub, e->nonce, msg)) != OK_OK) return r;
    r = secp256k1_ecdsa_sign_recoverable(ctx, &sig, msg, e->eph, NULL, NULL) ? OK_OK : OK_CRYPTO;
    ok_wipe(msg, sizeof msg);
    if (r != OK_OK) return r;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, rsv, &recid, &sig);
    rsv[64] = (uint8_t)recid;

    ok_rlp_init(&w, out + BODY, cap - BODY);
    mark = ok_rlp_list_begin(&w);
    ok_rlp_bytes(&w, rsv, 65);
    ok_rlp_bytes(&w, pub, 64);
    ok_rlp_bytes(&w, e->nonce, 32);
    ok_rlp_uint(&w, 4);
    ok_rlp_list_end(&w, mark);
    return seal(remote_pub, e, &w, out, cap, len);
}

ok_result ok_rlpx_ack_write(const uint8_t remote_pub[64], const ok_rlpx_entropy *e,
                            uint8_t *out, size_t cap, size_t *len)
{
    uint8_t eph[64];
    ok_rlp_writer w;
    size_t mark;
    ok_result r;
    if (!remote_pub || !e || !out || !len) return OK_ARGUMENT;
    if (cap < BODY) return OK_RANGE;
    if ((r = ok_keys_public(e->eph, eph)) != OK_OK) return r;
    ok_rlp_init(&w, out + BODY, cap - BODY);
    mark = ok_rlp_list_begin(&w);
    ok_rlp_bytes(&w, eph, 64);
    ok_rlp_bytes(&w, e->nonce, 32);
    ok_rlp_uint(&w, 4);
    ok_rlp_list_end(&w, mark);
    return seal(remote_pub, e, &w, out, cap, len);
}

/* Decrypts in place and returns the body list (padding after it is ignored). */
static ok_result open_packet(const uint8_t priv[32], uint8_t *packet, size_t n, ok_rlp_item *body)
{
    size_t len, used;
    ok_result r;
    if (!priv || !packet || !body) return OK_ARGUMENT;
    if (n < 2 || n > OK_RLPX_MAX_PACKET || ok_rlpx_packet_len(packet) != n) return OK_FORMAT;
    r = ok_ecies_decrypt(priv, packet + 2, n - 2, packet, 2, packet + 2, n - 2, &len);
    if (r != OK_OK) return r;
    if (ok_rlp_item_at(packet + 2, len, body, &used) != OK_OK || !body->list) return OK_FORMAT;
    return OK_OK;
}

static int field(const ok_rlp_item *list, size_t *pos, ok_rlp_item *item, size_t len)
{
    return ok_rlp_next(list, pos, item) == OK_OK && !item->list && item->len == len;
}

static int version(const ok_rlp_item *list, size_t *pos, uint64_t *v)
{
    ok_rlp_item item;
    return ok_rlp_next(list, pos, &item) == OK_OK && !item.list && ok_rlp_to_uint(&item, v) == OK_OK;
}

ok_result ok_rlpx_auth_read(const uint8_t priv[32], uint8_t *packet, size_t n, ok_rlpx_remote *remote)
{
    secp256k1_ecdsa_recoverable_signature sig;
    secp256k1_pubkey key;
    const secp256k1_context *ctx = ok_secp();
    ok_rlp_item body, s, p, k;
    uint8_t msg[32], eph[65];
    size_t pos = 0, eph_len = sizeof eph;
    ok_result r;
    if (!remote) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    if ((r = open_packet(priv, packet, n, &body)) != OK_OK) return r;
    if (!field(&body, &pos, &s, 65) || !field(&body, &pos, &p, 64) || !field(&body, &pos, &k, 32) ||
        !version(&body, &pos, &remote->version))
        return OK_FORMAT;
    memcpy(remote->pub, p.data, 64);
    memcpy(remote->nonce, k.data, 32);
    if ((r = signed_message(priv, remote->pub, remote->nonce, msg)) != OK_OK) return r;
    if (s.data[64] > 3 || !secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig, s.data, s.data[64]) ||
        !secp256k1_ecdsa_recover(ctx, &key, &sig, msg))
        r = OK_CRYPTO;
    ok_wipe(msg, sizeof msg);
    if (r != OK_OK) return r;
    secp256k1_ec_pubkey_serialize(ctx, eph, &eph_len, &key, SECP256K1_EC_UNCOMPRESSED);
    memcpy(remote->eph, eph + 1, 64);
    return OK_OK;
}

ok_result ok_rlpx_ack_read(const uint8_t priv[32], uint8_t *packet, size_t n, ok_rlpx_remote *remote)
{
    ok_rlp_item body, p, k;
    size_t pos = 0;
    ok_result r;
    if (!remote) return OK_ARGUMENT;
    if ((r = open_packet(priv, packet, n, &body)) != OK_OK) return r;
    if (!field(&body, &pos, &p, 64) || !field(&body, &pos, &k, 32) || !version(&body, &pos, &remote->version))
        return OK_FORMAT;
    memset(remote->pub, 0, 64);
    memcpy(remote->eph, p.data, 64);
    memcpy(remote->nonce, k.data, 32);
    return OK_OK;
}

ok_result ok_rlpx_derive(const uint8_t eph_priv[32], const uint8_t remote_eph[64], const uint8_t init_nonce[32],
                         const uint8_t resp_nonce[32], uint8_t aes_secret[32], uint8_t mac_secret[32])
{
    uint8_t buf[64];
    ok_keccak k;
    ok_result r;
    if (!eph_priv || !remote_eph || !init_nonce || !resp_nonce || !aes_secret || !mac_secret) return OK_ARGUMENT;
    if ((r = ok_ecdh(eph_priv, remote_eph, buf)) != OK_OK) return r;
    ok_keccak_init(&k);
    ok_keccak_update(&k, resp_nonce, 32);
    ok_keccak_update(&k, init_nonce, 32);
    ok_keccak_final(&k, buf + 32);
    ok_keccak256(buf, 64, buf + 32); /* shared secret */
    ok_keccak256(buf, 64, aes_secret);
    memcpy(buf + 32, aes_secret, 32);
    ok_keccak256(buf, 64, mac_secret);
    ok_wipe(buf, sizeof buf);
    return OK_OK;
}

static void mac_start(ok_keccak *k, const uint8_t mac_secret[32], const uint8_t nonce[32], const uint8_t *packet, size_t n)
{
    uint8_t x[32];
    int i;
    for (i = 0; i < 32; i++) x[i] = (uint8_t)(mac_secret[i] ^ nonce[i]);
    ok_keccak_init(k);
    ok_keccak_update(k, x, 32);
    ok_keccak_update(k, packet, n);
    ok_wipe(x, sizeof x);
}

ok_result ok_rlpx_session_init(ok_rlpx_session *s, int initiator, const uint8_t eph_priv[32],
                               const uint8_t own_nonce[32], const ok_rlpx_remote *remote,
                               const uint8_t *auth, size_t auth_len, const uint8_t *ack, size_t ack_len)
{
    static const uint8_t zero[16] = {0};
    const uint8_t *init_nonce, *resp_nonce;
    uint8_t aes_secret[32], mac_secret[32];
    ok_result r;
    if (!s || !eph_priv || !own_nonce || !remote || !auth || !ack) return OK_ARGUMENT;
    init_nonce = initiator ? own_nonce : remote->nonce;
    resp_nonce = initiator ? remote->nonce : own_nonce;
    r = ok_rlpx_derive(eph_priv, remote->eph, init_nonce, resp_nonce, aes_secret, mac_secret);
    if (r == OK_OK) r = ok_aes_ctr_init(&s->enc, aes_secret, 32, zero);
    if (r == OK_OK) r = ok_aes_ctr_init(&s->dec, aes_secret, 32, zero);
    if (r == OK_OK) r = ok_aes_init(&s->mac, mac_secret, 32);
    if (r == OK_OK) {
        /* The auth is MACed under the recipient's nonce, the ack under the initiator's. */
        mac_start(initiator ? &s->egress : &s->ingress, mac_secret, resp_nonce, auth, auth_len);
        mac_start(initiator ? &s->ingress : &s->egress, mac_secret, init_nonce, ack, ack_len);
    }
    ok_wipe(aes_secret, sizeof aes_secret);
    ok_wipe(mac_secret, sizeof mac_secret);
    if (r != OK_OK) ok_wipe(s, sizeof *s);
    return r;
}

static size_t padded(size_t n) { return (n + 15) & ~(size_t)15; }

size_t ok_rlpx_frame_len(size_t n) { return OK_RLPX_HEADER + padded(n) + 16; }

static void digest16(const ok_keccak *k, uint8_t out[16])
{
    ok_keccak copy = *k;
    uint8_t d[32];
    ok_keccak_final(&copy, d);
    memcpy(out, d, 16);
}

/* Header MAC: seed = aes(mac, digest) ^ header ciphertext. */
static void header_mac(ok_rlpx_session *s, ok_keccak *k, const uint8_t ct[16], uint8_t out[16])
{
    uint8_t seed[16];
    int i;
    digest16(k, seed);
    ok_aes_block(&s->mac, seed, seed);
    for (i = 0; i < 16; i++) seed[i] ^= ct[i];
    ok_keccak_update(k, seed, 16);
    digest16(k, out);
}

/* Frame MAC: absorb the ciphertext, then seed = aes(mac, digest) ^ digest. */
static void frame_mac(ok_rlpx_session *s, ok_keccak *k, const uint8_t *ct, size_t n, uint8_t out[16])
{
    uint8_t d[16], seed[16];
    int i;
    ok_keccak_update(k, ct, n);
    digest16(k, d);
    ok_aes_block(&s->mac, d, seed);
    for (i = 0; i < 16; i++) seed[i] ^= d[i];
    ok_keccak_update(k, seed, 16);
    digest16(k, out);
}

static int differ(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    size_t i;
    for (i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d != 0;
}

ok_result ok_rlpx_frame_write(ok_rlpx_session *s, const uint8_t *data, size_t n, uint8_t *out, size_t cap, size_t *len)
{
    size_t p;
    if (!s || (!data && n) || !out || !len) return OK_ARGUMENT;
    if (n > OK_RLPX_MAX_FRAME) return OK_RANGE;
    p = padded(n);
    if (cap < ok_rlpx_frame_len(n)) return OK_RANGE;
    if (n) memmove(out + OK_RLPX_HEADER, data, n);
    memset(out + OK_RLPX_HEADER + n, 0, p - n);
    memset(out, 0, 16);
    out[0] = (uint8_t)(n >> 16);
    out[1] = (uint8_t)(n >> 8);
    out[2] = (uint8_t)n;
    out[3] = 0xc2; /* [capability-id 0, context-id 0] */
    out[4] = 0x80;
    out[5] = 0x80;
    ok_aes_ctr_run(&s->enc, out, 16);
    header_mac(s, &s->egress, out, out + 16);
    ok_aes_ctr_run(&s->enc, out + OK_RLPX_HEADER, p);
    frame_mac(s, &s->egress, out + OK_RLPX_HEADER, p, out + OK_RLPX_HEADER + p);
    *len = ok_rlpx_frame_len(n);
    return OK_OK;
}

ok_result ok_rlpx_header_read(ok_rlpx_session *s, uint8_t header[OK_RLPX_HEADER], size_t *size)
{
    uint8_t want[16];
    if (!s || !header || !size) return OK_ARGUMENT;
    header_mac(s, &s->ingress, header, want);
    if (differ(want, header + 16, 16)) return OK_CRYPTO;
    ok_aes_ctr_run(&s->dec, header, 16);
    *size = (size_t)header[0] << 16 | (size_t)header[1] << 8 | header[2];
    return OK_OK;
}

ok_result ok_rlpx_body_read(ok_rlpx_session *s, uint8_t *body, size_t size)
{
    uint8_t want[16];
    size_t p;
    if (!s || !body) return OK_ARGUMENT;
    if (size > OK_RLPX_MAX_FRAME) return OK_RANGE;
    p = padded(size);
    frame_mac(s, &s->ingress, body, p, want);
    if (differ(want, body + p, 16)) return OK_CRYPTO;
    ok_aes_ctr_run(&s->dec, body, p);
    return OK_OK;
}
