#include "okupa_eth.h"
#include "okupa_p2p.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t i, n = strlen(s) / 2;
    assert(n <= cap);
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
    return n;
}

static void fork(const ok_eth_network *net, uint64_t block, uint64_t time, const char *hash_hex, uint64_t want_next)
{
    uint8_t hash[4], want[4];
    uint64_t next;
    hex(hash_hex, want, 4);
    ok_forkid(net, block, time, hash, &next);
    assert(!memcmp(hash, want, 4) && next == want_next);
}

int main(void)
{
    /* Mainnet's first forks, with Constantinople and Petersburg sharing a block. */
    static const uint64_t main_blocks[] = {0, 1150000, 1920000, 2463000, 2675000, 4370000, 7280000, 7280000};
    ok_eth_network mainnet = {1, {0}, main_blocks, 8, NULL, 0};
    static uint8_t buf[512], want[512];
    uint8_t tx[119];
    const uint8_t *txs[2];
    size_t lens[2], n, len;
    ok_eth_status st;

    /* CRC-32 (IEEE) check values, as zlib gives them. */
    assert(ok_crc32(0, "123456789", 9) == 0xcbf43926u && ok_crc32(0, "", 0) == 0);
    assert(ok_crc32(ok_crc32(0, "1234", 4), "56789", 5) == 0xcbf43926u);

    /* EIP-2124 mainnet vectors (hashes cross-checked with Python's zlib). */
    hex("d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3", mainnet.genesis, 32);
    fork(&mainnet, 0, 0, "fc64ec04", 1150000);
    fork(&mainnet, 1149999, 0, "fc64ec04", 1150000);
    fork(&mainnet, 1150000, 0, "97c2c34c", 1920000);
    fork(&mainnet, 1920000, 0, "91d1f948", 2463000);

    /* Sepolia today (all forks passed), and just before the last BPO fork. */
    fork(&ok_sepolia, UINT64_MAX, 1800000000u, "6c1d9423", 0);
    fork(&ok_sepolia, UINT64_MAX, 1791294815u, "268956b6", 1791294816u);

    /* Our Status matches an independent encoder (pyrlp) byte for byte. */
    assert(ok_eth_status_write(&ok_sepolia, 1800000000u, buf, sizeof buf, &len) == OK_OK);
    n = hex("f8504583aa36a7a025a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9c6846c1d9423808080"
            "a025a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9", want, sizeof want);
    assert(len == n && !memcmp(buf, want, n));
    assert(ok_eth_status_read(buf, len, &st) == OK_OK && ok_eth_status_mismatch(&ok_sepolia, &st) == NULL);
    assert(st.version == 69 && st.network_id == 11155111 && st.fork_next == 0 && st.latest == 0);

    /* A peer still before the last fork is fine; another network, version or history is not. */
    hex("268956b6", st.fork_hash, 4);
    assert(ok_eth_status_mismatch(&ok_sepolia, &st) == NULL);
    hex("deadbeef", st.fork_hash, 4);
    assert(!strcmp(ok_eth_status_mismatch(&ok_sepolia, &st), "fork id"));
    hex("6c1d9423", st.fork_hash, 4);
    st.network_id = 1;
    assert(!strcmp(ok_eth_status_mismatch(&ok_sepolia, &st), "network id"));
    st.network_id = 11155111;
    st.version = 68;
    assert(!strcmp(ok_eth_status_mismatch(&ok_sepolia, &st), "eth version"));
    st.version = 69;
    st.genesis[0] ^= 1;
    assert(!strcmp(ok_eth_status_mismatch(&ok_sepolia, &st), "genesis"));
    st.genesis[0] ^= 1;
    st.earliest = 5;
    assert(!strcmp(ok_eth_status_mismatch(&ok_sepolia, &st), "block range"));
    assert(ok_eth_status_read(buf, len - 1, &st) == OK_FORMAT);

    /* Transactions: the signed type-2 transaction from the tx test, as one byte string. */
    hex("02f87483aa36a780843b9aca008506fc23ac0082f35c946e2c210710508fd15f37b12a2beb312ec4beaf1980864f4b50410100c0"
        "80a0f50fc320e10ae34d46db9ce4eb0e4c5b3a4bac2fbfdcc3418433a43802e377bea06d75a300b7fc644bd03cc97f407e96baf8"
        "95dc805dc5e55777391e39c9831ad4", tx, sizeof tx);
    txs[0] = tx;
    lens[0] = sizeof tx;
    assert(ok_eth_transactions(txs, lens, 1, buf, sizeof buf, &len) == OK_OK);
    assert(len == 2 + 2 + 119 && buf[0] == 0xf8 && buf[1] == 0x79 && buf[2] == 0xb8 && buf[3] == 0x77);
    assert(!memcmp(buf + 4, tx, 119));
    txs[1] = tx;
    lens[1] = sizeof tx;
    assert(ok_eth_transactions(txs, lens, 2, buf, sizeof buf, &len) == OK_OK && len == 2 + 2 * 121 && buf[0] == 0xf8);
    assert(ok_eth_transactions(txs, lens, 0, buf, sizeof buf, &len) == OK_RANGE);
    assert(ok_eth_transactions(txs, lens, 1, buf, 100, &len) == OK_RANGE);
    puts("eth: crc32, EIP-2124 fork ids (mainnet, Sepolia), Status matches pyrlp, Transactions");
    return 0;
}
