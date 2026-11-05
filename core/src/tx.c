#include "okupa_tx.h"
#include "okupa_keccak.h"
#include "okupa_rlp.h"

/* The nine fields shared by the unsigned and the signed forms. */
static void fields(ok_rlp_writer *w, const ok_tx *tx)
{
    size_t access;
    ok_rlp_uint(w, tx->chain_id);
    ok_rlp_uint(w, tx->nonce);
    ok_rlp_uint(w, tx->max_priority_fee);
    ok_rlp_uint(w, tx->max_fee);
    ok_rlp_uint(w, tx->gas_limit);
    ok_rlp_bytes(w, tx->to, 20);
    ok_rlp_uint(w, tx->value);
    ok_rlp_bytes(w, tx->data, tx->data_len);
    access = ok_rlp_list_begin(w);
    ok_rlp_list_end(w, access);
}

ok_result ok_tx_unsigned(const ok_tx *tx, uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t list, n;
    if (!tx || !out || !len || cap < 1 || (tx->data_len && !tx->data)) return OK_ARGUMENT;
    out[0] = 0x02;
    ok_rlp_init(&w, out + 1, cap - 1);
    list = ok_rlp_list_begin(&w);
    fields(&w, tx);
    ok_rlp_list_end(&w, list);
    if (ok_rlp_done(&w, &n) != OK_OK) return OK_RANGE;
    *len = n + 1;
    return OK_OK;
}

ok_result ok_tx_signing_hash(const ok_tx *tx, uint8_t *scratch, size_t cap, uint8_t hash[32])
{
    size_t len;
    ok_result r;
    if (!hash) return OK_ARGUMENT;
    r = ok_tx_unsigned(tx, scratch, cap, &len);
    if (r != OK_OK) return r;
    ok_keccak256(scratch, len, hash);
    return OK_OK;
}
