#include "okupa_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const void *msg, size_t n, const char *want)
{
    uint8_t got[32];
    size_t i;
    ok_sha256(msg, n, got);
    for (i = 0; i < 32; i++) {
        unsigned v;
        assert(sscanf(want + 2 * i, "%2x", &v) == 1 && got[i] == v);
    }
}

int main(void)
{
    static uint8_t million[1000000];
    static const char two_blocks[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    /* FIPS 180-2 / NIST example vectors. */
    check("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(two_blocks, strlen(two_blocks), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    memset(million, 'a', sizeof million);
    check(million, sizeof million, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    puts("sha256: NIST vectors (empty, abc, 448 bits, a million a)");
    return 0;
}
