#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "secp256k1.h"
#include "secp256k1_recovery.h"

/* The pinned library links and agrees with a known fact: private key 1 is G. */
int main(void)
{
    static const unsigned char one[32] = {[31] = 1};
    static const unsigned char gx[32] = {
        0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac, 0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b, 0x07,
        0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8, 0x17, 0x98};
    unsigned char out[65], msg[32] = {7};
    size_t n = sizeof out;
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    secp256k1_pubkey pub, back;
    secp256k1_ecdsa_recoverable_signature sig;
    assert(secp256k1_ec_pubkey_create(ctx, &pub, one));
    assert(secp256k1_ec_pubkey_serialize(ctx, out, &n, &pub, SECP256K1_EC_UNCOMPRESSED) && n == 65);
    assert(out[0] == 4 && !memcmp(out + 1, gx, 32));
    assert(secp256k1_ecdsa_sign_recoverable(ctx, &sig, msg, one, NULL, NULL));
    assert(secp256k1_ecdsa_recover(ctx, &back, &sig, msg) && !memcmp(&back, &pub, sizeof pub));
    secp256k1_context_destroy(ctx);
    puts("secp: pinned v0.8.0 links, key 1 is G, sign/recover round trip");
    return 0;
}
