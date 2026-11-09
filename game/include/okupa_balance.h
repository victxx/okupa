#ifndef OKUPA_BALANCE_H
#define OKUPA_BALANCE_H

#include "okupa.h"
#include "okupa_think.h"

/* The console's ETH balance, for the wallet screen only. The console keeps
 * no chain state (no sync, no EVM), so it asks a public JSON-RPC node: its
 * word, not a proof, and never part of sending (that is P2P only). */
typedef struct {
    char eth[24];    /* "0.004200 ETH": six decimals, rounded down (never more than there is) */
    uint64_t block;  /* the block the node read it at */
    int low;         /* under what a hello may cost: 30 000 gas x 2 gwei (display only) */
    char error[48];  /* why it failed, "" if it did not */
    uint64_t nonce;  /* transactions the account has sent, at that block (its word) */
    int have_nonce;  /* 1 if the node said */
} ok_balance;

#define OK_BALANCE_LOW_WEI 60000000000000ull /* 30 000 gas x 2 gwei */

/* A "0x..." wei quantity (up to 64 hex digits) as "N.NNNNNN ETH", rounded
 * down. OK_FORMAT for anything else. */
ok_result ok_wei_to_eth(const char *hex0x, char *out, size_t cap);

/* eth_blockNumber, then eth_getBalance and eth_getTransactionCount
 * (address, at that block), on one keep-alive connection to host; buf (cap
 * bytes) holds the answers. A missing count leaves have_nonce 0, nothing else. */
ok_result ok_balance_ask(const char *host, const char *address0x, ok_think_fetch_fn fetch, void *ctx, char *buf,
                         size_t cap, ok_balance *b);

#endif
