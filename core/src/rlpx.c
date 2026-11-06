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
