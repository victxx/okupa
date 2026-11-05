#include "okupa_rlp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t buf[1200];
static ok_rlp_writer w;

static void start(void) { ok_rlp_init(&w, buf, sizeof buf); }

static void expect(const char *hex)
{
    size_t len, i, n = strlen(hex) / 2;
    assert(ok_rlp_done(&w, &len) == OK_OK && len == n);
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(hex + 2 * i, "%2x", &v) == 1 && buf[i] == v);
    }
}

static void str(const char *s) { ok_rlp_bytes(&w, (const uint8_t *)s, strlen(s)); }

int main(void)
{
    static const char lorem[] = "Lorem ipsum dolor sit amet, consectetur adipisicing elit";
    static uint8_t big[1024];
    uint8_t one;
    size_t a, b, c, i, len;

    /* Examples from the RLP specification (ethereum.org). */
    start(); str("dog"); expect("83646f67");
    start(); a = ok_rlp_list_begin(&w); str("cat"); str("dog"); ok_rlp_list_end(&w, a);
    expect("c88363617483646f67");
    start(); str(""); expect("80");
    start(); a = ok_rlp_list_begin(&w); ok_rlp_list_end(&w, a); expect("c0");
    start(); ok_rlp_uint(&w, 0); expect("80");
    start(); one = 0; ok_rlp_bytes(&w, &one, 1); expect("00");
    start(); ok_rlp_uint(&w, 15); expect("0f");
    start(); ok_rlp_uint(&w, 1024); expect("820400");
    start(); one = 0x80; ok_rlp_bytes(&w, &one, 1); expect("8180");
    /* The set-theoretic representation of three: [ [], [[]], [ [], [[]] ] ]. */
    start();
    a = ok_rlp_list_begin(&w);
    b = ok_rlp_list_begin(&w); ok_rlp_list_end(&w, b);
    b = ok_rlp_list_begin(&w); c = ok_rlp_list_begin(&w); ok_rlp_list_end(&w, c); ok_rlp_list_end(&w, b);
    b = ok_rlp_list_begin(&w);
    c = ok_rlp_list_begin(&w); ok_rlp_list_end(&w, c);
    c = ok_rlp_list_begin(&w); i = ok_rlp_list_begin(&w); ok_rlp_list_end(&w, i); ok_rlp_list_end(&w, c);
    ok_rlp_list_end(&w, b);
    ok_rlp_list_end(&w, a);
    expect("c7c0c1c0c3c0c1c0");
    /* 56 bytes: the first long string. */
    start(); str(lorem);
    assert(ok_rlp_done(&w, &len) == OK_OK && len == 58 && buf[0] == 0xb8 && buf[1] == 56);
    assert(!memcmp(buf + 2, lorem, 56));
    /* A long list: two of those strings, 116 bytes of payload. */
    start(); a = ok_rlp_list_begin(&w); str(lorem); str(lorem); ok_rlp_list_end(&w, a);
    assert(ok_rlp_done(&w, &len) == OK_OK && len == 118 && buf[0] == 0xf8 && buf[1] == 116);
    /* Two-byte length and leading zeros dropped from big-endian integers. */
    start(); ok_rlp_bytes(&w, big, sizeof big);
    assert(ok_rlp_done(&w, &len) == OK_OK && len == 1027 && buf[0] == 0xb9 && buf[1] == 4 && buf[2] == 0);
    start(); { uint8_t be[32] = {0}; be[30] = 1; be[31] = 2; ok_rlp_uint_be(&w, be, 32); } expect("820102");
    /* Running out of room is reported, never written past. */
    ok_rlp_init(&w, buf, 3); str("dog"); assert(ok_rlp_done(&w, &len) == OK_RANGE);
    ok_rlp_init(&w, buf, 4); a = ok_rlp_list_begin(&w); str("dog"); ok_rlp_list_end(&w, a);
    assert(ok_rlp_done(&w, &len) == OK_RANGE);
    ok_rlp_init(&w, NULL, 10); str(""); assert(ok_rlp_done(&w, &len) == OK_RANGE);
    puts("rlp: spec examples, long strings and lists, integers, overflow");
    return 0;
}
