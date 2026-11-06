#include "okupa_keys.h"
#include "okupa_keccak.h"
#include "secp256k1.h"
#include "secp256k1_preallocated.h"
#include <string.h>

/* Signing needs a built context; the static one only verifies. It lives in a
 * fixed buffer (no heap) and is built once; the core has a single owner. */
static secp256k1_context *context(void)
{
    static union { unsigned char b[1024]; long double align; } mem;
    static secp256k1_context *ctx;
    if (!ctx && secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE) <= sizeof mem.b)
        ctx = secp256k1_context_preallocated_create(mem.b, SECP256K1_CONTEXT_NONE);
    return ctx;
}

const secp256k1_context *ok_secp(void) { return context(); }

ok_result ok_keys_public(const uint8_t priv[32], uint8_t pub[64])
{
    secp256k1_pubkey key;
    uint8_t out[65];
    size_t n = sizeof out;
    const secp256k1_context *ctx = context();
    if (!priv || !pub) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    if (!secp256k1_ec_seckey_verify(ctx, priv)) return OK_CRYPTO;
    if (!secp256k1_ec_pubkey_create(ctx, &key, priv)) return OK_CRYPTO;
    secp256k1_ec_pubkey_serialize(ctx, out, &n, &key, SECP256K1_EC_UNCOMPRESSED);
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

ok_result ok_keys_parse_address(const char *text, uint8_t addr[20])
{
    char sum[43];
    int i, lower = 0, upper = 0;
    if (!text || !addr || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) return OK_FORMAT;
    for (i = 0; i < 40; i++) {
        char c = text[2 + i];
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10, lower = 1;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10, upper = 1;
        else return OK_FORMAT;
        if (i % 2) addr[i / 2] = (uint8_t)(addr[i / 2] | v);
        else addr[i / 2] = (uint8_t)(v << 4);
    }
    if (text[42] && text[42] != ' ' && text[42] != '\r' && text[42] != '\n' && text[42] != '\t') return OK_FORMAT;
    if (lower && upper) {
        ok_keys_checksum(addr, sum);
        if (memcmp(sum + 2, text + 2, 40)) return OK_FORMAT;
    }
    return OK_OK;
}
