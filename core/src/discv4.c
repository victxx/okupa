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

ok_result ok_v4_decode_light(const uint8_t *in, size_t n, ok_v4_packet *p)
{
    uint8_t digest[32];
    size_t used;
    if (!in || !p) return OK_ARGUMENT;
    if (n < HEAD + 2 || n > OK_V4_MAX) return OK_FORMAT;
    ok_keccak256(in + 32, n - 32, digest);
    if (memcmp(digest, in, 32)) return OK_FORMAT;
    memset(p->sender, 0, 64);
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

const char *const ok_sepolia_bootnodes[] = {
    "enr:-KG4QHn1Os72NN5Z44Ehf9CHPmN490GBphTFirfk33pFuWnkY9UNrQMfHkuCw6a-ykeRhGKrPL5ppMXiZQchgPrCDJQBgmlkgnY0gmlwhNRj2kKDaXA2kCoAHKALAA0CAAAAAAAAAF6Jc2VjcDI1NmsxoQJK_ye9jx9mela-ME_Kt5e017Ywv3hYH_5rwNhIUrtzNoN1ZHCCTriEdWRwNoJOuA",
    "enr:-KG4QCpiRltiyX7LgozCkQ8XBrcDzejBXbE7YRUQFmIEgv46SC8B4CJLcOO3O2bH-_u-B242GTzO_hQPhv3rpyvAHXMBgmlkgnY0gmlwhIHUpj2DaXA2kCYEqIAABAHQAAAAA2GdUACJc2VjcDI1NmsxoQNmVWXve5c0uvsn_agjTKQ8pR6gl9DynZyTQNruBDfJ4YN1ZHCCdsOEdWRwNoJ2ww",
    "enr:-KG4QJUWvc8qvCUr7x-qCFWee6HPLgmRUfVn-M_yqXD-sjSoAalKCV3NgUhmlqozjTJIj_nPr7J_uT8WJwbWiIx3Cv4BgmlkgnY0gmlwhJB-_BiDaXA2kCQAYYABAADQAAAAAYEgYAGJc2VjcDI1NmsxoQKOQetrA-97TELUzuGegVD1_cHKKNnjOmJ9CYdeSTor7YN1ZHCCdsOEdWRwNoJ2ww",
    "enr:-KG4QIBsIiST0JBBvgSNxvHQOeH5PkRgWeufahVXYQQWj7GLYPFDsviuQ6qU-3Xx4qnARaZiI-qqVRyw_2qqmOKRY1UBgmlkgnY0gmlwhLKc14yDaXA2kCoBBP8A9DxKAAAAAAAAAAGJc2VjcDI1NmsxoQKx4n3wy0KtwnuZCHmlxMmc4b0VvfC4Ka_f3sUPvjUq1oN1ZHCCdsOEdWRwNoJ2ww",
    "enr:-KG4QPM5C6wuTxu5yk4jkI9Mm64iSUKMPNt8y8kmoFzOlYrnYV4SBeZK7UiS4YWrkX4y2FD9_eH7xiPGvLQ-nXoXIcYBgmlkgnY0gmlwhAXfXlGDaXA2kCoBBP8C8BytAAAAAAAAAAGJc2VjcDI1NmsxoQIC3FMD8Si9DIBVofsSbJkpvXTUAfyi8q66XXQgjRhUsIN1ZHCCdsOEdWRwNoJ2ww",
};
const int ok_sepolia_bootnode_count = sizeof ok_sepolia_bootnodes / sizeof ok_sepolia_bootnodes[0];

const char *const ok_mainnet_bootnodes[] = {
    "enr:-KG4QCF1Mj32xpKHjinNb6ocCtMZG6IR_tyF5dkio5Hkek7zVbT6MM5eJwhjJFdiksQl51T33IRgryE0XLXiy1QOqsUBgmlkgnY0gmlwhNRj2kKDaXA2kCoAHKALAA0CAAAAAAAAAF6Jc2VjcDI1NmsxoQLKlnQYuhZRBTA8-7cz37kr_KuA1lAJ1eXxWMjp5fLJB4N1ZHCCTreEdWRwNoJOtw",
    "enr:-KG4QMHihBfLI_tpXLWmfTi28HItJa4wDADDAaVjkfSELAb0BajqVWXM2GEMds5J63SCkpDRlqct5E8ftfQi4U93pigBgmlkgnY0gmlwhIHUpj2DaXA2kCYEqIAABAHQAAAAA2GdUACJc2VjcDI1NmsxoQLeQmW8OMuoUIoUImNW-r9IC4jaiAwZ2knfV4BqWyQvKoN1ZHCCdl-EdWRwNoJ2Xw",
    "enr:-KG4QKsHcKQvIX8UPbJgw2cIEP_9EislF1srNGdYom4dcSsobt-92iEDNkHTt9dcdKXHaDMVbCM8QQ4V1RP8HcExLDsBgmlkgnY0gmlwhJB-_BiDaXA2kCQAYYABAADQAAAAAYEgYAGJc2VjcDI1NmsxoQILdc6UClD5s_N8-hYhIGQtFqRYR3Qxf9qgLQ7sj4qvEYN1ZHCCdl-EdWRwNoJ2Xw",
    "enr:-KG4QFplptv1jVEhFeNNxRN7qe5p9rAGGlMeNnmGvUJV23W4Kr8hmiSzdWjZZHFqwi7nOuQrM32VO6uHBORXOdLG98gBgmlkgnY0gmlwhLKc14yDaXA2kCoBBP8A9DxKAAAAAAAAAAGJc2VjcDI1NmsxoQMBRycp4yiHbGzOqZPPMmuKGslp1NpOrFf_predLYM4-oN1ZHCCdl-EdWRwNoJ2Xw",
    "enr:-KG4QGY74qTrst5NbQDAMIF8B7jEC8CMBSo6C5Ljea5I4PxYV1VSYdsGUqqIBlOYRBfwhl6YpEyvvvZOqNCa4dvmSXgBgmlkgnY0gmlwhAXfXlGDaXA2kCoBBP8C8BytAAAAAAAAAAGJc2VjcDI1NmsxoQIMlJp7yNcblauYicEeSBpHQzFAk9Wg3lhx3ie9gpTohIN1ZHCCdl-EdWRwNoJ2Xw",
};
const int ok_mainnet_bootnode_count = sizeof ok_mainnet_bootnodes / sizeof ok_mainnet_bootnodes[0];

enum { NEW, PINGED, BONDED, ASKED, DONE };
#define EXPIRY 60 /* seconds; generous, as console clocks are often off by a bit */

void ok_v4_crawl_init(ok_v4_crawl *c, const uint8_t priv[32], const uint8_t want_fork[4], uint32_t seed)
{
    void *ctx = c->ctx;
    int (*send)(void *, const ok_v4_endpoint *, const uint8_t *, size_t) = c->send;
    int (*recv)(void *, uint8_t *, size_t, ok_v4_endpoint *) = c->recv;
    void (*found)(void *, const ok_enr *) = c->found;
    memset(c, 0, sizeof *c);
    c->ctx = ctx;
    c->send = send;
    c->recv = recv;
    c->found = found;
    memcpy(c->priv, priv, 32);
    memcpy(c->want_fork, want_fork, 4);
    c->rng = seed ? seed : 0x6f6b7061u;
}

static ok_v4_entry *add(ok_v4_crawl *c, const uint8_t id[64], const ok_v4_endpoint *at)
{
    int i;
    for (i = 0; i < c->count; i++)
        if (!memcmp(c->table[i].id, id, 64)) return NULL;
    if (c->count >= OK_V4_TABLE || !at->udp) return NULL;
    memset(&c->table[c->count], 0, sizeof c->table[0]);
    memcpy(c->table[c->count].id, id, 64);
    c->table[c->count].at = *at;
    return &c->table[c->count++];
}

ok_result ok_v4_crawl_add_enr(ok_v4_crawl *c, const char *text)
{
    ok_enr e;
    ok_v4_endpoint at;
    ok_result r = ok_enr_parse_text(text, strlen(text), &e);
    if (r != OK_OK) return r;
    if (!e.has_ip || !e.udp) return OK_FORMAT;
    memcpy(at.ip, e.ip, 4);
    at.udp = e.udp;
    at.tcp = e.tcp;
    add(c, e.pub, &at);
    return OK_OK;
}

void ok_v4_crawl_add_node(ok_v4_crawl *c, const uint8_t id[64], const ok_v4_endpoint *at)
{
    ok_v4_endpoint e = *at;
    if (!e.udp) e.udp = e.tcp;
    add(c, id, &e);
}

static ok_v4_entry *by_address(ok_v4_crawl *c, const ok_v4_endpoint *from)
{
    int i;
    for (i = 0; i < c->count; i++)
        if (!memcmp(c->table[i].at.ip, from->ip, 4) && c->table[i].at.udp == from->udp) return &c->table[i];
    return NULL;
}

static uint32_t next_random(ok_v4_crawl *c)
{
    c->rng ^= c->rng << 13;
    c->rng ^= c->rng >> 17;
    c->rng ^= c->rng << 5;
    return c->rng;
}

static void handle(ok_v4_crawl *c, const uint8_t *buf, size_t n, const ok_v4_endpoint *from, uint32_t now)
{
    static uint8_t out[OK_V4_MAX];
    ok_v4_packet p;
    ok_v4_entry *e;
    size_t len, pos = 0;
    if (ok_v4_decode_light(buf, n, &p) != OK_OK) return;
    c->received++;
    e = by_address(c, from);
    if (p.type == OK_V4_PING) {
        /* Answering is how they learn we are a real node. */
        if (ok_v4_pong(c->priv, from, p.hash, now + EXPIRY, out, sizeof out, &len) == OK_OK) {
            c->send(c->ctx, from, out, len);
            c->sent++;
        }
        if (e && e->state < BONDED) {
            e->state = BONDED;
            e->last = now;
            c->bonded++;
        }
    } else if (p.type == OK_V4_PONG) {
        if (e && e->state < BONDED) {
            e->state = BONDED;
            e->last = now;
            c->bonded++;
        }
    } else if (p.type == OK_V4_NEIGHBORS) {
        ok_v4_node node;
        while (ok_v4_neighbors_next(&p, &pos, &node) == OK_OK) add(c, node.id, &node.at);
    } else if (p.type == OK_V4_ENRRESPONSE && e && e->state == ASKED) {
        uint8_t request[32];
        ok_enr enr;
        if (ok_v4_enrresponse_read(&p, request, &enr) != OK_OK || memcmp(request, e->enr_request, 32)) return;
        e->state = DONE;
        c->records++;
        /* The record is self-signed: only trust it if it is the node we asked. */
        if (memcmp(enr.pub, e->id, 64) || !enr.has_eth || memcmp(enr.fork_hash, c->want_fork, 4) || !enr.has_ip ||
            !enr.tcp)
            return;
        c->found_count++;
        c->found(c->ctx, &enr);
    }
}

void ok_v4_crawl_step(ok_v4_crawl *c, uint32_t now)
{
    static uint8_t buf[OK_V4_MAX];
    ok_v4_endpoint from, self;
    int i, budget = 8;
    for (i = 0; i < 32; i++) {
        int n = c->recv(c->ctx, buf, sizeof buf, &from);
        if (n <= 0) break;
        handle(c, buf, (size_t)n, &from, now);
    }
    memset(&self, 0, sizeof self);
    for (i = 0; i < c->count && budget > 0; i++) {
        ok_v4_entry *e = &c->table[c->cursor];
        size_t len;
        c->cursor = (c->cursor + 1) % c->count;
        if (e->state == NEW || (e->state == PINGED && now - e->last >= 3 && e->tries < 2)) {
            if (ok_v4_ping(c->priv, &self, &e->at, now + EXPIRY, buf, sizeof buf, &len, NULL) != OK_OK) continue;
            c->send(c->ctx, &e->at, buf, len);
            e->state = PINGED;
            e->tries++;
            e->last = now;
            budget--;
            c->sent++;
        } else if (e->state == BONDED && now - e->last >= 1) {
            uint8_t target[64];
            int k;
            for (k = 0; k < 64; k += 4) {
                uint32_t r = next_random(c);
                memcpy(target + k, &r, 4);
            }
            if (ok_v4_findnode(c->priv, target, now + EXPIRY, buf, sizeof buf, &len) == OK_OK) c->send(c->ctx, &e->at, buf, len);
            if (ok_v4_enrrequest(c->priv, now + EXPIRY, buf, sizeof buf, &len, e->enr_request) == OK_OK)
                c->send(c->ctx, &e->at, buf, len);
            e->state = ASKED;
            e->last = now;
            budget -= 2;
            c->sent += 2;
        }
    }
}
