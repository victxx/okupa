#ifndef OKUPA_STATE_H
#define OKUPA_STATE_H

#include "okupa.h"
#include "okupa_pet.h"
#include <stdio.h>

/* Everything the okupa must not lose, in one snapshot: the pet, the page in
 * the sketchbook, the wall, the account's next nonce and the one transaction
 * on its way, exactly as it was signed (so a retry, even after a restart,
 * sends the same bytes and the same hash).
 *
 * "OKST" | version | 0 0 0 | generation (u64) | fields | keccak-256 of all
 * before it. Little-endian, strict: any damage is refused whole.
 *
 * The rule that makes it safe: a transaction is saved here (with the nonce
 * already moved on) before it is handed to the network. So a save that never
 * finished only loses a transaction nobody has seen. */
#define OK_STATE_VERSION 1
#define OK_STATE_WALL 6
#define OK_STATE_PICTURE 1024 /* the largest picture: 64x64, 2 bits a pixel */
#define OK_STATE_TX_MAX 1200
#define OK_STATE_WIRE_MAX 9000

/* What a picture is: a 1-bit drawing (512 bytes) or a 4-tone photo (1024). */
enum { OK_PIC_NONE = 0, OK_PIC_DRAWING = 1, OK_PIC_TONES = 2 };
/* What the transaction on its way carries. */
enum { OK_TX_NONE = 0, OK_TX_OLD = 1, OK_TX_PICTURE = 2 };

typedef struct {
    ok_pet pet;
    uint8_t page[512]; /* the sketchbook page being drawn */
    int wall_count;
    uint8_t wall_kind[OK_STATE_WALL];
    uint8_t wall[OK_STATE_WALL][OK_STATE_PICTURE];
    uint64_t chain_id;   /* the chain the nonce and the transaction belong to */
    uint64_t next_nonce; /* the next one to sign with */
    /* The transaction on its way: kind, the signed bytes and their hash, and
     * the picture it hangs once a block holds it. */
    uint8_t tx_kind;
    uint8_t tx_page;      /* 1: the picture is the sketchbook page, locked until it hangs */
    uint8_t tx_fee_level; /* how many times its fee was raised (same nonce, new bytes) */
    uint8_t tx_older;     /* earlier versions still to look for in blocks */
    uint8_t tx_older_hash[3][32];
    uint16_t tx_len;
    uint8_t tx[OK_STATE_TX_MAX];
    uint8_t tx_hash[32];
    uint8_t tx_pic_kind;
    uint8_t tx_pic[OK_STATE_PICTURE];
    uint64_t watch_from; /* the block to start looking for it from (0: unknown) */
} ok_state;

/* The bytes of a picture of this kind (0 for an unknown kind). */
size_t ok_state_picture_size(int kind);

ok_result ok_state_encode(const ok_state *s, uint64_t generation, uint8_t *out, size_t cap, size_t *len);
ok_result ok_state_decode(const uint8_t *in, size_t n, ok_state *s, uint64_t *generation);

/* Two files that take turns (state-a.bin, state-b.bin in dir): a save goes
 * to the one not in use, is flushed, synced, read back and compared, and
 * only then becomes the current one. Opening takes the newest valid one; if
 * the newest is damaged (a save cut short) the older one is used, which the
 * rule above makes safe. With files there but none valid, the store is
 * blocked: nothing is saved over them and nothing may be sent. */
typedef struct {
    char dir[96];
    uint64_t generation;
    int active;  /* 0 a, 1 b, -1 none yet */
    int blocked; /* damaged files: saving and sending stop */
    int fell_back; /* opened the older file because the newer was damaged */
    uint8_t wire[OK_STATE_WIRE_MAX];
    int fail_stage; /* the last failed save: 1 blocked, 2 encode, 3 open, 4 write, 5 sync, 6 close, 7 reread, 8 differs */
} ok_state_store;

/* 1 a snapshot was loaded into s; 0 there was none (s untouched: start a
 * new okupa, or import the old files); -1 blocked. dir ends with '/'. */
int ok_state_open(ok_state_store *st, const char *dir, ok_state *s);
/* 1 saved and read back; 0 failed (and the store keeps the last good one). */
int ok_state_save(ok_state_store *st, const ok_state *s);

/* The files of earlier versions, read once into s (net: "mainnet",
 * "sepolia"): save.bin, wall.bin, drawing-sent.bin, pending-<net>.hex,
 * nonce-<net>.txt, watch-<net>.txt. Returns how many were found. They are
 * left in place; the snapshot is what counts from then on. */
int ok_state_import(const char *dir, const char *net, ok_state *s);

#endif
