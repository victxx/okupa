#ifndef OKUPA_ENR_H
#define OKUPA_ENR_H

#include "okupa.h"

/* Ethereum Node Records (EIP-778), "v4" identity only: [sig, seq, k, v, ...]
 * with sorted, unique keys, signed by the node's secp256k1 key. Records are
 * accepted only with a valid signature. */
#define OK_ENR_MAX 300

typedef struct {
    uint64_t seq;
    uint8_t pub[64];  /* the node's key, uncompressed without 0x04: what enode:// carries */
    uint8_t ip[4];
    uint16_t tcp, udp;
    int has_ip, has_eth;
    uint8_t fork_hash[4];
    uint64_t fork_next;
} ok_enr;

ok_result ok_enr_parse(const uint8_t *raw, size_t n, ok_enr *enr);
/* The text form: "enr:" + unpadded URL-safe base64. */
ok_result ok_enr_parse_text(const char *text, size_t n, ok_enr *enr);
/* Unpadded URL-safe base64, as ENR and EIP-1459 use it. */
ok_result ok_base64url_decode(const char *in, size_t n, uint8_t *out, size_t cap, size_t *len);

#endif
