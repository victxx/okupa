#include "okupa_keys.h"
#include "okupa_keccak.h"
#include "secp256k1.h"
#include <string.h>

ok_result ok_keys_public(const uint8_t priv[32], uint8_t pub[64])
{
    secp256k1_pubkey key;
    uint8_t out[65];
    size_t n = sizeof out;
    if (!priv || !pub) return OK_ARGUMENT;
    if (!secp256k1_ec_seckey_verify(secp256k1_context_static, priv)) return OK_CRYPTO;
    if (!secp256k1_ec_pubkey_create(secp256k1_context_static, &key, priv)) return OK_CRYPTO;
    secp256k1_ec_pubkey_serialize(secp256k1_context_static, out, &n, &key, SECP256K1_EC_UNCOMPRESSED);
    memcpy(pub, out + 1, 64);
    return OK_OK;
}

void ok_keys_address(const uint8_t pub[64], uint8_t addr[20])
{
    uint8_t h[32];
    ok_keccak256(pub, 64, h);
    memcpy(addr, h + 12, 20);
}

void ok_keys_checksum(const uint8_t addr[20], char out[43])
{
    static const char digits[] = "0123456789abcdef";
    char lower[40];
    uint8_t h[32];
    int i;
    for (i = 0; i < 20; i++) {
        lower[2 * i] = digits[addr[i] >> 4];
        lower[2 * i + 1] = digits[addr[i] & 15];
    }
    ok_keccak256(lower, sizeof lower, h);
    out[0] = '0';
    out[1] = 'x';
    for (i = 0; i < 40; i++) {
        int nibble = (h[i / 2] >> (i % 2 ? 0 : 4)) & 15;
        char c = lower[i];
        out[2 + i] = (c >= 'a' && nibble >= 8) ? (char)(c - 'a' + 'A') : c;
    }
    out[42] = 0;
}
