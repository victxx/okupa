#include "okupa_keccak.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
}

static void check(const uint8_t *msg, size_t n, const char *want)
{
    uint8_t got[32], expect[32];
    ok_keccak k;
    size_t i;
    hex(want, expect, 32);
    ok_keccak256(msg, n, got);
    assert(!memcmp(got, expect, 32));
    /* Same digest when fed one byte at a time, across every rate boundary. */
    ok_keccak_init(&k);
    for (i = 0; i < n; i++) ok_keccak_update(&k, msg + i, 1);
    ok_keccak_final(&k, got);
    assert(!memcmp(got, expect, 32));
    for (i = 0; i < sizeof k; i++) assert(((uint8_t *)&k)[i] == 0);
}

int main(void)
{
    static uint8_t a3[200], ramp[512];
    size_t i;
    memset(a3, 0xa3, sizeof a3);
    for (i = 0; i < sizeof ramp; i++) ramp[i] = (uint8_t)i;
    /* Reference values from an independent implementation (pycryptodome). */
    check((const uint8_t *)"", 0, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    check((const uint8_t *)"abc", 3, "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
    check(a3, sizeof a3, "3a57666b048777f2c953dc4456f45a2588e1cb6f2da760122d530ac2ce607d4a");
    check(ramp, sizeof ramp, "f55ba327291604f0e5be6651752398b7be2331aad65f5763ce067df95cc13be1");
    puts("keccak: empty, abc, 200 B, 512 B, byte-at-a-time, state wiped");
    return 0;
}
