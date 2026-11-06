#include "okupa_sntp.h"
#include <string.h>

#define NTP_TO_UNIX 2208988800u /* seconds from 1900 to 1970 */

void ok_sntp_request(const uint8_t stamp[8], uint8_t out[OK_SNTP_SIZE])
{
    memset(out, 0, OK_SNTP_SIZE);
    out[0] = 0x23;                /* no leap warning, version 4, client */
    memcpy(out + 40, stamp, 8);   /* transmit timestamp: the server copies it to originate */
}

ok_result ok_sntp_read(const uint8_t *msg, size_t n, const uint8_t stamp[8], int64_t *unix_seconds)
{
    uint32_t sec;
    if (!msg || !stamp || !unix_seconds) return OK_ARGUMENT;
    if (n < OK_SNTP_SIZE) return OK_FORMAT;
    if ((msg[0] & 7) != 4 || (msg[0] >> 6) == 3) return OK_FORMAT; /* a server, clock synchronised */
    if (msg[1] == 0 || msg[1] > 15) return OK_FORMAT;               /* stratum 0 is a "kiss of death" */
    if (memcmp(msg + 24, stamp, 8)) return OK_FORMAT;                /* not the answer to our request */
    sec = (uint32_t)msg[40] << 24 | (uint32_t)msg[41] << 16 | (uint32_t)msg[42] << 8 | msg[43];
    if (!sec) return OK_FORMAT;
    /* Era 0 ends in 2036; NTP seconds below 2^31 then mean era 1. */
    *unix_seconds = (int64_t)sec - NTP_TO_UNIX + (sec < 0x80000000u ? 4294967296LL : 0);
    return OK_OK;
}
