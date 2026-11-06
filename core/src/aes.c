#include "okupa_aes.h"
#include <string.h>

ok_result ok_aes_init(ok_aes *aes, const uint8_t *key, size_t key_len)
{
    if (!aes || !key) return OK_ARGUMENT;
    if (key_len != 16 && key_len != 32) return OK_RANGE;
    br_aes_ct_cbcenc_init(&aes->keys, key, key_len);
    return OK_OK;
}

void ok_aes_block(const ok_aes *aes, const uint8_t in[16], uint8_t out[16])
{
    /* CBC over a single block with a zero IV is the bare block cipher. */
    uint8_t iv[16] = {0};
    memcpy(out, in, 16);
    br_aes_ct_cbcenc_run(&aes->keys, iv, out, 16);
}

ok_result ok_aes_ctr_init(ok_aes_ctr *ctr, const uint8_t *key, size_t key_len, const uint8_t iv[16])
{
    ok_result r;
    if (!ctr || !iv) return OK_ARGUMENT;
    r = ok_aes_init(&ctr->aes, key, key_len);
    if (r != OK_OK) return r;
    memcpy(ctr->counter, iv, 16);
    ctr->used = 16;
    return OK_OK;
}

void ok_aes_ctr_run(ok_aes_ctr *ctr, uint8_t *data, size_t n)
{
    size_t i;
    int j;
    for (i = 0; i < n; i++) {
        if (ctr->used == 16) {
            ok_aes_block(&ctr->aes, ctr->counter, ctr->stream);
            for (j = 15; j >= 0 && ++ctr->counter[j] == 0; j--) {}
            ctr->used = 0;
        }
        data[i] ^= ctr->stream[ctr->used++];
    }
}
