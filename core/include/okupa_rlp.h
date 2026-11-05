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

#endif
