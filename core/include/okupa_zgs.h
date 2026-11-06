#ifndef OKUPA_ZGS_H
#define OKUPA_ZGS_H

#include "okupa.h"

/* 0G Storage from the console, for files of one segment (up to 256 KB): the
 * Merkle root and submission nodes exactly as the official SDK computes them
 * (keccak256 over 256-byte chunks, zero padding, odd nodes carried up), the
 * Flow contract's submit() calldata, the fee, and the segment the storage
 * node takes. No I/O here: the platform talks to the chain and the node. */
#define OK_ZGS_CHUNK 256
#define OK_ZGS_SEGMENT_CHUNKS 1024
#define OK_ZGS_MAX (OK_ZGS_CHUNK * OK_ZGS_SEGMENT_CHUNKS)
#define OK_ZGS_NODES 12

typedef struct {
    size_t size;
    uint32_t chunks, padded_chunks;
    int node_count;
    uint8_t node_height[OK_ZGS_NODES];
    uint8_t node_root[OK_ZGS_NODES][32];
    uint8_t root[32]; /* the file's root: what 0G Storage knows it by */
} ok_zgs_file;

/* Lays out and hashes data (1..OK_ZGS_MAX bytes). */
ok_result ok_zgs_file_hash(const uint8_t *data, size_t size, ok_zgs_file *f);
/* submit((length, tags, nodes[]), submitter) with empty tags. */
ok_result ok_zgs_submit_calldata(const ok_zgs_file *f, const uint8_t submitter[20], uint8_t *out, size_t cap,
                                 size_t *len);
/* The storage fee: one sector a chunk of each node, times the market price. */
uint64_t ok_zgs_fee(const ok_zgs_file *f, uint64_t price_per_sector);
/* The JSON-RPC body for zgs_uploadSegmentsByTxSeq: the one segment (the data
 * chunks, the last zero-padded), its proof and the file size. */
ok_result ok_zgs_upload_body(const ok_zgs_file *f, const uint8_t *data, uint64_t tx_seq, char *out, size_t cap,
                             size_t *len);

#endif
