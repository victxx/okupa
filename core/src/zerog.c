#include "okupa_zerog.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_sha256.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"
#include <stdio.h>
#include <string.h>

static void hex(const uint8_t *in, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
}

void ok_zerog_text(const uint8_t *req, size_t req_len, const uint8_t *resp, size_t resp_len, char text[OK_ZEROG_TEXT + 1])
{
    uint8_t h[32];
    ok_sha256(req, req_len, h);
    hex(h, 32, text);
    text[64] = ':';
    ok_sha256(resp, resp_len, h);
    hex(h, 32, text + 65);
    text[OK_ZEROG_TEXT] = 0;
}

ok_result ok_personal_recover(const void *msg, size_t n, const uint8_t sig[65], uint8_t address[20])
{
    const secp256k1_context *ctx = ok_secp();
    secp256k1_ecdsa_recoverable_signature s;
    secp256k1_pubkey key;
    uint8_t digest[32], pub[65];
    char prefix[48];
    size_t pub_len = sizeof pub;
    int v, k;
    ok_keccak h;
    if (!msg || !sig || !address) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    v = sig[64] >= 27 ? sig[64] - 27 : sig[64];
    if (v < 0 || v > 1) return OK_CRYPTO;
    k = snprintf(prefix, sizeof prefix, "\x19" "Ethereum Signed Message:\n%lu", (unsigned long)n);
    ok_keccak_init(&h);
    ok_keccak_update(&h, prefix, (size_t)k);
    ok_keccak_update(&h, msg, n);
    ok_keccak_final(&h, digest);
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &s, sig, v) ||
        !secp256k1_ecdsa_recover(ctx, &key, &s, digest))
        return OK_CRYPTO;
    secp256k1_ec_pubkey_serialize(ctx, pub, &pub_len, &key, SECP256K1_EC_UNCOMPRESSED);
    ok_keys_address(pub + 1, address);
    return OK_OK;
}

static int unhex(const char *s, size_t n, uint8_t *out)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int b = 0, j;
        for (j = 0; j < 2; j++) {
            char c = s[2 * i + j];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return 0;
            b = b << 4 | d;
        }
        out[i] = (uint8_t)b;
    }
    return 1;
}

ok_result ok_zerog_verify(const uint8_t *req, size_t req_len, const uint8_t *resp, size_t resp_len,
                          const char *text, size_t text_len, const char *sig_hex, const uint8_t signer[20])
{
    char want[OK_ZEROG_TEXT + 1];
    uint8_t sig[65], who[20];
    size_t n;
    ok_result r;
    if (!req || !resp || !text || !sig_hex || !signer) return OK_ARGUMENT;
    ok_zerog_text(req, req_len, resp, resp_len, want);
    if (text_len != OK_ZEROG_TEXT || memcmp(text, want, OK_ZEROG_TEXT)) return OK_FORMAT; /* not our exchange */
    if (sig_hex[0] == '0' && (sig_hex[1] == 'x' || sig_hex[1] == 'X')) sig_hex += 2;
    n = strlen(sig_hex);
    if (n != 130 || !unhex(sig_hex, 65, sig)) return OK_FORMAT;
    if ((r = ok_personal_recover(text, text_len, sig, who)) != OK_OK) return r;
    return memcmp(who, signer, 20) ? OK_CRYPTO : OK_OK;
}
