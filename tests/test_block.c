#include "okupa_block.h"
#include "okupa_keccak.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
}

static uint8_t body[200000];

/* Two real mainnet blocks (bodies as in BlockBodies, fetched for the test):
 * the transactions root recomputed here must equal the one in the header,
 * and a transaction in the middle must be found by its hash. */
static void real_block(const char *path, const char *root_hex, const char *tx_hex, uint32_t txs, int32_t index)
{
    ok_rlp_item item;
    ok_header h;
    ok_body_check c;
    uint8_t want[32];
    size_t n;
    FILE *f = fopen(path, "rb");
    assert(f);
    n = fread(body, 1, sizeof body, f);
    fclose(f);
    assert(ok_rlp_decode(body, n, &item) == OK_OK);
    memset(&h, 0, sizeof h);
    hex(root_hex, h.tx_root, 32);
    hex(tx_hex, want, 32);
    assert(ok_body_check_tx(&item, &h, want, &c) == OK_OK);
    assert(c.count == txs && c.root_ok && c.found == index);
    h.tx_root[0] ^= 1;
    assert(ok_body_check_tx(&item, &h, NULL, &c) == OK_OK && !c.root_ok && c.found == -1);
}

int main(void)
{
    uint8_t root[32], expect[32], buf[600], hdr[700];
    size_t len, i;
    ok_rlp_writer w;
    ok_header h;
    ok_rlp_item list;
    uint64_t req;
    static const uint8_t hashes[2][32] = {{1}, {2}};

    /* An empty trie. */
    assert(ok_trie_ordered_root(NULL, NULL, 0, root) == OK_OK);
    hex("56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421", expect, 32);
    assert(!memcmp(root, expect, 32));

    /* 375 transactions (keys up to rlp(374), 23 legacy) and 137 (keys up to rlp(136), 29 legacy). */
    real_block("tests/vectors/mainnet-26357098-body.rlp",
               "edd0c11a14690794d7f140496c57d36bcacb59255cdb17b4463eb0a5be7e1111",
               "cead78f1421574f27d8bb1ad36bd294f7be2ccd92917a1bdef13776bf4f388fc", 375, 187);
    real_block("tests/vectors/mainnet-26357084-body.rlp",
               "cc0dd2c11a9dcd80fb2d8914db86fe337104d45dc6cfb09a3fd8ed8177dbffe1",
               "a5f0d3a80bbdf876a1fd2b2a07839b65c9902a579e9a6b1ecc783997ebcaa89d", 137, 68);

    /* A header: the fields we read and the hash of its exact bytes. */
    ok_rlp_init(&w, hdr, sizeof hdr);
    {
        size_t m = ok_rlp_list_begin(&w);
        uint8_t z[256] = {0}, p[32], r[32];
        memset(p, 0x11, 32);
        memset(r, 0x44, 32);
        ok_rlp_bytes(&w, p, 32);   /* parent */
        ok_rlp_bytes(&w, z, 32);   /* ommers */
        ok_rlp_bytes(&w, z, 20);   /* coinbase */
        ok_rlp_bytes(&w, z, 32);   /* state */
        ok_rlp_bytes(&w, r, 32);   /* transactions */
        ok_rlp_bytes(&w, z, 32);   /* receipts */
        ok_rlp_bytes(&w, z, 256);  /* bloom */
        ok_rlp_uint(&w, 0);        /* difficulty */
        ok_rlp_uint(&w, 26357098); /* number */
        ok_rlp_uint(&w, 45000000);
        ok_rlp_uint(&w, 21000);
        ok_rlp_uint(&w, 1762450000); /* time */
        ok_rlp_bytes(&w, (const uint8_t *)"okupa", 5);
        ok_rlp_list_end(&w, m);
    }
    assert(ok_rlp_done(&w, &len) == OK_OK);
    assert(ok_header_read(hdr, len, &h) == OK_OK);
    ok_keccak256(hdr, len, expect);
    assert(!memcmp(h.hash, expect, 32) && h.number == 26357098 && h.time == 1762450000);
    for (i = 0; i < 32; i++) assert(h.parent[i] == 0x11 && h.tx_root[i] == 0x44);
    assert(ok_header_read(hdr, len - 1, &h) != OK_OK);

    /* Requests and a response, with their request ids. */
    assert(ok_eth_get_headers(7, 26357098, 4, buf, sizeof buf, &len) == OK_OK);
    hex("ca07c88401922d6a048080", expect, 11);
    assert(len == 11 && !memcmp(buf, expect, 11));
    assert(ok_eth_get_bodies(9, hashes, 2, buf, sizeof buf, &len) == OK_OK && len == 2 + 1 + 2 + 66);
    assert(ok_eth_response(buf, len, &req, &list) == OK_OK && req == 9 && list.list && list.len == 66);
    printf("block: trie roots of two mainnet blocks, header hash, requests ok\n");
    return 0;
}
