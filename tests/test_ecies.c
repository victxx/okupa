#include "okupa_ecies.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_rlp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t i, n = strlen(s) / 2;
    assert(n <= cap);
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
    return n;
}

/* EIP-8 test vectors: A's handshake packets to B, B's ack to A. */
static const char *KEY_A = "49a7b37aa6f6645917e7b807e9d1c00d4fa71f18343b0d4122a4d2df64dd6fee";
static const char *KEY_B = "b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291";
static const char *EPH_A = "869d6ecf5211f1cc60418a13b9d870b22959d0c16f02bec714c960dd2298a32d";
static const char *EPH_B = "e238eb8e04fee6511ab04c6dd3c89ce097b11f25d584863ac2b6d5b35b1847e4";
static const char *NONCE_A = "7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6";
static const char *NONCE_B = "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd";
static const char *AUTH1 =
    "048ca79ad18e4b0659fab4853fe5bc58eb83992980f4c9cc147d2aa31532efd29a3d3dc6a3d89eaf"
    "913150cfc777ce0ce4af2758bf4810235f6e6ceccfee1acc6b22c005e9e3a49d6448610a58e98744"
    "ba3ac0399e82692d67c1f58849050b3024e21a52c9d3b01d871ff5f210817912773e610443a9ef14"
    "2e91cdba0bd77b5fdf0769b05671fc35f83d83e4d3b0b000c6b2a1b1bba89e0fc51bf4e460df3105"
    "c444f14be226458940d6061c296350937ffd5e3acaceeaaefd3c6f74be8e23e0f45163cc7ebd7622"
    "0f0128410fd05250273156d548a414444ae2f7dea4dfca2d43c057adb701a715bf59f6fb66b2d1d2"
    "0f2c703f851cbf5ac47396d9ca65b6260bd141ac4d53e2de585a73d1750780db4c9ee4cd4d225173"
    "a4592ee77e2bd94d0be3691f3b406f9bba9b591fc63facc016bfa8";
static const char *AUTH2 =
    "01b304ab7578555167be8154d5cc456f567d5ba302662433674222360f08d5f1534499d3678b513b"
    "0fca474f3a514b18e75683032eb63fccb16c156dc6eb2c0b1593f0d84ac74f6e475f1b8d56116b84"
    "9634a8c458705bf83a626ea0384d4d7341aae591fae42ce6bd5c850bfe0b999a694a49bbbaf3ef6c"
    "da61110601d3b4c02ab6c30437257a6e0117792631a4b47c1d52fc0f8f89caadeb7d02770bf999cc"
    "147d2df3b62e1ffb2c9d8c125a3984865356266bca11ce7d3a688663a51d82defaa8aad69da39ab6"
    "d5470e81ec5f2a7a47fb865ff7cca21516f9299a07b1bc63ba56c7a1a892112841ca44b6e0034dee"
    "70c9adabc15d76a54f443593fafdc3b27af8059703f88928e199cb122362a4b35f62386da7caad09"
    "c001edaeb5f8a06d2b26fb6cb93c52a9fca51853b68193916982358fe1e5369e249875bb8d0d0ec3"
    "6f917bc5e1eafd5896d46bd61ff23f1a863a8a8dcd54c7b109b771c8e61ec9c8908c733c0263440e"
    "2aa067241aaa433f0bb053c7b31a838504b148f570c0ad62837129e547678c5190341e4f1693956c"
    "3bf7678318e2d5b5340c9e488eefea198576344afbdf66db5f51204a6961a63ce072c8926c";
static const char *ACK1 =
    "049f8abcfa9c0dc65b982e98af921bc0ba6e4243169348a236abe9df5f93aa69d99cadddaa387662"
    "b0ff2c08e9006d5a11a278b1b3331e5aaabf0a32f01281b6f4ede0e09a2d5f585b26513cb794d963"
    "5a57563921c04a9090b4f14ee42be1a5461049af4ea7a7f49bf4c97a352d39c8d02ee4acc416388c"
    "1c66cec761d2bc1c72da6ba143477f049c9d2dde846c252c111b904f630ac98e51609b3b1f58168d"
    "dca6505b7196532e5f85b259a20c45e1979491683fee108e9660edbf38f3add489ae73e3dda2c71b"
    "d1497113d5c755e942d1";

/* A packet from an independent Python encoder (pycryptodome, plain curve maths). */
static const char *PY_OUT =
    "04654d1044b69c577a44e5f01a1209523adb4026e70c62d1c13a067acabc09d2667a49821a0ad4b6"
    "34554d330a15a58fe61f8a8e0544b310c6de7b0c8da7528a8da0a1a2a3a4a5a6a7a8a9aaabacadae"
    "af2fd9196d623933537b99b5f711c20a7ca92ee69db8462159f3a231673f0734a590dfe8758b76e3"
    "efd3e6622247611b8ef8cd2a2c0962e57d3b0cb31f2d6f48264b539170";

int main(void)
{
    static uint8_t in[1024], out[1024], want[1024];
    uint8_t ka[32], kb[32], ea[32], eb[32], pub_a[64], pub_b[64], eph[64], nonce[32], h[32], iv[16];
    static const uint8_t auth[2] = {0x00, 0x99};
    static const char msg[] = "okupa says hello, through rlpx ecies";
    ok_rlp_item body, item;
    uint64_t version;
    size_t n, len, pos;

    hex(KEY_A, ka, 32);
    hex(KEY_B, kb, 32);
    hex(EPH_A, ea, 32);
    hex(EPH_B, eb, 32);
    assert(ok_keys_public(ka, pub_a) == OK_OK && ok_keys_public(kb, pub_b) == OK_OK);

    /* Auth v4: sig (65) || keccak(ephemeral pub) || static pub || nonce || 0. */
    n = hex(AUTH1, in, sizeof in);
    assert(ok_ecies_decrypt(kb, in, n, NULL, 0, out, sizeof out, &len) == OK_OK && len == 194);
    assert(!memcmp(out + 97, pub_a, 64));
    assert(ok_keys_public(ea, eph) == OK_OK);
    ok_keccak256(eph, 64, h);
    assert(!memcmp(out + 65, h, 32));
    hex(NONCE_A, nonce, 32);
    assert(!memcmp(out + 161, nonce, 32) && out[193] == 0);

    /* Auth EIP-8: the 2-byte size prefix is authenticated; the body is
     * [sig, static pub, nonce, version] followed by random padding. */
    n = hex(AUTH2, in, sizeof in);
    assert(n == 2 + (size_t)(in[0] << 8 | in[1]));
    assert(ok_ecies_decrypt(kb, in + 2, n - 2, in, 1, out, sizeof out, &len) == OK_CRYPTO);
    assert(ok_ecies_decrypt(kb, in + 2, n - 2, in, 2, out, sizeof out, &len) == OK_OK);
    assert(ok_rlp_item_at(out, len, &body, &n) == OK_OK && body.list && n < len);
    pos = 0;
    assert(ok_rlp_next(&body, &pos, &item) == OK_OK && item.len == 65);
    assert(ok_rlp_next(&body, &pos, &item) == OK_OK && item.len == 64 && !memcmp(item.data, pub_a, 64));
    assert(ok_rlp_next(&body, &pos, &item) == OK_OK && item.len == 32 && !memcmp(item.data, nonce, 32));
    assert(ok_rlp_next(&body, &pos, &item) == OK_OK && ok_rlp_to_uint(&item, &version) == OK_OK && version == 4);
    assert(ok_rlp_next(&body, &pos, &item) == OK_RANGE);

    /* Ack v4 to A: ephemeral pub || nonce || 0. */
    n = hex(ACK1, in, sizeof in);
    assert(ok_ecies_decrypt(ka, in, n, NULL, 0, out, sizeof out, &len) == OK_OK && len == 97);
    assert(ok_keys_public(eb, eph) == OK_OK && !memcmp(out, eph, 64));
    hex(NONCE_B, nonce, 32);
    assert(!memcmp(out + 64, nonce, 32) && out[96] == 0);

    /* Encryption matches the independent encoder byte for byte, and round-trips. */
    for (n = 0; n < 16; n++) iv[n] = (uint8_t)(0xa0 + n);
    n = hex(PY_OUT, want, sizeof want);
    assert(ok_ecies_encrypt(pub_b, ea, iv, (const uint8_t *)msg, strlen(msg), auth, 2, out, sizeof out, &len) == OK_OK);
    assert(len == n && len == strlen(msg) + OK_ECIES_OVERHEAD && !memcmp(out, want, n));
    assert(ok_ecies_decrypt(kb, out, len, auth, 2, in, sizeof in, &n) == OK_OK);
    assert(n == strlen(msg) && !memcmp(in, msg, n));

    /* A flipped bit, the wrong key or the wrong auth data is refused. */
    out[90] ^= 1;
    assert(ok_ecies_decrypt(kb, out, len, auth, 2, in, sizeof in, &n) == OK_CRYPTO);
    out[90] ^= 1;
    assert(ok_ecies_decrypt(ka, out, len, auth, 2, in, sizeof in, &n) == OK_CRYPTO);
    assert(ok_ecies_decrypt(kb, out, len, NULL, 0, in, sizeof in, &n) == OK_CRYPTO);
    assert(ok_ecies_decrypt(kb, out, OK_ECIES_OVERHEAD - 1, NULL, 0, in, sizeof in, &n) == OK_FORMAT);
    assert(ok_ecies_decrypt(kb, out, len, auth, 2, in, 3, &n) == OK_RANGE);
    assert(ok_ecies_encrypt(pub_b, ea, iv, (const uint8_t *)msg, strlen(msg), auth, 2, out, len - 1, &n) == OK_RANGE);
    out[0] = 2;
    assert(ok_ecies_decrypt(kb, out, len, auth, 2, in, sizeof in, &n) == OK_FORMAT);
    puts("ecies: EIP-8 auth/ack vectors decrypt; encryption matches an independent encoder");
    return 0;
}
