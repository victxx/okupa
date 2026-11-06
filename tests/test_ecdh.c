#include "okupa_ecdh.h"
#include "okupa_keys.h"
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

/* Expected secrets from plain affine curve arithmetic in Python. */
static void check(const char *priv_hex, const char *pub_hex, const char *secret_hex)
{
    uint8_t priv[32], pub[64], secret[32], want[32];
    hex(priv_hex, priv, 32);
    hex(pub_hex, pub, 64);
    hex(secret_hex, want, 32);
    assert(ok_ecdh(priv, pub, secret) == OK_OK && !memcmp(secret, want, 32));
}

int main(void)
{
    uint8_t a[32], b[32], pa[64], pb[64], sa[32], sb[32];
    check("0101010101010101010101010101010101010101010101010101010101010101",
          "4bc2a31265153f07e70e0bab08724e6b85e217f8cd628ceb62974247bb493382ce28cab79ad7119ee1ad3ebcdb98a16805211530ecc6cfefa1b88e6dff99232a",
          "377ce356bf122a355c1e9acc3631d31822c8594f83a79bf6722832bf8ba0069f");
    check("0000000000000000000000000000000000000000000000000000000000000003",
          "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798b7c52588d95c3b9aa25b0403f1eef75702e84bb7597aabe663b82f6f04ef2777",
          "f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9");

    /* Both sides agree. */
    memset(a, 0x11, 32);
    memset(b, 0x22, 32);
    assert(ok_keys_public(a, pa) == OK_OK && ok_keys_public(b, pb) == OK_OK);
    assert(ok_ecdh(a, pb, sa) == OK_OK && ok_ecdh(b, pa, sb) == OK_OK && !memcmp(sa, sb, 32));

    /* A point off the curve and a zero key are refused. */
    pb[63] ^= 1;
    assert(ok_ecdh(a, pb, sa) == OK_CRYPTO);
    memset(a, 0, 32);
    assert(ok_ecdh(a, pa, sa) == OK_CRYPTO);
    assert(ok_ecdh(NULL, pa, sa) == OK_ARGUMENT);
    puts("ecdh: raw x secrets match independent curve arithmetic; bad points refused");
    return 0;
}
