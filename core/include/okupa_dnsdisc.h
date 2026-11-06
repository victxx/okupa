#ifndef OKUPA_DNSDISC_H
#define OKUPA_DNSDISC_H

#include "okupa.h"

/* EIP-1459 node lists in DNS TXT records. The tree is signed at the root by a
 * known key; every child is named by the hash of its own text, so a resolver
 * can withhold records but cannot forge them. */
#define OK_DNSDISC_HASH 26 /* base32 of 16 bytes, unpadded */

enum { OK_DNSDISC_ROOT, OK_DNSDISC_BRANCH, OK_DNSDISC_ENR, OK_DNSDISC_LINK, OK_DNSDISC_UNKNOWN };

typedef struct {
    char enr_root[OK_DNSDISC_HASH + 1];  /* subtree of node records */
    char link_root[OK_DNSDISC_HASH + 1]; /* subtree of links to other trees */
    uint64_t seq;
} ok_dnsdisc_root;

/* "enrtree://<base32 compressed key>@<domain>". */
ok_result ok_dnsdisc_url(const char *url, uint8_t key[33], char *domain, size_t cap);
int ok_dnsdisc_kind(const char *txt, size_t n);
/* Checks the root's signature against the tree key. */
ok_result ok_dnsdisc_root_parse(const char *txt, size_t n, const uint8_t key[33], ok_dnsdisc_root *root);
/* 1 if txt is really the record named hash (base32 of its keccak, first 16 bytes). */
int ok_dnsdisc_matches(const char *hash, size_t hash_len, const char *txt, size_t n);
/* Walks "enrtree-branch:h1,h2,...": start with *pos = 0; OK_RANGE at the end. */
ok_result ok_dnsdisc_branch_next(const char *txt, size_t n, size_t *pos, char child[OK_DNSDISC_HASH + 1]);

/* The whole walk: verified root, then depth first through branches, every
 * record checked against its name and every ENR against its signature. The
 * platform answers TXT lookups (lookup returns 1 with the text, 0 if none);
 * found() gets each node that announces want_fork. Returns nodes found. */
#define OK_DNSDISC_STACK 128
typedef struct {
    int (*lookup)(void *ctx, const char *name, char *txt, size_t cap, size_t *len);
    void (*found)(void *ctx, const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp);
    void *ctx;
    unsigned records, refused; /* filled in by the walk */
    uint64_t seq;
    char stack[OK_DNSDISC_STACK][OK_DNSDISC_HASH + 1];
} ok_dnsdisc_walk;

int ok_dnsdisc_walk_tree(ok_dnsdisc_walk *w, const char *url, const uint8_t want_fork[4], int limit);

#endif
