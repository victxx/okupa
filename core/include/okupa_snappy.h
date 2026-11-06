#ifndef OKUPA_SNAPPY_H
#define OKUPA_SNAPPY_H

#include "okupa.h"

/* Snappy block format, as devp2p v5 compresses every message after Hello:
 * a varint length, then literals and back-references. The decoder refuses
 * anything that does not produce exactly the announced length within bounds.
 * The encoder only writes literals: valid Snappy, no compression, little code. */

/* The announced length, without decoding (check it against your limit first). */
ok_result ok_snappy_length(const uint8_t *in, size_t n, size_t *len);
ok_result ok_snappy_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *len);

size_t ok_snappy_max_encoded(size_t n);
/* in must not overlap out. */
ok_result ok_snappy_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *len);

#endif
