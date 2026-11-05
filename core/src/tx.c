#include "okupa_tx.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_rlp.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"

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

ok_result ok_tx_sign(const ok_tx *tx, const uint8_t priv[32], uint8_t *out, size_t cap,
                     size_t *len, uint8_t hash[32])
{
    const secp256k1_context *ctx = ok_secp();
    secp256k1_ecdsa_recoverable_signature sig;
    uint8_t digest[32], rs[64];
    int recid;
    ok_rlp_writer w;
    size_t list, n;
    ok_result r;
    if (!priv || !len || !hash) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    /* The output buffer doubles as scratch for the unsigned form. */
    r = ok_tx_signing_hash(tx, out, cap, digest);
    if (r != OK_OK) return r;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, digest, priv, NULL, NULL)) return OK_CRYPTO;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, rs, &recid, &sig);
    out[0] = 0x02;
    ok_rlp_init(&w, out + 1, cap - 1);
    list = ok_rlp_list_begin(&w);
    fields(&w, tx);
    ok_rlp_uint(&w, (uint64_t)recid);
    ok_rlp_uint_be(&w, rs, 32);
    ok_rlp_uint_be(&w, rs + 32, 32);
    ok_rlp_list_end(&w, list);
    ok_wipe(&sig, sizeof sig);
    if (ok_rlp_done(&w, &n) != OK_OK) return OK_RANGE;
    *len = n + 1;
    ok_keccak256(out, *len, hash);
    return OK_OK;
}
