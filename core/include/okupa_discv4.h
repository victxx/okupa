#ifndef OKUPA_DISCV4_H
#define OKUPA_DISCV4_H

#include "okupa.h"
#include "okupa_enr.h"
#include "okupa_rlp.h"

/* Node discovery v4 over UDP: hash || signature || type || RLP data. The
 * signature also tells us who sent it. Readers accept extra list elements and
 * trailing bytes, as EIP-8 asks. Only IPv4 endpoints are used. */
enum { OK_V4_PING = 1, OK_V4_PONG, OK_V4_FINDNODE, OK_V4_NEIGHBORS, OK_V4_ENRREQUEST, OK_V4_ENRRESPONSE };
#define OK_V4_MAX 1280

typedef struct {
    uint8_t ip[4];
    uint16_t udp, tcp;
} ok_v4_endpoint;

typedef struct {
    int type;
    uint8_t hash[32];   /* the packet's own hash: Pong and ENRResponse quote it */
    uint8_t sender[64]; /* recovered from the signature */
    ok_rlp_item data;   /* the packet's list */
} ok_v4_packet;

typedef struct {
    ok_v4_endpoint at;
    uint8_t id[64];
} ok_v4_node;

ok_result ok_v4_decode(const uint8_t *in, size_t n, ok_v4_packet *p);
/* Same without recovering the sender (p->sender is zeroed): the hash still
 * guards against damage. For a crawl that knows peers by address, where a
 * signature check per packet would be too slow on the console; ENRs that
 * matter are still verified by their own signature. */
ok_result ok_v4_decode_light(const uint8_t *in, size_t n, ok_v4_packet *p);

/* Writers: each returns the packet's hash, which the answer will quote. */
ok_result ok_v4_ping(const uint8_t priv[32], const ok_v4_endpoint *from, const ok_v4_endpoint *to, uint64_t expiration,
                     uint8_t *out, size_t cap, size_t *len, uint8_t hash[32]);
ok_result ok_v4_pong(const uint8_t priv[32], const ok_v4_endpoint *to, const uint8_t ping_hash[32], uint64_t expiration,
                     uint8_t *out, size_t cap, size_t *len);
ok_result ok_v4_findnode(const uint8_t priv[32], const uint8_t target[64], uint64_t expiration,
                         uint8_t *out, size_t cap, size_t *len);
ok_result ok_v4_enrrequest(const uint8_t priv[32], uint64_t expiration, uint8_t *out, size_t cap, size_t *len,
                           uint8_t hash[32]);

/* Readers for a decoded packet of the right type. */
ok_result ok_v4_pong_read(const ok_v4_packet *p, uint8_t ping_hash[32]);
/* Walk the nodes of a Neighbors packet: *pos = 0 first; OK_RANGE at the end.
 * Nodes with IPv6 addresses are skipped. */
ok_result ok_v4_neighbors_next(const ok_v4_packet *p, size_t *pos, ok_v4_node *node);
ok_result ok_v4_enrresponse_read(const ok_v4_packet *p, uint8_t request_hash[32], ok_enr *enr);

/* A bounded crawl: ping what we know, ask bonded nodes for neighbours and for
 * their ENR, and report the ones on the wanted network. The platform moves
 * the UDP packets and calls ok_v4_crawl_step regularly with the time. */
#define OK_V4_TABLE 384

typedef struct {
    uint8_t id[64];
    ok_v4_endpoint at;
    uint8_t state, tries;
    uint32_t last;
    uint8_t enr_request[32];
} ok_v4_entry;

typedef struct {
    int (*send)(void *ctx, const ok_v4_endpoint *to, const uint8_t *packet, size_t n);
    int (*recv)(void *ctx, uint8_t *buf, size_t cap, ok_v4_endpoint *from); /* >0 bytes, 0 none waiting */
    void (*found)(void *ctx, const ok_enr *enr);
    void *ctx;
    uint8_t priv[32], want_fork[4];
    ok_v4_entry table[OK_V4_TABLE];
    int count, cursor;
    unsigned sent, received, bonded, records, found_count;
    uint32_t rng;
} ok_v4_crawl;

void ok_v4_crawl_init(ok_v4_crawl *c, const uint8_t priv[32], const uint8_t want_fork[4], uint32_t seed);
/* Start from a node record ("enr:..."), e.g. a network's bootnodes. */
ok_result ok_v4_crawl_add_enr(ok_v4_crawl *c, const char *text);
void ok_v4_crawl_step(ok_v4_crawl *c, uint32_t now);

/* Sepolia's bootnodes as go-ethereum v1.17.7 ships them (params/bootnodes.go). */
extern const char *const ok_sepolia_bootnodes[];
extern const int ok_sepolia_bootnode_count;

#endif
