#ifndef OKUPA_SEEDS_H
#define OKUPA_SEEDS_H

#include "okupa.h"

/* Entropy the DSi cannot make itself, brought in from a computer: a file of
 * one-use seeds (sd:/okupa/seeds.bin, made by build/okupa-seeds from the
 * computer's own random source).
 *
 * "OKSEEDS1" | batch (16) | count | 7 zero bytes | keccak of those 32 bytes,
 * then count tickets of 48 seed bytes and a keccak check of (batch, index,
 * seed). A used ticket is all zeros, and the used ones come first.
 *
 * Taking one: the ticket is zeroed on the SD, synced and read back as zeros
 * before its seed is handed out, so a seed is never used twice even if the
 * power goes right after. The seed is mixed into the console's own entropy,
 * never used alone. The checks find damage, not tampering, and a copied or
 * restored file cannot be told apart: keep it on this one SD card. */
#define OK_SEEDS_COUNT 64
#define OK_SEED_BYTES 48
#define OK_SEEDS_HEADER 64
#define OK_SEEDS_TICKET 80
#define OK_SEEDS_SIZE (OK_SEEDS_HEADER + OK_SEEDS_COUNT * OK_SEEDS_TICKET)
#define OK_SEEDS_RANDOM (16 + OK_SEEDS_COUNT * OK_SEED_BYTES)

/* A new file from OK_SEEDS_RANDOM bytes of a good random source. */
ok_result ok_seeds_make(const uint8_t random[OK_SEEDS_RANDOM], uint8_t out[OK_SEEDS_SIZE]);
/* OK_OK if the file is whole; *used: the tickets already taken. */
ok_result ok_seeds_check(const uint8_t *file, size_t n, int *used);
/* 1 a seed (and *left after it), 0 none left or no file, -1 damaged or the
 * SD would not keep the change (no seed is handed out then). */
int ok_seeds_take(const char *path, uint8_t seed[OK_SEED_BYTES], int *left);

#endif
