#include "okupa_discv4.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"
#include <string.h>

#define HEAD (32 + 65)

ok_result ok_v4_decode(const uint8_t *in, size_t n, ok_v4_packet *p)
{
    const secp256k1_context *ctx = ok_secp();
    secp256k1_ecdsa_recoverable_signature sig;
    secp256k1_pubkey key;
    uint8_t digest[32], pub[65];
    size_t used, pub_len = sizeof pub;
    if (!in || !p) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    if (n < HEAD + 2 || n > OK_V4_MAX) return OK_FORMAT;
    ok_keccak256(in + 32, n - 32, digest);
    if (memcmp(digest, in, 32)) return OK_FORMAT;
    ok_keccak256(in + HEAD, n - HEAD, digest);
    if (in[HEAD - 1] > 3 || !secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig, in + 32, in[HEAD - 1]) ||
        !secp256k1_ecdsa_recover(ctx, &key, &sig, digest))
        return OK_CRYPTO;
    secp256k1_ec_pubkey_serialize(ctx, pub, &pub_len, &key, SECP256K1_EC_UNCOMPRESSED);
    memcpy(p->sender, pub + 1, 64);
    memcpy(p->hash, in, 32);
    p->type = in[HEAD];
    if (ok_rlp_item_at(in + HEAD + 1, n - HEAD - 1, &p->data, &used) != OK_OK || !p->data.list) return OK_FORMAT;
    return OK_OK;
}

/* Signs type || data in place: out = hash || sig || type || data. */
static ok_result seal(const uint8_t priv[32], uint8_t *out, size_t len, uint8_t hash[32])
{
    const secp256k1_context *ctx = ok_secp();
    secp256k1_ecdsa_recoverable_signature sig;
    uint8_t digest[32];
    int recid;
    if (!ctx) return OK_CRYPTO;
    ok_keccak256(out + HEAD, len - HEAD, digest);
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, digest, priv, NULL, NULL)) return OK_CRYPTO;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, out + 32, &recid, &sig);
    out[HEAD - 1] = (uint8_t)recid;
    ok_keccak256(out + 32, len - 32, out);
    if (hash) memcpy(hash, out, 32);
    return OK_OK;
}

static void endpoint(ok_rlp_writer *w, const ok_v4_endpoint *e)
{
    size_t m = ok_rlp_list_begin(w);
    ok_rlp_bytes(w, e->ip, 4);
    ok_rlp_uint(w, e->udp);
    ok_rlp_uint(w, e->tcp);
    ok_rlp_list_end(w, m);
}

/* The body is written after the header; type goes right before it. */
static ok_result finish(const uint8_t priv[32], ok_rlp_writer *w, uint8_t *out, int type, size_t *len, uint8_t hash[32])
{
    size_t body;
    ok_result r = ok_rlp_done(w, &body);
    if (r != OK_OK) return r;
    out[HEAD] = (uint8_t)type;
    *len = HEAD + 1 + body;
    return seal(priv, out, *len, hash);
}

static int start(ok_rlp_writer *w, uint8_t *out, size_t cap)
{
    if (!out || cap < HEAD + 2) return 0;
    ok_rlp_init(w, out + HEAD + 1, (cap < OK_V4_MAX ? cap : OK_V4_MAX) - HEAD - 1);
    return 1;
}

ok_result ok_v4_ping(const uint8_t priv[32], const ok_v4_endpoint *from, const ok_v4_endpoint *to, uint64_t expiration,
                     uint8_t *out, size_t cap, size_t *len, uint8_t hash[32])
{
    ok_rlp_writer w;
    size_t m;
    if (!priv || !from || !to || !len || !start(&w, out, cap)) return OK_ARGUMENT;
    m = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, 4);
    endpoint(&w, from);
    endpoint(&w, to);
    ok_rlp_uint(&w, expiration);
    ok_rlp_list_end(&w, m);
    return finish(priv, &w, out, OK_V4_PING, len, hash);
}

ok_result ok_v4_pong(const uint8_t priv[32], const ok_v4_endpoint *to, const uint8_t ping_hash[32], uint64_t expiration,
                     uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t m;
    if (!priv || !to || !ping_hash || !len || !start(&w, out, cap)) return OK_ARGUMENT;
    m = ok_rlp_list_begin(&w);
    endpoint(&w, to);
    ok_rlp_bytes(&w, ping_hash, 32);
    ok_rlp_uint(&w, expiration);
    ok_rlp_list_end(&w, m);
    return finish(priv, &w, out, OK_V4_PONG, len, NULL);
}

ok_result ok_v4_findnode(const uint8_t priv[32], const uint8_t target[64], uint64_t expiration,
                         uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t m;
    if (!priv || !target || !len || !start(&w, out, cap)) return OK_ARGUMENT;
    m = ok_rlp_list_begin(&w);
    ok_rlp_bytes(&w, target, 64);
    ok_rlp_uint(&w, expiration);
    ok_rlp_list_end(&w, m);
    return finish(priv, &w, out, OK_V4_FINDNODE, len, NULL);
}

ok_result ok_v4_enrrequest(const uint8_t priv[32], uint64_t expiration, uint8_t *out, size_t cap, size_t *len,
                           uint8_t hash[32])
{
    ok_rlp_writer w;
    size_t m;
    if (!priv || !len || !start(&w, out, cap)) return OK_ARGUMENT;
    m = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, expiration);
    ok_rlp_list_end(&w, m);
    return finish(priv, &w, out, OK_V4_ENRREQUEST, len, hash);
}

ok_result ok_v4_pong_read(const ok_v4_packet *p, uint8_t ping_hash[32])
{
    ok_rlp_item to, h;
    size_t pos = 0;
    if (!p || !ping_hash) return OK_ARGUMENT;
    if (p->type != OK_V4_PONG || ok_rlp_next(&p->data, &pos, &to) != OK_OK || ok_rlp_next(&p->data, &pos, &h) != OK_OK ||
        h.list || h.len != 32)
        return OK_FORMAT;
    memcpy(ping_hash, h.data, 32);
    return OK_OK;
}

static int port(const ok_rlp_item *v, uint16_t *out)
{
    uint64_t x;
    if (ok_rlp_to_uint(v, &x) != OK_OK || x > 65535) return 0;
    *out = (uint16_t)x;
    return 1;
}

ok_result ok_v4_neighbors_next(const ok_v4_packet *p, size_t *pos, ok_v4_node *node)
{
    ok_rlp_item nodes, entry, ip, udp, tcp, id;
    size_t first = 0;
    if (!p || !pos || !node) return OK_ARGUMENT;
    if (p->type != OK_V4_NEIGHBORS || ok_rlp_next(&p->data, &first, &nodes) != OK_OK || !nodes.list) return OK_FORMAT;
    for (;;) {
        size_t epos = 0;
        ok_result r = ok_rlp_next(&nodes, pos, &entry);
        if (r != OK_OK) return r;
        if (!entry.list || ok_rlp_next(&entry, &epos, &ip) != OK_OK || ok_rlp_next(&entry, &epos, &udp) != OK_OK ||
            ok_rlp_next(&entry, &epos, &tcp) != OK_OK || ok_rlp_next(&entry, &epos, &id) != OK_OK || ip.list ||
            id.list || id.len != 64 || !port(&udp, &node->at.udp) || !port(&tcp, &node->at.tcp))
            return OK_FORMAT;
        if (ip.len != 4) continue; /* IPv6: not for us */
        memcpy(node->at.ip, ip.data, 4);
        memcpy(node->id, id.data, 64);
        return OK_OK;
    }
}

ok_result ok_v4_enrresponse_read(const ok_v4_packet *p, uint8_t request_hash[32], ok_enr *enr)
{
    ok_rlp_item h, record;
    size_t pos = 0;
    if (!p || !request_hash || !enr) return OK_ARGUMENT;
    if (p->type != OK_V4_ENRRESPONSE || ok_rlp_next(&p->data, &pos, &h) != OK_OK || h.list || h.len != 32 ||
        ok_rlp_next(&p->data, &pos, &record) != OK_OK || !record.list)
        return OK_FORMAT;
    memcpy(request_hash, h.data, 32);
    /* The record is the whole RLP list including its header: rebuild that span. */
    {
        const uint8_t *start = record.data;
        size_t header = 1;
        if (record.len >= 56) {
            size_t len = record.len;
            while (len) {
                header++;
                len >>= 8;
            }
        }
        return ok_enr_parse(start - header, record.len + header, enr);
    }
}
