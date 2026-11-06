#ifndef OKUPA_SHA256_H
#define OKUPA_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 from BearSSL; needed to check 0G's TEE signatures and for RLPx. */
void ok_sha256(const void *data, size_t n, uint8_t out[32]);

/* HMAC-SHA256 (BearSSL), one shot or in parts (ECIES MACs three pieces). */
#include "bearssl_hmac.h"
typedef struct {
    br_hmac_key_context key;
    br_hmac_context mac;
} ok_hmac;

void ok_hmac_init(ok_hmac *h, const void *key, size_t key_len);
void ok_hmac_update(ok_hmac *h, const void *data, size_t n);
void ok_hmac_final(ok_hmac *h, uint8_t out[32]);
void ok_hmac_sha256(const void *key, size_t key_len, const void *data, size_t n, uint8_t out[32]);

#endif
