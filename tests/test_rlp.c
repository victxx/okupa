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
    /* Decoding: round trips, list walks and every malformed shape. */
    {
        ok_rlp_item it, child;
        size_t pos = 0;
        uint64_t v;
        static const uint8_t cat_dog[] = {0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g'};
        static const uint8_t bad[][4] = {
            {0x81, 0x05},             /* single byte < 0x80 wrapped */
            {0xb8, 0x05, 1, 2},       /* long form for a short string */
            {0xb9, 0x00, 0x40},       /* leading zero in the length */
            {0x83, 'd', 'o'},         /* truncated payload */
            {0xc3, 0x83, 'a', 'b'},   /* list truncated */
        };
        static const size_t bad_len[] = {2, 4, 3, 3, 4};
        assert(ok_rlp_decode(cat_dog, sizeof cat_dog, &it) == OK_OK && it.list && it.len == 8);
        assert(ok_rlp_next(&it, &pos, &child) == OK_OK && !child.list && child.len == 3 && !memcmp(child.data, "cat", 3));
        assert(ok_rlp_next(&it, &pos, &child) == OK_OK && !memcmp(child.data, "dog", 3));
        assert(ok_rlp_next(&it, &pos, &child) == OK_RANGE);
        for (i = 0; i < 5; i++) assert(ok_rlp_decode(bad[i], bad_len[i], &it) == OK_FORMAT);
        assert(ok_rlp_decode(cat_dog, sizeof cat_dog - 1, &it) == OK_FORMAT);
        { uint8_t extra[10]; memcpy(extra, cat_dog, 9); extra[9] = 0; assert(ok_rlp_decode(extra, 10, &it) == OK_FORMAT); }
        assert(ok_rlp_decode(cat_dog, 0, &it) == OK_ARGUMENT);
        /* A list item claiming more bytes than its parent holds. */
        { static const uint8_t lie[] = {0xc2, 0x85, 'x'}; assert(ok_rlp_decode(lie, sizeof lie, &it) == OK_FORMAT); }
        /* Nesting deeper than 16 lists is refused; 16 is fine. */
        { uint8_t deep[17]; /* d0 cf ... c1 c0: seventeen lists, each holding the next */
          for (i = 0; i < 17; i++) deep[i] = (uint8_t)(0xc0 + (16 - i));
          assert(ok_rlp_decode(deep, 17, &it) == OK_FORMAT);
          assert(ok_rlp_decode(deep + 1, 16, &it) == OK_OK); }
        /* Encoder output decodes back, including the 1024-byte string. */
        start(); ok_rlp_bytes(&w, big, sizeof big); assert(ok_rlp_done(&w, &len) == OK_OK);
        assert(ok_rlp_decode(buf, len, &it) == OK_OK && it.len == 1024 && it.data == buf + 3);
        start(); ok_rlp_uint(&w, 1024); assert(ok_rlp_done(&w, &len) == OK_OK);
        assert(ok_rlp_decode(buf, len, &it) == OK_OK && ok_rlp_to_uint(&it, &v) == OK_OK && v == 1024);
        start(); ok_rlp_uint(&w, 0); assert(ok_rlp_done(&w, &len) == OK_OK);
        assert(ok_rlp_decode(buf, len, &it) == OK_OK && ok_rlp_to_uint(&it, &v) == OK_OK && v == 0);
        { static const uint8_t zero_led[] = {0x82, 0x00, 0x01}; assert(ok_rlp_decode(zero_led, 3, &it) == OK_OK && ok_rlp_to_uint(&it, &v) == OK_FORMAT); }
        { static const uint8_t nine[] = {0x89, 1, 0, 0, 0, 0, 0, 0, 0, 0}; assert(ok_rlp_decode(nine, 10, &it) == OK_OK && ok_rlp_to_uint(&it, &v) == OK_FORMAT); }
        /* Every one-byte truncation of a valid long list fails cleanly. */
        start(); a = ok_rlp_list_begin(&w); str(lorem); str(lorem); ok_rlp_list_end(&w, a); assert(ok_rlp_done(&w, &len) == OK_OK);
        for (i = 0; i < len; i++) assert(ok_rlp_decode(buf, i, &it) != OK_OK);
    }
    puts("rlp: spec examples, long strings and lists, integers, overflow, strict decoding");
    return 0;
}
