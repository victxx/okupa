#include "okupa_aes.h"
#include "okupa_sha256.h"
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

static void block(size_t key_len, const char *want_hex)
{
    uint8_t key[32], in[16], out[16], want[16];
    ok_aes aes;
    size_t i;
    for (i = 0; i < key_len; i++) key[i] = (uint8_t)i;
    hex("00112233445566778899aabbccddeeff", in, 16);
    hex(want_hex, want, 16);
    assert(ok_aes_init(&aes, key, key_len) == OK_OK);
    ok_aes_block(&aes, in, out);
    assert(!memcmp(out, want, 16));
}

/* CTR in pieces of every size must match the one-shot stream. */
static void ctr(const uint8_t *key, size_t key_len, const char *iv_hex, const uint8_t *in, size_t n, const char *want_hex)
{
    static uint8_t buf[64], want[64];
    uint8_t iv[16];
    ok_aes_ctr c;
    size_t step, at;
    hex(iv_hex, iv, 16);
    hex(want_hex, want, n);
    for (step = 1; step <= n; step++) {
        memcpy(buf, in, n);
        assert(ok_aes_ctr_init(&c, key, key_len, iv) == OK_OK);
        for (at = 0; at < n; at += step) ok_aes_ctr_run(&c, buf + at, at + step <= n ? step : n - at);
        assert(!memcmp(buf, want, n));
    }
}

static void mac(const void *key, size_t key_len, const char *msg, const char *want_hex)
{
    uint8_t out[32], want[32];
    ok_hmac h;
    hex(want_hex, want, 32);
    ok_hmac_sha256(key, key_len, msg, strlen(msg), out);
    assert(!memcmp(out, want, 32));
    ok_hmac_init(&h, key, key_len);
    ok_hmac_update(&h, msg, 3);
    ok_hmac_update(&h, msg + 3, strlen(msg) - 3);
    ok_hmac_final(&h, out);
    assert(!memcmp(out, want, 32));
}

int main(void)
{
    uint8_t key[32], pt[64], zero[64] = {0}, k0b[20], kaa[131];
    ok_aes aes;
    size_t i;

    /* FIPS-197 appendix C. */
    block(16, "69c4e0d86a7b0430d8cdb78070b4c55a");
    block(32, "8ea2b7ca516745bfeafc49904b496089");
    assert(ok_aes_init(&aes, key, 24) == OK_RANGE);

    /* SP 800-38A F.5.1, then counters that carry past 32 and 128 bits (pycryptodome). */
    hex("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", pt, 64);
    ctr(key, 16, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", pt, 64,
        "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee");
    ctr(key, 16, "000102030405060708090a0bfffffffe", zero, 64,
        "08ff81431e8af8811d931e7bef271fc4bdb7c0ef49717942fc68eeb17692fcf4eef89e9494c1082ab27d4d9095feff60e4c55e024df3f265e436ab9720921bb4");
    for (i = 0; i < 32; i++) key[i] = (uint8_t)i;
    ctr(key, 32, "ffffffffffffffffffffffffffffffff", zero, 40,
        "e999e41d4ca770da5387117b5d8f57eef29000b62a499fd0a9f39a6add2e7780f05d76ae4ab99fe5");

    /* RFC 4231 cases 1, 2 and 6. */
    memset(k0b, 0x0b, sizeof k0b);
    memset(kaa, 0xaa, sizeof kaa);
    mac(k0b, sizeof k0b, "Hi There", "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    mac("Jefe", 4, "what do ya want for nothing?", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    mac(kaa, sizeof kaa, "Test Using Larger Than Block-Size Key - Hash Key First",
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    puts("aes: FIPS-197 blocks, SP 800-38A CTR, 128-bit counter carry; hmac: RFC 4231");
    return 0;
}
