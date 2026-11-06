#ifndef OKUPA_SHA256_H
#define OKUPA_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 from BearSSL; needed to check 0G's TEE signatures and for RLPx. */
void ok_sha256(const void *data, size_t n, uint8_t out[32]);

#endif
