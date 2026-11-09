#include "account.h"
#include "okupa_facts.h"
#include "okupa_rlp.h"
#include "okupa_keccak.h"
#include "okupa_act.h"
#include "okupa_keys.h"
#include "okupa_tx.h"
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SAMPLES 480 /* about eight seconds of scribbling */

static const char *key_path;
static ok_keccak pool;
static int samples, last_x = -1, last_y = -1, started;
static uint8_t seed[32];
static int have_seed;

void ok_dsi_account_seed(const uint8_t s[32])
{
    memcpy(seed, s, sizeof seed);
    have_seed = 1;
}

static void show_address(const uint8_t priv[32], ok_ui *u)
{
    uint8_t pub[64], addr[20];
    if (ok_keys_public(priv, pub) != OK_OK) return;
    ok_keys_address(pub, addr);
    ok_keys_checksum(addr, u->wallet_address);
    u->wallet_ready = 1;
    snprintf(ok_facts_now.address, sizeof ok_facts_now.address, "%s", u->wallet_address);
}

void ok_dsi_account_open(const char *path, ok_ui *u)
{
    char text[80] = {0};
    uint8_t priv[32];
    unsigned i, v;
    FILE *f;
    key_path = path;
    if (u->wallet_ready || !path || !(f = fopen(path, "r"))) return;
    if (fgets(text, sizeof text, f)) {
        for (i = 0; i < 32 && sscanf(text + 2 * i, "%2x", &v) == 1; i++) priv[i] = (uint8_t)v;
        if (i == 32) show_address(priv, u);
    }
    fclose(f);
    ok_wipe(priv, sizeof priv);
    ok_wipe(text, sizeof text);
}

/* Each sample adds where the stylus is and exactly when (a 33 MHz timer):
 * human movement is the randomness. Only moving samples count. */
void ok_dsi_account_scribble(int x, int y, ok_ui *u)
{
    uint32_t t = cpuGetTiming();
    time_t now = time(NULL);
    uint8_t priv[32], pub[64];
    uint32_t round = 0;
    FILE *f;
    int i;
    if (u->wallet_ready) return;
    if (!started) {
        ok_keccak_init(&pool);
        ok_keccak_update(&pool, "okupa account", 13);
        ok_keccak_update(&pool, &now, sizeof now);
        if (have_seed) { /* a one-use seed from seeds.bin, mixed with the strokes */
            ok_keccak_update(&pool, seed, sizeof seed);
            ok_wipe(seed, sizeof seed);
            have_seed = 0;
        }
        started = 1;
    }
    ok_keccak_update(&pool, &x, sizeof x);
    ok_keccak_update(&pool, &y, sizeof y);
    ok_keccak_update(&pool, &t, sizeof t);
    if (x != last_x || y != last_y) samples++;
    last_x = x;
    last_y = y;
    u->wallet_progress = samples * 100 / SAMPLES;
    ok_facts_now.scribble_samples = samples;
    if (samples < SAMPLES) return;
    if (!key_path) {
        snprintf(u->wallet_address, sizeof u->wallet_address, "Needs an SD card");
        return;
    }
    do {
        ok_keccak k = pool;
        ok_keccak_update(&k, &round, sizeof round);
        ok_keccak_final(&k, priv);
        round++;
    } while (ok_keys_public(priv, pub) != OK_OK);
    /* Never replace a key that is already on the card (it may hold funds):
     * if one is there after all, use it. New keys go to a new file first. */
    if ((f = fopen(key_path, "r"))) {
        fclose(f);
        ok_wipe(priv, sizeof priv);
        ok_wipe(&pool, sizeof pool);
        u->wallet_ready = 0;
        ok_dsi_account_open(key_path, u);
        return;
    }
    if (!(f = fopen(key_path, "w"))) {
        snprintf(u->wallet_address, sizeof u->wallet_address, "Could not write the SD");
        ok_wipe(priv, sizeof priv);
        return;
    }
    for (i = 0; i < 32; i++) fprintf(f, "%02x", priv[i]);
    fprintf(f, "\n");
    fclose(f);
    show_address(priv, u);
    ok_wipe(priv, sizeof priv);
    ok_wipe(&pool, sizeof pool);
}

/* The signed transaction's own fields, for the technical screens. */
static void note_tx(const ok_tx *tx, const uint8_t *raw, size_t len, const uint8_t hash[32], uint32_t us, const char *kind)
{
    ok_facts *f = &ok_facts_now;
    ok_rlp_item list, it;
    size_t pos = 0;
    int i;
    if (len > sizeof f->tx) return;
    memcpy(f->tx, raw, len);
    f->tx_len = (uint32_t)len;
    memcpy(f->tx_hash, hash, 32);
    f->tx_nonce = tx->nonce;
    f->tx_gas = tx->gas_limit;
    f->tx_max_fee = tx->max_fee;
    f->tx_tip = tx->max_priority_fee;
    f->tx_data_len = (uint32_t)tx->data_len;
    f->us_sign = us;
    f->tx_handed = 0;
    f->nonce = tx->nonce + 1;
    snprintf(f->tx_kind, sizeof f->tx_kind, "%s", kind);
    /* 0x02 || [chain, nonce, tip, max fee, gas, to, value, data, access, y, r, s] */
    if (ok_rlp_decode(raw + 1, len - 1, &list) != OK_OK) return;
    for (i = 0; ok_rlp_next(&list, &pos, &it) == OK_OK; i++) {
        uint64_t v;
        if (i == 9 && ok_rlp_to_uint(&it, &v) == OK_OK) f->tx_v = (int)v;
        if (i == 10 && it.len <= 32) {
            memset(f->tx_r, 0, 32);
            memcpy(f->tx_r + 32 - it.len, it.data, it.len);
        }
        if (i == 11 && it.len <= 32) {
            memset(f->tx_s, 0, 32);
            memcpy(f->tx_s + 32 - it.len, it.data, it.len);
        }
    }
}

/* A zero-value transaction to itself carrying an Okupa act, signed here with
 * the console's key. The fees are capped: a 0.1 gwei tip and at most 2 gwei a
 * unit of gas (today's base fee is far below), on a gas limit just above what
 * the act needs, so the most it can ever cost is gas_limit x 2 gwei. */
static int sign_act(uint64_t chain_id, uint64_t nonce, const uint8_t *act, size_t act_len, uint64_t gas_limit,
                    const char *kind, uint8_t *out, size_t cap, size_t *len, uint8_t hash[32])
{
    char text[80] = {0};
    uint8_t priv[32], pub[64];
    unsigned i, v;
    ok_tx tx;
    FILE *f;
    int ok = 0;
    if (!key_path || !(f = fopen(key_path, "r"))) return 0;
    if (fgets(text, sizeof text, f)) {
        for (i = 0; i < 32 && sscanf(text + 2 * i, "%2x", &v) == 1; i++) priv[i] = (uint8_t)v;
        if (i == 32 && ok_keys_public(priv, pub) == OK_OK) {
            uint32_t t0;
            memset(&tx, 0, sizeof tx);
            tx.chain_id = chain_id;
            tx.nonce = nonce;
            tx.max_priority_fee = 100000000ull; /* 0.1 gwei */
            tx.max_fee = 2000000000ull;         /* 2 gwei */
            tx.gas_limit = gas_limit;
            ok_keys_address(pub, tx.to); /* to itself */
            tx.data = act;
            tx.data_len = act_len;
            t0 = cpuGetTiming();
            ok = ok_tx_sign(&tx, priv, out, cap, len, hash) == OK_OK;
            if (ok) note_tx(&tx, out, *len, hash, (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33514u), kind);
        }
    }
    fclose(f);
    ok_wipe(priv, sizeof priv);
    ok_wipe(text, sizeof text);
    return ok;
}

/* A drawing: about 21 000 + 16 x 518 gas, or the calldata floor (with room
 * for a higher one). */
int ok_dsi_account_act(uint64_t chain_id, uint64_t nonce, const uint8_t *pixels, uint8_t *out, size_t cap, size_t *len,
                       uint8_t hash[32])
{
    uint8_t act[OK_ACT_MAX];
    size_t act_len;
    if (!pixels || ok_act_drawing(pixels, act, sizeof act, &act_len) != OK_OK) return 0;
    return sign_act(chain_id, nonce, act, act_len, 60000, "drawing", out, cap, len, hash);
}

/* A 4-tone photo: 1030 bytes of calldata, about 62 200 gas at the calldata
 * floor; 80 000 leaves room. */
int ok_dsi_account_photo(uint64_t chain_id, uint64_t nonce, const uint8_t *tones, uint8_t *out, size_t cap, size_t *len,
                         uint8_t hash[32])
{
    uint8_t act[OK_ACT_MAX];
    size_t act_len;
    if (!tones || ok_act_photo(tones, act, sizeof act, &act_len) != OK_OK) return 0;
    return sign_act(chain_id, nonce, act, act_len, 80000, "photo", out, cap, len, hash);
}
