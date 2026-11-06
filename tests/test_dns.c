#include "okupa_dns.h"
#include "okupa_dnsdisc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t i, n = strlen(s) / 2;
    assert(n <= cap);
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
    return n;
}

/* Raw replies from 1.1.1.1 on 2026-11-06 to the same queries built in Python. */
static const char LEAF_REPLY[] =
    "1234818000010001000000001a51454c5641484956534f55454b33555a4753343641333245495503616c6c077365706f"
    "6c696108657468646973636f036e65740000100001c00c001000010009382800e1e0656e723a2d4b4f34514f527a3848"
    "6845573873527a36375964634e45532d453032436b7569637335697134434b575675715041376270694e624d65507768"
    "586a4c426f4d4e427046344d5f3065346e416b7075364a4a624c5f76653561465347415a3753634e4f4767325630614d"
    "6647684777646c434f41676d6c6b676e5930676d6c77684a323061754b4a6332566a634449314e6d73786f514c69744c"
    "4b4974305a4130344458484b71683235376d323059394d6c35414f347369484c52544e5539787a6f527a626d46777749"
    "4e3059334343646d434464575277676e5a67";
static const char BRANCH_REPLY[] =
    "beef818000010001000000001a443347593254515953354736445442513537535646375a4e504d03616c6c077365706f"
    "6c696108657468646973636f036e65740000100001c00c0010000100093828004544656e72747265652d6272616e6368"
    "3a554242474f5353565332434336524156595346534a4157584c342c334449574a5654474d36333758364b354f345351"
    "37324e484855";
#define LEAF_NAME "QELVAHIVSOUEK3UZGS46A32EIU.all.sepolia.ethdisco.net"

int main(void)
{
    static uint8_t msg[OK_DNS_MAX], q[OK_DNS_MAX];
    char txt[400];
    size_t n, len, qn;

    /* Our query is exactly the question the server echoed back. */
    assert(ok_dns_query(LEAF_NAME, 0x1234, q, sizeof q, &qn) == OK_OK && qn == 12 + 53 + 4);
    n = hex(LEAF_REPLY, msg, sizeof msg);
    assert(!memcmp(q, msg, 2) && !memcmp(q + 12, msg + 12, qn - 12));

    assert(ok_dns_txt(msg, n, 0x1234, txt, sizeof txt, &len) == OK_OK && len == 224);
    assert(!memcmp(txt, "enr:-KO4", 8) && ok_dnsdisc_matches("QELVAHIVSOUEK3UZGS46A32EIU", 26, txt, len));
    assert(ok_dns_txt(msg, n, 0x1235, txt, sizeof txt, &len) == OK_FORMAT); /* not our id */
    assert(ok_dns_txt(msg, n - 1, 0x1234, txt, sizeof txt, &len) == OK_FORMAT);
    assert(ok_dns_txt(msg, n, 0x1234, txt, 100, &len) == OK_RANGE);
    msg[2] |= 0x02; /* truncated */
    assert(ok_dns_txt(msg, n, 0x1234, txt, sizeof txt, &len) == OK_FORMAT);
    msg[2] &= (uint8_t)~0x02;
    msg[3] |= 3; /* NXDOMAIN */
    assert(ok_dns_txt(msg, n, 0x1234, txt, sizeof txt, &len) == OK_RANGE);

    n = hex(BRANCH_REPLY, msg, sizeof msg);
    assert(ok_dns_txt(msg, n, 0xbeef, txt, sizeof txt, &len) == OK_OK);
    assert(!strcmp(txt, "enrtree-branch:UBBGOSSVS2CC6RAVYSFSJAWXL4,3DIWJVTGM637X6K5O4SQ72NHHU"));
    msg[7] = 0; /* no answers */
    assert(ok_dns_txt(msg, n, 0xbeef, txt, sizeof txt, &len) == OK_RANGE);

    assert(ok_dns_query("a..b", 1, q, sizeof q, &qn) == OK_FORMAT);
    assert(ok_dns_query(LEAF_NAME, 1, q, 20, &qn) == OK_RANGE);
    puts("dns: our TXT query matches the echoed question; live replies decode; bad replies refused");
    return 0;
}
