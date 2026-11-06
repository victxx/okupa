#include "okupa_ecies.h"
#include "okupa_aes.h"
#include "okupa_ecdh.h"
#include "okupa_keys.h"
#include "okupa_sha256.h"
#include <string.h>

/* kE (16) || sha256(kM) (32), from KDF(S, 32) = sha256(00000001 || S). */
static ok_result derive(const uint8_t priv[32], const uint8_t pub[64], uint8_t ke[16], uint8_t km[32])
{
    uint8_t buf[36] = {0, 0, 0, 1}, k[32];
    ok_result r = ok_ecdh(priv, pub, buf + 4);
    if (r != OK_OK) return r;
    ok_sha256(buf, sizeof buf, k);
    memcpy(ke, k, 16);
    ok_sha256(k + 16, 16, km);
    ok_wipe(buf, sizeof buf);
    ok_wipe(k, sizeof k);
    return OK_OK;
}

static void tag(const uint8_t km[32], const uint8_t *iv_c, size_t n, const uint8_t *auth, size_t auth_len, uint8_t out[32])
{
    ok_hmac h;
    ok_hmac_init(&h, km, 32);
    ok_hmac_update(&h, iv_c, n);
    if (auth_len) ok_hmac_update(&h, auth, auth_len);
    ok_hmac_final(&h, out);
    ok_wipe(&h, sizeof h);
}

static ok_result crypt(const uint8_t ke[16], const uint8_t iv[16], uint8_t *data, size_t n)
{
    ok_aes_ctr c;
    ok_result r = ok_aes_ctr_init(&c, ke, 16, iv);
    if (r == OK_OK) ok_aes_ctr_run(&c, data, n);
    ok_wipe(&c, sizeof c);
    return r;
}

ok_result ok_ecies_encrypt(const uint8_t pub[64], const uint8_t eph_priv[32], const uint8_t iv[16],
                           const uint8_t *msg, size_t n, const uint8_t *auth, size_t auth_len,
                           uint8_t *out, size_t cap, size_t *len)
{
    uint8_t ke[16], km[32];
    ok_result r;
    if (!pub || !eph_priv || !iv || (!msg && n) || (!auth && auth_len) || !out || !len) return OK_ARGUMENT;
    if (n > SIZE_MAX - OK_ECIES_OVERHEAD || cap < n + OK_ECIES_OVERHEAD) return OK_RANGE;
    out[0] = 4;
    if ((r = ok_keys_public(eph_priv, out + 1)) != OK_OK) return r;
    if ((r = derive(eph_priv, pub, ke, km)) != OK_OK) return r;
    memcpy(out + 65, iv, 16);
    if (n) memmove(out + 81, msg, n);
    r = crypt(ke, iv, out + 81, n);
    if (r == OK_OK) {
        tag(km, out + 65, 16 + n, auth, auth_len, out + 81 + n);
        *len = n + OK_ECIES_OVERHEAD;
    }
    ok_wipe(ke, sizeof ke);
    ok_wipe(km, sizeof km);
    return r;
}

ok_result ok_ecies_decrypt(const uint8_t priv[32], const uint8_t *in, size_t n,
                           const uint8_t *auth, size_t auth_len, uint8_t *out, size_t cap, size_t *len)
{
    uint8_t ke[16], km[32], want[32], diff = 0;
    size_t m, i;
    ok_result r;
    if (!priv || !in || (!auth && auth_len) || !out || !len) return OK_ARGUMENT;
    if (n < OK_ECIES_OVERHEAD || in[0] != 4) return OK_FORMAT;
    m = n - OK_ECIES_OVERHEAD;
    if (cap < m) return OK_RANGE;
    if ((r = derive(priv, in + 1, ke, km)) != OK_OK) return r;
    tag(km, in + 65, 16 + m, auth, auth_len, want);
    for (i = 0; i < 32; i++) diff |= (uint8_t)(want[i] ^ in[81 + m + i]);
    if (diff) {
        r = OK_CRYPTO;
    } else {
        if (m) memmove(out, in + 81, m);
        r = crypt(ke, in + 65, out, m);
        if (r == OK_OK) *len = m;
    }
    ok_wipe(ke, sizeof ke);
    ok_wipe(km, sizeof km);
    return r;
}
