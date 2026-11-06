#include "okupa_sntp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void answer(uint8_t m[48], const uint8_t stamp[8], uint32_t sec)
{
    memset(m, 0, 48);
    m[0] = 0x24; /* version 4, server */
    m[1] = 2;    /* stratum */
    memcpy(m + 24, stamp, 8);
    m[40] = (uint8_t)(sec >> 24);
    m[41] = (uint8_t)(sec >> 16);
    m[42] = (uint8_t)(sec >> 8);
    m[43] = (uint8_t)sec;
}

int main(void)
{
    static const uint8_t stamp[8] = {1, 2, 3, 4, 5, 6, 7, 8}, other[8] = {9};
    uint8_t q[48], m[48];
    int64_t t;

    ok_sntp_request(stamp, q);
    assert(q[0] == 0x23 && !memcmp(q + 40, stamp, 8));

    /* 2026-11-06 22:00:00 UTC is unix 1794002400. */
    answer(m, stamp, 1794002400u + 2208988800u);
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_OK && t == 1794002400);
    /* After 2036 the seconds wrap: era 1. */
    answer(m, stamp, 1000);
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_OK && t == 1000 - 2208988800LL + 4294967296LL);

    /* Refused: someone else's answer, a client packet, an unsynchronised
     * server, a kiss of death, a short or empty packet. */
    answer(m, other, 3000000000u);
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_FORMAT);
    answer(m, stamp, 3000000000u);
    m[0] = 0x23;
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_FORMAT);
    answer(m, stamp, 3000000000u);
    m[0] = 0xe4;
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_FORMAT);
    answer(m, stamp, 3000000000u);
    m[1] = 0;
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_FORMAT);
    assert(ok_sntp_read(m, 47, stamp, &t) == OK_FORMAT);
    answer(m, stamp, 0);
    assert(ok_sntp_read(m, 48, stamp, &t) == OK_FORMAT);
    puts("sntp: request, answer, era wrap and the packets it refuses");
    return 0;
}
