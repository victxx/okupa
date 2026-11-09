#define _POSIX_C_SOURCE 200809L /* fileno, fsync */
#include "okupa_seeds.h"
#include "okupa_keccak.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void ticket_check(const uint8_t batch[16], int index, const uint8_t seed[OK_SEED_BYTES], uint8_t out[32])
{
    ok_keccak k;
    uint8_t i = (uint8_t)index;
    ok_keccak_init(&k);
    ok_keccak_update(&k, "okupa seed", 10);
    ok_keccak_update(&k, batch, 16);
    ok_keccak_update(&k, &i, 1);
    ok_keccak_update(&k, seed, OK_SEED_BYTES);
    ok_keccak_final(&k, out);
}

static int zero(const uint8_t *p, size_t n)
{
    uint8_t any = 0;
    while (n--) any |= *p++;
    return !any;
}

ok_result ok_seeds_make(const uint8_t random[OK_SEEDS_RANDOM], uint8_t out[OK_SEEDS_SIZE])
{
    int i;
    if (!random || !out) return OK_ARGUMENT;
    memset(out, 0, OK_SEEDS_SIZE);
    memcpy(out, "OKSEEDS1", 8);
    memcpy(out + 8, random, 16);
    out[24] = OK_SEEDS_COUNT;
    ok_keccak256(out, 32, out + 32);
    for (i = 0; i < OK_SEEDS_COUNT; i++) {
        uint8_t *t = out + OK_SEEDS_HEADER + i * OK_SEEDS_TICKET;
        memcpy(t, random + 16 + i * OK_SEED_BYTES, OK_SEED_BYTES);
        if (zero(t, OK_SEED_BYTES)) return OK_ARGUMENT; /* a broken random source */
        ticket_check(out + 8, i, t, t + OK_SEED_BYTES);
    }
    return OK_OK;
}

ok_result ok_seeds_check(const uint8_t *file, size_t n, int *used)
{
    uint8_t h[32];
    int i, u = 0;
    if (!file || !used) return OK_ARGUMENT;
    if (n != OK_SEEDS_SIZE || memcmp(file, "OKSEEDS1", 8) || file[24] != OK_SEEDS_COUNT || !zero(file + 25, 7))
        return OK_FORMAT;
    ok_keccak256(file, 32, h);
    if (memcmp(h, file + 32, 32)) return OK_FORMAT;
    for (i = 0; i < OK_SEEDS_COUNT; i++) {
        const uint8_t *t = file + OK_SEEDS_HEADER + i * OK_SEEDS_TICKET;
        if (zero(t, OK_SEEDS_TICKET)) {
            if (u != i) return OK_FORMAT; /* used ones come first */
            u++;
            continue;
        }
        ticket_check(file + 8, i, t, h);
        if (memcmp(h, t + OK_SEED_BYTES, 32)) return OK_FORMAT;
    }
    *used = u;
    return OK_OK;
}

int ok_seeds_take(const char *path, uint8_t seed[OK_SEED_BYTES], int *left)
{
    static uint8_t file[OK_SEEDS_SIZE + 1], back[OK_SEEDS_SIZE + 1];
    static const uint8_t blank[OK_SEEDS_TICKET];
    uint8_t mine[OK_SEED_BYTES];
    size_t n;
    long at;
    int used, ok;
    FILE *f;
    if (!path || !seed || !left) return -1;
    if (!(f = fopen(path, "rb"))) return 0;
    n = fread(file, 1, sizeof file, f);
    fclose(f);
    if (ok_seeds_check(file, n, &used) != OK_OK) return -1;
    if (used == OK_SEEDS_COUNT) return 0;
    at = OK_SEEDS_HEADER + (long)used * OK_SEEDS_TICKET;
    memcpy(mine, file + at, OK_SEED_BYTES);
    memset(file + at, 0, OK_SEEDS_TICKET); /* what the SD must hold afterwards */
    /* Gone from the SD first, synced, and read back. */
    ok = (f = fopen(path, "r+b")) != NULL;
    if (ok) {
        ok = fseek(f, at, SEEK_SET) == 0 && fwrite(blank, 1, sizeof blank, f) == sizeof blank && fflush(f) == 0 &&
             fsync(fileno(f)) == 0;
        ok = fclose(f) == 0 && ok;
    }
    if (ok && (ok = (f = fopen(path, "rb")) != NULL)) {
        n = fread(back, 1, sizeof back, f);
        fclose(f);
        ok = n == OK_SEEDS_SIZE && !memcmp(back, file, OK_SEEDS_SIZE);
    }
    ok_wipe(file, sizeof file);
    ok_wipe(back, sizeof back);
    if (!ok) {
        ok_wipe(mine, sizeof mine);
        return -1;
    }
    memcpy(seed, mine, OK_SEED_BYTES); /* only now, once it can never be taken again */
    ok_wipe(mine, sizeof mine);
    *left = OK_SEEDS_COUNT - used - 1;
    return 1;
}
