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

/* Reference values from an independent implementation (eth-keys). */
static void check(const char *priv_hex, const char *pub_hex, const char *address)
{
    uint8_t priv[32], pub[64], want[64], addr[20];
    char text[43];
    hex(priv_hex, priv, 32);
    hex(pub_hex, want, 64);
    assert(ok_keys_public(priv, pub) == OK_OK && !memcmp(pub, want, 64));
    ok_keys_address(pub, addr);
    ok_keys_checksum(addr, text);
    assert(!strcmp(text, address));
}

int main(void)
{
    uint8_t priv[32], pub[64], addr[20];
    char text[43];
    check("0101010101010101010101010101010101010101010101010101010101010101",
          "1b84c5567b126440995d3ed5aaba0565d71e1834604819ff9c17f5e9d5dd078f70beaf8f588b541507fed6a642c5ab42dfdf8120a7f639de5122d47a69a8e8d1",
          "0x1a642f0E3c3aF545E7AcBD38b07251B3990914F1");
    check("4646464646464646464646464646464646464646464646464646464646464646",
          "4bc2a31265153f07e70e0bab08724e6b85e217f8cd628ceb62974247bb493382ce28cab79ad7119ee1ad3ebcdb98a16805211530ecc6cfefa1b88e6dff99232a",
          "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F");
    check("fad9c8855b740a0b7ed4c221dbad0f33a83a49cad6b3fe8d5817ac83d38b6a19",
          "9a7df67f79246283fdc93af76d4f8cdd62c4886e8cd870944e817dd0b97934fdd7719d0810951e03418205868a5c1b40b192451367f28e0088dd75e15de40c05",
          "0x96216849c49358B10257cb55b28eA603c874b05E");
    /* EIP-55 examples from the specification. */
    hex("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed", addr, 20);
    ok_keys_checksum(addr, text);
    assert(!strcmp(text, "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"));
    hex("fb6916095ca1df60bb79ce92ce3ea74c37c5d359", addr, 20);
    ok_keys_checksum(addr, text);
    assert(!strcmp(text, "0xfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359"));
    /* Reading an address back: lower case, the checksum, and a typo in it. */
    {
        uint8_t back[20];
        assert(ok_keys_parse_address("0xfb6916095ca1df60bb79ce92ce3ea74c37c5d359", back) == OK_OK && !memcmp(back, addr, 20));
        assert(ok_keys_parse_address("0xfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359\n", back) == OK_OK && !memcmp(back, addr, 20));
        assert(ok_keys_parse_address("0xFB6916095ca1df60bB79Ce92cE3Ea74c37c5d359", back) == OK_FORMAT);
        assert(ok_keys_parse_address("0xfb6916095ca1df60bb79ce92ce3ea74c37c5d35", back) == OK_FORMAT);
        assert(ok_keys_parse_address("fb6916095ca1df60bb79ce92ce3ea74c37c5d359", back) == OK_FORMAT);
    }
    /* Zero and the group order are not valid private keys. */
    memset(priv, 0, 32);
    assert(ok_keys_public(priv, pub) == OK_CRYPTO);
    hex("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141", priv, 32);
    assert(ok_keys_public(priv, pub) == OK_CRYPTO);
    assert(ok_keys_public(NULL, pub) == OK_ARGUMENT);
    puts("keys: public keys and addresses match eth-keys, EIP-55, invalid keys refused");
    return 0;
}
