#include "okupa_ecdh.h"
#include "okupa_keys.h"
#include "secp256k1.h"
#include "secp256k1_ecdh.h"
#include <string.h>

/* libsecp256k1 hashes the shared point by default; devp2p wants x as is. */
static int copy_x(unsigned char *out, const unsigned char *x, const unsigned char *y, void *data)
{
    (void)y;
    (void)data;
    memcpy(out, x, 32);
    return 1;
}

ok_result ok_ecdh(const uint8_t priv[32], const uint8_t pub[64], uint8_t secret[32])
{
    secp256k1_pubkey key;
    uint8_t in[65];
    const secp256k1_context *ctx = ok_secp();
    if (!priv || !pub || !secret) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    in[0] = 4;
    memcpy(in + 1, pub, 64);
    if (!secp256k1_ec_pubkey_parse(ctx, &key, in, sizeof in)) return OK_CRYPTO;
    if (!secp256k1_ecdh(ctx, secret, &key, priv, copy_x, NULL)) return OK_CRYPTO;
    return OK_OK;
}
