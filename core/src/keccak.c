#include "okupa_keccak.h"
#include "okupa.h"
#include "sha3.h"
#include <string.h>

/* Byte-wise sponge over tiny_sha3's permutation, which handles byte order itself. */
void ok_keccak_init(ok_keccak *k)
{
    memset(k, 0, sizeof *k);
}

void ok_keccak_update(ok_keccak *k, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    while (n--) {
        k->st.b[k->pt++] ^= *p++;
        if (k->pt == OK_KECCAK_RATE) {
            sha3_keccakf(k->st.q);
            k->pt = 0;
        }
    }
}

void ok_keccak_final(ok_keccak *k, uint8_t out[32])
{
    k->st.b[k->pt] ^= 0x01;
    k->st.b[OK_KECCAK_RATE - 1] ^= 0x80;
    sha3_keccakf(k->st.q);
    memcpy(out, k->st.b, 32);
    ok_wipe(k, sizeof *k);
}

void ok_keccak256(const void *data, size_t n, uint8_t out[32])
{
    ok_keccak k;
    ok_keccak_init(&k);
    ok_keccak_update(&k, data, n);
    ok_keccak_final(&k, out);
}
