#include "okupa_tx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
}

/* Expected values from an independent encoder (Python rlp + pycryptodome). */
static void check(const ok_tx *tx, size_t want_len, const char *prefix, const char *hash_hex)
{
    static uint8_t buf[2048];
    uint8_t hash[32], want[32], head[40];
    size_t len;
    hex(hash_hex, want, 32);
    hex(prefix, head, strlen(prefix) / 2);
    assert(ok_tx_unsigned(tx, buf, sizeof buf, &len) == OK_OK && len == want_len);
    assert(!memcmp(buf, head, strlen(prefix) / 2));
    assert(ok_tx_signing_hash(tx, buf, sizeof buf, hash) == OK_OK && !memcmp(hash, want, 32));
}

int main(void)
{
    static uint8_t big[1024];
    static uint8_t small[16];
    static const uint8_t hello[] = {'O', 'K', 'P', 'A', 1, 0};
    ok_tx tx;
    size_t i, len;
    memset(&tx, 0, sizeof tx);
    hex("6e2c210710508fd15f37b12a2beb312ec4beaf19", tx.to, 20);

    tx.chain_id = 11155111; tx.max_priority_fee = 1000000000; tx.max_fee = 30000000000ull;
    tx.gas_limit = 62300; tx.data = hello; tx.data_len = sizeof hello;
    check(&tx, 51, "02f183aa36a780843b9aca008506fc23ac0082f35c946e2c210710508fd15f37b12a2beb312ec4be",
          "cdde6a5e05f9e927a507632463aa9ae6fbf6cfeb3df0d219c8e0b641cf630a97");

    tx.chain_id = 1; tx.nonce = 5; tx.max_priority_fee = 0; tx.max_fee = 0; tx.gas_limit = 21000;
    tx.value = 123456789; tx.data = NULL; tx.data_len = 0;
    check(&tx, 37, "02e301058080825208946e2c210710508fd15f37b12a2beb312ec4beaf1984075bcd1580c0",
          "e6e970aa574c71ca9be215a378b9d5272657f842231defe7c0474ad9c1506e92");

    for (i = 0; i < sizeof big; i++) big[i] = (uint8_t)i;
    tx.chain_id = 11155111; tx.nonce = 4294967296ull; tx.max_priority_fee = 2; tx.max_fee = 3;
    tx.gas_limit = 100000; tx.value = 0; tx.data = big; tx.data_len = sizeof big;
    check(&tx, 1070, "02f9042a83aa36a78501000000000203830186a0946e2c210710508fd15f37b12a2beb312ec4beaf",
          "3cf4347362b5f907f23f055f6bd4f3cc1cdf635be3e747715568c901dd3ba79c");

    /* Too small a buffer is an error, never a partial transaction. */
    assert(ok_tx_unsigned(&tx, small, sizeof small, &len) == OK_RANGE);
    tx.data = NULL;
    assert(ok_tx_unsigned(&tx, small, sizeof small, &len) == OK_ARGUMENT);
    puts("tx: unsigned type 2 bytes and signing hash match an independent encoder");
    return 0;
}
