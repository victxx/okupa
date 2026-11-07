#ifndef OKUPA_STORE_H
#define OKUPA_STORE_H

#include "okupa.h"
#include "okupa_think.h"
#include "okupa_json.h"
#include "okupa_zgs.h"

/* One JSON-RPC call over the platform's HTTPS round trip (keep-alive): body
 * POSTed to host "/", the answer read into buf (cap bytes), *result its
 * "result" member. 0 and a reason in error if it failed or the node
 * answered with an error object. */
int ok_jsonrpc_call(ok_think_fetch_fn fetch, void *ctx, const char *host, const char *body, size_t body_len, char *buf,
                    size_t cap, ok_json *result, char *error, size_t error_cap);

/* Putting a file (one segment, up to 256 KB) on 0G Storage from the console
 * itself: its root is computed here (okupa_zgs.h); the submission is a
 * transaction to the Flow contract signed here with the 0G key and sent to
 * the 0G chain's JSON-RPC over HTTPS; the segment goes to the storage nodes
 * over plain HTTP. The platform gives the round trips (the same callback as
 * the mind's) and a way to wait. */
typedef struct {
    const char *rpc_host;       /* the 0G chain's JSON-RPC, e.g. evmrpc.0g.ai (HTTPS) */
    const char *node[8];        /* storage nodes, "ip:port" (plain HTTP); the segment goes to all */
    int node_shard[8];          /* each node's shard id */
    int node_count, shards;     /* shards: how many the network splits into */
    uint8_t flow[20], market[20];
    uint64_t chain_id;          /* 16661 mainnet */
} ok_store_net;

typedef enum {
    OK_STORE_DONE = 0, /* stored: the nodes have it (finalized or uploaded) */
    OK_STORE_SETUP,
    OK_STORE_RPC,      /* the chain's JSON-RPC failed or refused */
    OK_STORE_TX,       /* the submission was not accepted */
    OK_STORE_WAIT,     /* the nodes never saw the submission */
    OK_STORE_UPLOAD    /* no node took the segment */
} ok_store_stage;

typedef struct {
    ok_store_stage stage;
    int already;          /* the file was there before */
    int finalized;        /* every shard has a node that says it is finalized */
    uint64_t nonce, gas_price, gas, fee, seq;
    char root[67];        /* 0x + 64 hex */
    char tx[67];          /* the submission's transaction hash */
    char error[64];
    char step[48];        /* the last thing it did, for the feed */
    ok_zgs_file file;
} ok_store;

/* wait_ms: the platform's pause (it should let the game run). log: optional,
 * called with each step. buf/cap: room for the request bodies (at least
 * 4/3 of size + 4 KB). */
ok_result ok_store_put(ok_store *s, const ok_store_net *n, const uint8_t key[32], const uint8_t *data, size_t size,
                       char *buf, size_t cap, ok_think_fetch_fn https, ok_think_fetch_fn http, void *ctx,
                       void (*wait_ms)(int ms), void (*log)(const char *step));

#endif
