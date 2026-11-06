#ifndef OKUPA_SNTP_H
#define OKUPA_SNTP_H

#include "okupa.h"

/* The time of day from an NTP server (SNTP, RFC 4330), one UDP packet each
 * way; the platform sends and receives it. The console's own clock can be
 * days off, and TLS certificates and 0G tokens need real UTC.
 * This is not authenticated: it only has to beat a clock that is wrong, and
 * the caller never lets it go below a floor it knows. */
#define OK_SNTP_SIZE 48

/* stamp: 8 bytes the server must echo back (random, so a stray or replayed
 * packet is refused). */
void ok_sntp_request(const uint8_t stamp[8], uint8_t out[OK_SNTP_SIZE]);
/* OK_OK with unix seconds from a server's answer to that request. */
ok_result ok_sntp_read(const uint8_t *msg, size_t n, const uint8_t stamp[8], int64_t *unix_seconds);

#endif
