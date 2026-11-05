#ifndef OKUPA_KECCAK_H
#define OKUPA_KECCAK_H

#include <stddef.h>
#include <stdint.h>

/* Ethereum's Keccak-256: the original Keccak padding (0x01), not SHA3-256 (0x06). */
#define OK_KECCAK_RATE 136

typedef struct {
    union { uint8_t b[200]; uint64_t q[25]; } st;
    size_t pt;
} ok_keccak;

void ok_keccak_init(ok_keccak *k);
void ok_keccak_update(ok_keccak *k, const void *data, size_t n);
/* Writes the digest and wipes the state. */
void ok_keccak_final(ok_keccak *k, uint8_t out[32]);
void ok_keccak256(const void *data, size_t n, uint8_t out[32]);

#endif
