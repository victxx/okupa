#ifndef OKUPA_SAVE_H
#define OKUPA_SAVE_H

#include "okupa.h"
#include "okupa_pet.h"

/* The okupa's save: "OKSV" | version | pet | drawing | CRC-32 of all before it.
 * Little-endian, fixed size; a save that fails any check is refused whole. */
#define OK_SAVE_VERSION 1
#define OK_SAVE_SIZE (4 + 1 + OK_NEEDS + 1 + 8 + 4 * OK_NEEDS + 512 + 4)

void ok_save_write(const ok_pet *p, const uint8_t drawing[512], uint8_t out[OK_SAVE_SIZE]);
ok_result ok_save_read(const uint8_t *in, size_t n, ok_pet *p, uint8_t drawing[512]);

#endif
