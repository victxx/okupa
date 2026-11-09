/* Makes a file of one-use seeds for the console (okupa_seeds.h) from this
 * computer's random source, and checks one.
 *   build/okupa-seeds <out file>       a new file (refuses to overwrite one)
 *   build/okupa-seeds --check <file>   whole or not, and how many are left
 * Copy it to sd:/okupa/seeds.bin with the DSi off. Never keep a copy: a seed
 * put back on the card would be used twice. */
#include "okupa_seeds.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    static uint8_t random[OK_SEEDS_RANDOM], file[OK_SEEDS_SIZE + 1];
    FILE *f;
    size_t n;
    int used;
    if (argc == 3 && !strcmp(argv[1], "--check")) {
        if (!(f = fopen(argv[2], "rb"))) return perror(argv[2]), 2;
        n = fread(file, 1, sizeof file, f);
        fclose(f);
        if (ok_seeds_check(file, n, &used) != OK_OK) return fprintf(stderr, "okupa-seeds: damaged\n"), 1;
        printf("okupa-seeds: whole, %d of %d left\n", OK_SEEDS_COUNT - used, OK_SEEDS_COUNT);
        return 0;
    }
    if (argc != 2) return fprintf(stderr, "usage: okupa-seeds <out file> | --check <file>\n"), 2;
    if ((f = fopen(argv[1], "rb"))) {
        fclose(f);
        return fprintf(stderr, "okupa-seeds: %s exists; not overwriting it\n", argv[1]), 1;
    }
    if (!(f = fopen("/dev/urandom", "rb")) || fread(random, 1, sizeof random, f) != sizeof random)
        return fprintf(stderr, "okupa-seeds: no random source\n"), 1;
    fclose(f);
    if (ok_seeds_make(random, file) != OK_OK) return fprintf(stderr, "okupa-seeds: bad random bytes\n"), 1;
    ok_wipe(random, sizeof random);
    if (!(f = fopen(argv[1], "wb")) || fwrite(file, 1, OK_SEEDS_SIZE, f) != OK_SEEDS_SIZE || fclose(f))
        return fprintf(stderr, "okupa-seeds: could not write %s\n", argv[1]), 1;
    ok_wipe(file, sizeof file);
    printf("okupa-seeds: %d seeds in %s\n", OK_SEEDS_COUNT, argv[1]);
    return 0;
}
