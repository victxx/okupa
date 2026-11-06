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

#endif
