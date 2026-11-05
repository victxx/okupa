#ifndef OKUPA_RLP_H
#define OKUPA_RLP_H

#include "okupa.h"

/* RLP encoder into a caller-owned buffer. Writes past the end are dropped and
 * remembered; check ok_rlp_done before using the output. */
typedef struct {
    uint8_t *buf;
    size_t cap, len;
    int overflow;
} ok_rlp_writer;

void ok_rlp_init(ok_rlp_writer *w, uint8_t *buf, size_t cap);
void ok_rlp_bytes(ok_rlp_writer *w, const uint8_t *data, size_t n);
void ok_rlp_uint(ok_rlp_writer *w, uint64_t v);
/* Big-endian unsigned integer of any width; leading zero bytes are dropped. */
void ok_rlp_uint_be(ok_rlp_writer *w, const uint8_t *be, size_t n);
/* A list: remember the mark, write its items, then close it with the mark. */
size_t ok_rlp_list_begin(ok_rlp_writer *w);
void ok_rlp_list_end(ok_rlp_writer *w, size_t mark);
ok_result ok_rlp_done(const ok_rlp_writer *w, size_t *len);

/* Decoder: canonical encodings only, every length checked against the input.
 * Items point into the input; nothing is copied. */
typedef struct {
    int list;
    const uint8_t *data; /* payload */
    size_t len;
} ok_rlp_item;

/* One item at the start of in; *used = header + payload bytes. */
ok_result ok_rlp_item_at(const uint8_t *in, size_t n, ok_rlp_item *item, size_t *used);
/* Exactly one item filling the whole input; nested lists checked to depth 16. */
ok_result ok_rlp_decode(const uint8_t *in, size_t n, ok_rlp_item *item);
/* Walk a list: start with *pos = 0; OK_RANGE when the list is exhausted. */
ok_result ok_rlp_next(const ok_rlp_item *list, size_t *pos, ok_rlp_item *child);
/* Canonical unsigned integer up to 64 bits (no leading zeros). */
ok_result ok_rlp_to_uint(const ok_rlp_item *item, uint64_t *v);

#endif
