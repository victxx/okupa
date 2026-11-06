#ifndef OKUPA_ECDH_H
#define OKUPA_ECDH_H

#include "okupa.h"

/* Raw ECDH as devp2p uses it: the x coordinate of priv * pub, unhashed.
 * pub is 64 bytes (x || y) without the 0x04 prefix. */
ok_result ok_ecdh(const uint8_t priv[32], const uint8_t pub[64], uint8_t secret[32]);

#endif
