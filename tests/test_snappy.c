#include "okupa_snappy.h"
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

static uint8_t in[200000], out[200000], back[200000];

static void decodes(const char *compressed, const uint8_t *want, size_t want_len)
{
    size_t n = hex(compressed, in, sizeof in), len;
    assert(ok_snappy_length(in, n, &len) == OK_OK && len == want_len);
    assert(ok_snappy_decode(in, n, out, sizeof out, &len) == OK_OK && len == want_len);
    assert(!memcmp(out, want, want_len));
    assert(want_len == 0 || ok_snappy_decode(in, n, out, want_len - 1, &len) == OK_RANGE);
}

static void refused(const char *compressed)
{
    size_t n = hex(compressed, in, sizeof in), len;
    assert(ok_snappy_decode(in, n, out, sizeof out, &len) == OK_FORMAT);
}

int main(void)
{
    static const size_t sizes[] = {0, 1, 60, 61, 256, 257, 65536, 65537, 150000};
    uint8_t want[300];
    size_t i, j, len, n;

    /* Compressed by cramjam 2.9.1 (Rust snap): literals, 2-byte-offset copies, overlap. */
    decodes("00", want, 0);
    decodes("1d1468656c6c6f205a0600", (const uint8_t *)"hello hello hello hello hello", 29);
    for (i = 0; i < 300; i++) want[i] = (uint8_t)(i % 10);
    decodes("ac022400010203040506070809fe0a00fe0a00fe0a00fe0a00860a00", want, 300);
    /* Hand-made: 1-byte and 4-byte offset copies. */
    decodes("080c61626364" "0104", (const uint8_t *)"abcdabcd", 8);
    decodes("080c61626364" "0f04000000", (const uint8_t *)"abcdabcd", 8);

    /* Malformed: offset 0, offset past the start, short or long output,
     * truncated literal or offset, oversized varint. */
    refused("080c61626364" "0100");
    refused("080c61626364" "0105");
    refused("090c61626364" "0104");
    refused("070c61626364" "0104");
    refused("050c616263");
    refused("080c61626364" "01");
    refused("080c61626364" "0f0400");
    refused("8080808010");
    refused("ffffffffff00");
    refused("05f0");
    refused("05f4ffffffff");

    /* Our literal-only output (format checked against cramjam's decoder) round-trips
     * across every literal length form. */
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        for (j = 0; j < sizes[i]; j++) in[j] = (uint8_t)(j * 31 + i);
        assert(ok_snappy_encode(in, sizes[i], out, sizeof out, &n) == OK_OK && n <= ok_snappy_max_encoded(sizes[i]));
        assert(ok_snappy_decode(out, n, back, sizeof back, &len) == OK_OK && len == sizes[i]);
        assert(!memcmp(back, in, len));
    }
    assert(ok_snappy_encode((const uint8_t *)"ok", 2, out, sizeof out, &n) == OK_OK);
    assert(n == 4 && !memcmp(out, "\x02\x04ok", 4));
    assert(ok_snappy_encode(in, 100, out, ok_snappy_max_encoded(100) - 1, &n) == OK_RANGE);
    puts("snappy: decodes cramjam output and hand-made copies, refuses malformed input; literals round-trip");
    return 0;
}
