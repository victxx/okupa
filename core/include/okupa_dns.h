#ifndef OKUPA_DNS_H
#define OKUPA_DNS_H

#include "okupa.h"

/* Just enough DNS to ask for one TXT record over UDP and read the answer.
 * The platform sends and receives the packets; nothing here touches a socket. */
#define OK_DNS_MAX 512

ok_result ok_dns_query(const char *name, uint16_t id, uint8_t *out, size_t cap, size_t *len);
/* The first TXT answer, its strings joined, NUL-terminated. OK_RANGE: no such
 * record (NXDOMAIN or no TXT answer) or out too small. OK_FORMAT: anything malformed, a
 * different id, a truncated reply or a server error. */
ok_result ok_dns_txt(const uint8_t *msg, size_t n, uint16_t id, char *out, size_t cap, size_t *len);

#endif
