#ifndef OKUPA_AES_H
#define OKUPA_AES_H

#include "okupa.h"
#include "bearssl_block.h"

/* AES from BearSSL's constant-time "ct" code. One block at a time is all RLPx
 * needs for its MAC; CTR here carries the full 128-bit counter, as Go and the
 * other devp2p clients do (BearSSL's own CTR only counts in 32 bits). */
typedef struct {
    br_aes_ct_cbcenc_keys keys;
} ok_aes;

typedef struct {
    ok_aes aes;
    uint8_t counter[16];
    uint8_t stream[16];
    unsigned used; /* bytes of stream already spent, 16 = none left */
} ok_aes_ctr;

/* key_len is 16 or 32 (AES-128 for ECIES, AES-256 for RLPx frames). */
ok_result ok_aes_init(ok_aes *aes, const uint8_t *key, size_t key_len);
void ok_aes_block(const ok_aes *aes, const uint8_t in[16], uint8_t out[16]);

ok_result ok_aes_ctr_init(ok_aes_ctr *ctr, const uint8_t *key, size_t key_len, const uint8_t iv[16]);
/* XORs the key stream into data, in place; calls may split the stream anywhere. */
void ok_aes_ctr_run(ok_aes_ctr *ctr, uint8_t *data, size_t n);

#endif
