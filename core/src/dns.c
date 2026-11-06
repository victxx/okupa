#include "okupa_dns.h"
#include <string.h>

enum { TXT = 16, IN = 1 };

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

ok_result ok_dns_query(const char *name, uint16_t id, uint8_t *out, size_t cap, size_t *len)
{
    static const uint8_t header[10] = {0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0}; /* recursion desired, one question */
    size_t o = 12, n;
    const char *label = name, *dot;
    if (!name || !out || !len) return OK_ARGUMENT;
    n = strlen(name);
    if (n == 0 || n > 253 || cap < 12 + n + 2 + 4) return n > 253 || n == 0 ? OK_FORMAT : OK_RANGE;
    out[0] = (uint8_t)(id >> 8);
    out[1] = (uint8_t)id;
    memcpy(out + 2, header, sizeof header);
    for (;;) {
        size_t k;
        dot = strchr(label, '.');
        k = dot ? (size_t)(dot - label) : strlen(label);
        if (k == 0 || k > 63) return OK_FORMAT;
        out[o++] = (uint8_t)k;
        memcpy(out + o, label, k);
        o += k;
        if (!dot) break;
        label = dot + 1;
    }
    out[o++] = 0;
    out[o++] = 0;
    out[o++] = TXT;
    out[o++] = 0;
    out[o++] = IN;
    *len = o;
    return OK_OK;
}

/* Skips a possibly compressed name; 0 if it runs off the message. */
static size_t skip_name(const uint8_t *msg, size_t n, size_t at)
{
    while (at < n) {
        uint8_t k = msg[at];
        if (k == 0) return at + 1;
        if ((k & 0xc0) == 0xc0) return at + 2 <= n ? at + 2 : 0;
        if (k & 0xc0) return 0;
        at += 1 + k;
    }
    return 0;
}

ok_result ok_dns_txt(const uint8_t *msg, size_t n, uint16_t id, char *out, size_t cap, size_t *len)
{
    size_t at = 12, i, questions, answers;
    if (!msg || !out || !len || cap == 0) return OK_ARGUMENT;
    if (n < 12 || be16(msg) != id || !(msg[2] & 0x80) || (msg[2] & 0x02)) return OK_FORMAT; /* reply, not truncated */
    if ((msg[3] & 0x0f) == 3) return OK_RANGE; /* no such name */
    if (msg[3] & 0x0f) return OK_FORMAT;
    questions = be16(msg + 4);
    answers = be16(msg + 6);
    for (i = 0; i < questions; i++) {
        if (!(at = skip_name(msg, n, at)) || at + 4 > n) return OK_FORMAT;
        at += 4;
    }
    for (i = 0; i < answers; i++) {
        size_t rdlen, end, o = 0;
        if (!(at = skip_name(msg, n, at)) || at + 10 > n) return OK_FORMAT;
        rdlen = be16(msg + at + 8);
        end = at + 10 + rdlen;
        if (end > n) return OK_FORMAT;
        if (be16(msg + at) != TXT || be16(msg + at + 2) != IN) {
            at = end; /* a CNAME or anything else on the way */
            continue;
        }
        for (at += 10; at < end;) {
            size_t k = msg[at++];
            if (at + k > end) return OK_FORMAT;
            if (o + k >= cap) return OK_RANGE;
            memcpy(out + o, msg + at, k);
            o += k;
            at += k;
        }
        out[o] = 0;
        *len = o;
        return OK_OK;
    }
    return OK_RANGE;
}
