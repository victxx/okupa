#include "okupa_sha256.h"
#include "bearssl_hash.h"

void ok_sha256(const void *data, size_t n, uint8_t out[32])
{
    br_sha256_context c;
    br_sha256_init(&c);
    br_sha256_update(&c, data, n);
    br_sha256_out(&c, out);
}

void ok_hmac_init(ok_hmac *h, const void *key, size_t key_len)
{
    br_hmac_key_init(&h->key, &br_sha256_vtable, key, key_len);
    br_hmac_init(&h->mac, &h->key, 0);
}

void ok_hmac_update(ok_hmac *h, const void *data, size_t n)
{
    br_hmac_update(&h->mac, data, n);
}

void ok_hmac_final(ok_hmac *h, uint8_t out[32])
{
    br_hmac_out(&h->mac, out);
}

void ok_hmac_sha256(const void *key, size_t key_len, const void *data, size_t n, uint8_t out[32])
{
    ok_hmac h;
    ok_hmac_init(&h, key, key_len);
    ok_hmac_update(&h, data, n);
    ok_hmac_final(&h, out);
}
