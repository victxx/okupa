#include "okupa_eth.h"
#include "okupa_p2p.h"
#include "okupa_rlp.h"
#include <string.h>

/* Sepolia as configured in go-ethereum v1.17.7 (params/config.go): the merge
 * netsplit block, then Shanghai, Cancun, Prague, Osaka and the BPO forks. */
static const uint64_t sepolia_blocks[] = {1735371};
static const uint64_t sepolia_times[] = {1677557088, 1706655072, 1741159776, 1760427360,
                                         1761017184, 1761607008, 1791294816};
const ok_eth_network ok_sepolia = {
    11155111,
    {0x25, 0xa5, 0xcc, 0x10, 0x6e, 0xea, 0x71, 0x38, 0xac, 0xab, 0x33, 0x23, 0x1d, 0x71, 0x60, 0xd6,
     0x9c, 0xb7, 0x77, 0xee, 0x0c, 0x2c, 0x55, 0x3f, 0xcd, 0xdf, 0x51, 0x38, 0x99, 0x3e, 0x6d, 0xd9},
    sepolia_blocks, sizeof sepolia_blocks / sizeof sepolia_blocks[0],
    sepolia_times, sizeof sepolia_times / sizeof sepolia_times[0],
};

/* Mainnet from go-ethereum v1.17.7 (`geth dumpgenesis --mainnet`): every
 * block fork from Homestead to Gray Glacier, then Shanghai to BPO2. */
static const uint64_t mainnet_blocks[] = {1150000, 1920000, 2463000, 2675000, 4370000, 7280000,
                                          9069000, 9200000, 12244000, 12965000, 13773000, 15050000};
static const uint64_t mainnet_times[] = {1681338455, 1710338135, 1746612311, 1764798551, 1765290071, 1767747671};
const ok_eth_network ok_mainnet = {
    1,
    {0xd4, 0xe5, 0x67, 0x40, 0xf8, 0x76, 0xae, 0xf8, 0xc0, 0x10, 0xb8, 0x6a, 0x40, 0xd5, 0xf5, 0x67,
     0x45, 0xa1, 0x18, 0xd0, 0x90, 0x6a, 0x34, 0xe6, 0x9a, 0xec, 0x8c, 0x0d, 0xb1, 0xcb, 0x8f, 0xa3},
    mainnet_blocks, sizeof mainnet_blocks / sizeof mainnet_blocks[0],
    mainnet_times, sizeof mainnet_times / sizeof mainnet_times[0],
};

uint32_t ok_crc32(uint32_t crc, const void *data, size_t n)
{
    const uint8_t *p = data;
    int k;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static uint32_t add_fork(uint32_t crc, uint64_t fork)
{
    uint8_t be[8];
    int i;
    for (i = 0; i < 8; i++) be[i] = (uint8_t)(fork >> (56 - 8 * i));
    return ok_crc32(crc, be, 8);
}

/* Walks the fork list in order; stops at the first fork not passed at the head.
 * If want is set, also reports whether any prefix of the history hashes to it. */
static uint32_t walk(const ok_eth_network *net, uint64_t head_block, uint64_t head_time, uint64_t *next,
                     const uint8_t *want, int *seen)
{
    uint32_t crc = ok_crc32(0, net->genesis, 32);
    uint64_t last = 0;
    size_t i, total = net->n_blocks + net->n_times;
    if (next) *next = 0;
    for (i = 0; i <= total; i++) {
        uint64_t fork;
        int by_time;
        if (want) {
            uint8_t h[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
            if (!memcmp(h, want, 4)) *seen = 1;
        }
        if (i == total) break;
        by_time = i >= net->n_blocks;
        fork = by_time ? net->times[i - net->n_blocks] : net->blocks[i];
        if (fork == 0 || fork == last) continue;
        if (fork > (by_time ? head_time : head_block)) {
            if (next) *next = fork;
            break;
        }
        crc = add_fork(crc, fork);
        last = fork;
    }
    return crc;
}

void ok_forkid(const ok_eth_network *net, uint64_t head_block, uint64_t head_time, uint8_t hash[4], uint64_t *next)
{
    uint32_t crc = walk(net, head_block, head_time, next, NULL, NULL);
    hash[0] = (uint8_t)(crc >> 24);
    hash[1] = (uint8_t)(crc >> 16);
    hash[2] = (uint8_t)(crc >> 8);
    hash[3] = (uint8_t)crc;
}

ok_result ok_eth_status_write(const ok_eth_network *net, uint64_t now, uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    uint8_t hash[4];
    uint64_t next;
    size_t list, fork;
    if (!net || !out || !len) return OK_ARGUMENT;
    ok_forkid(net, UINT64_MAX, now, hash, &next);
    ok_rlp_init(&w, out, cap);
    list = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, OK_P2P_ETH_VERSION);
    ok_rlp_uint(&w, net->network_id);
    ok_rlp_bytes(&w, net->genesis, 32);
    fork = ok_rlp_list_begin(&w);
    ok_rlp_bytes(&w, hash, 4);
    ok_rlp_uint(&w, next);
    ok_rlp_list_end(&w, fork);
    ok_rlp_uint(&w, 0);                /* earliest */
    ok_rlp_uint(&w, 0);                /* latest */
    ok_rlp_bytes(&w, net->genesis, 32); /* latest hash */
    ok_rlp_list_end(&w, list);
    return ok_rlp_done(&w, len);
}

static int uint_at(const ok_rlp_item *list, size_t *pos, uint64_t *v)
{
    ok_rlp_item item;
    return ok_rlp_next(list, pos, &item) == OK_OK && ok_rlp_to_uint(&item, v) == OK_OK;
}

static int bytes_at(const ok_rlp_item *list, size_t *pos, uint8_t *out, size_t n)
{
    ok_rlp_item item;
    if (ok_rlp_next(list, pos, &item) != OK_OK || item.list || item.len != n) return 0;
    memcpy(out, item.data, n);
    return 1;
}

ok_result ok_eth_status_read(const uint8_t *payload, size_t n, ok_eth_status *st)
{
    ok_rlp_item list, fork;
    size_t pos = 0, fpos = 0;
    if (!payload || !st) return OK_ARGUMENT;
    if (ok_rlp_decode(payload, n, &list) != OK_OK || !list.list) return OK_FORMAT;
    if (!uint_at(&list, &pos, &st->version) || !uint_at(&list, &pos, &st->network_id) ||
        !bytes_at(&list, &pos, st->genesis, 32))
        return OK_FORMAT;
    if (ok_rlp_next(&list, &pos, &fork) != OK_OK || !fork.list || !bytes_at(&fork, &fpos, st->fork_hash, 4) ||
        !uint_at(&fork, &fpos, &st->fork_next))
        return OK_FORMAT;
    if (!uint_at(&list, &pos, &st->earliest) || !uint_at(&list, &pos, &st->latest) ||
        !bytes_at(&list, &pos, st->latest_hash, 32))
        return OK_FORMAT;
    return OK_OK;
}

const char *ok_eth_status_mismatch(const ok_eth_network *net, const ok_eth_status *st)
{
    int seen = 0;
    if (st->version != OK_P2P_ETH_VERSION) return "eth version";
    if (st->network_id != net->network_id) return "network id";
    if (memcmp(st->genesis, net->genesis, 32)) return "genesis";
    if (st->earliest > st->latest) return "block range";
    walk(net, UINT64_MAX, UINT64_MAX, NULL, st->fork_hash, &seen);
    return seen ? NULL : "fork id";
}

ok_result ok_eth_transactions(const uint8_t *const *txs, const size_t *lens, size_t count,
                              uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t i, list;
    if ((!txs || !lens) && count) return OK_ARGUMENT;
    if (!out || !len) return OK_ARGUMENT;
    if (count == 0) return OK_RANGE; /* empty Transactions messages are discouraged */
    ok_rlp_init(&w, out, cap);
    list = ok_rlp_list_begin(&w);
    for (i = 0; i < count; i++) {
        if (!txs[i] || lens[i] == 0) return OK_ARGUMENT;
        ok_rlp_bytes(&w, txs[i], lens[i]);
    }
    ok_rlp_list_end(&w, list);
    return ok_rlp_done(&w, len);
}
