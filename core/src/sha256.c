#include "okupa_sha256.h"
#include "bearssl_hash.h"

void ok_sha256(const void *data, size_t n, uint8_t out[32])
{
    br_sha256_context c;
    br_sha256_init(&c);
    br_sha256_update(&c, data, n);
    br_sha256_out(&c, out);
}
