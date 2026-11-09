#define _POSIX_C_SOURCE 200809L
#include "okupa_seeds.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PATH "build/test-seeds.bin"

static void write_all(const uint8_t *p, size_t n)
{
    FILE *f = fopen(PATH, "wb");
    assert(f && fwrite(p, 1, n, f) == n && !fclose(f));
}

int main(void)
{
    static uint8_t random[OK_SEEDS_RANDOM], file[OK_SEEDS_SIZE], copy[OK_SEEDS_SIZE];
    uint8_t seed[OK_SEED_BYTES], first[OK_SEED_BYTES];
    int used, left, i, k;
    FILE *f;

    for (i = 0; i < OK_SEEDS_RANDOM; i++) random[i] = (uint8_t)(i * 37 + 11);
    assert(ok_seeds_make(random, file) == OK_OK && !memcmp(file, "OKSEEDS1", 8));
    assert(ok_seeds_check(file, sizeof file, &used) == OK_OK && used == 0);
    /* Damage anywhere is found; a short file too. */
    for (i = 0; i < OK_SEEDS_SIZE * 8; i += 97) {
        file[i / 8] ^= (uint8_t)(1u << (i % 8));
        assert(ok_seeds_check(file, sizeof file, &used) == OK_FORMAT);
        file[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
    assert(ok_seeds_check(file, sizeof file - 1, &used) == OK_FORMAT);

    /* Taken in order, each one zeroed on the file before it is handed out. */
    mkdir("build", 0700);
    write_all(file, sizeof file);
    assert(ok_seeds_take(PATH, first, &left) == 1 && left == OK_SEEDS_COUNT - 1);
    assert(!memcmp(first, random + 16, OK_SEED_BYTES));
    assert((f = fopen(PATH, "rb")) && fread(copy, 1, sizeof copy, f) == sizeof copy && !fclose(f));
    assert(ok_seeds_check(copy, sizeof copy, &used) == OK_OK && used == 1);
    for (k = 0; k < 80; k++) assert(copy[OK_SEEDS_HEADER + k] == 0);
    assert(ok_seeds_take(PATH, seed, &left) == 1 && memcmp(seed, first, sizeof seed));
    assert(!memcmp(seed, random + 16 + OK_SEED_BYTES, OK_SEED_BYTES));
    for (i = 2; i < OK_SEEDS_COUNT; i++) assert(ok_seeds_take(PATH, seed, &left) == 1);
    assert(left == 0 && ok_seeds_take(PATH, seed, &left) == 0); /* all used: none, never one again */

    /* A used ticket that comes back (an old copy put back over part of it) breaks the order: refused. */
    write_all(file, sizeof file);
    assert(ok_seeds_take(PATH, seed, &left) == 1);
    memset(file + OK_SEEDS_HEADER + 3 * OK_SEEDS_TICKET, 0, OK_SEEDS_TICKET); /* a hole after unused ones */
    write_all(file, sizeof file);
    assert(ok_seeds_take(PATH, seed, &left) == -1);
    remove(PATH);
    assert(ok_seeds_take(PATH, seed, &left) == 0); /* no file: none */

    /* A broken random source (a zero seed) makes no file. */
    memset(random + 16, 0, OK_SEED_BYTES);
    assert(ok_seeds_make(random, file) == OK_ARGUMENT);
    puts("seeds: one-use seeds, zeroed and synced before use, in order; damage refused");
    return 0;
}
