#include "okupa_enr.h"
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

/* EIP-778's example record. */
static const char EXAMPLE[] =
    "enr:-IS4QHCYrYZbAKWCBRlAy5zzaDZXJBGkcnh4MHcBFZntXNFrdvJjX04jRzjzCBOonrkTfj499SZuOh8R33Ls8RRcy5wBgmlkgnY0gmlw"
    "hH8AAAGJc2VjcDI1NmsxoQPKY0yuDUmstAHYpMa2_oxVtw0RW_QAdpzBQA8yWM0xOIN1ZHCCdl8";
/* A Sepolia node from all.sepolia.ethdisco.net (leaf QELVAHIVSOUEK3UZGS46A32EIU),
 * decoded independently with pyrlp + eth-keys. */
static const char SEPOLIA[] =
    "enr:-KO4QORz8HhEW8sRz67YdcNES-E02Ckuics5iq4CKWVuqPA7bpiNbMePwhXjLBoMNBpF4M_0e4nAkpu6JJbL_ve5aFSGAZ7ScNOGg2V0"
    "aMfGhGwdlCOAgmlkgnY0gmlwhJ20auKJc2VjcDI1NmsxoQLitLKIt0ZA04DXHKqh257m20Y9Ml5AO4siHLRTNU9xzoRzbmFwwIN0Y3CCdmCD"
    "dWRwgnZg";

int main(void)
{
    uint8_t want[64], id[32], buf[8];
    char bad[sizeof SEPOLIA];
    ok_enr e;
    size_t n;

    assert(ok_enr_parse_text(EXAMPLE, strlen(EXAMPLE), &e) == OK_OK);
    assert(e.seq == 1 && e.has_ip && !memcmp(e.ip, "\x7f\x00\x00\x01", 4) && e.udp == 30303 && e.tcp == 0);
    assert(!e.has_eth);
    hex("ca634cae0d49acb401d8a4c6b6fe8c55b70d115bf400769cc1400f3258cd31387574077f301b421bc84df7266c44e9e6d569fc56be00812904767bf5ccd1fc7f", want, 64);
    assert(!memcmp(e.pub, want, 64));
    ok_keccak256(e.pub, 64, id); /* node id from EIP-778 */
    hex("a448f24c6d18e575453db13171562b71999873db5b286df957af199ec94617f7", want, 32);
    assert(!memcmp(id, want, 32));

    assert(ok_enr_parse_text(SEPOLIA, strlen(SEPOLIA), &e) == OK_OK);
    assert(e.seq == 1781647070086ull && !memcmp(e.ip, "\x9d\xb4\x6a\xe2", 4) && e.tcp == 30304 && e.udp == 30304);
    assert(e.has_eth && !memcmp(e.fork_hash, "\x6c\x1d\x94\x23", 4) && e.fork_next == 0);
    hex("e2b4b288b74640d380d71caaa1db9ee6db463d325e403b8b221cb453354f71ce2841aa64bc3e45479c5d08dfd51d414cf5c774bb1a2fca63891afc385328ac88", want, 64);
    assert(!memcmp(e.pub, want, 64));

    /* Any changed character breaks the signature or the encoding. */
    for (n = 4; n < strlen(SEPOLIA); n += 3) {
        memcpy(bad, SEPOLIA, sizeof bad);
        bad[n] = bad[n] == 'A' ? 'B' : 'A';
        assert(ok_enr_parse_text(bad, strlen(bad), &e) != OK_OK);
    }
    assert(ok_enr_parse_text(SEPOLIA + 1, strlen(SEPOLIA) - 1, &e) == OK_FORMAT);

    /* base64url: unpadded, URL alphabet, no stray bits. */
    assert(ok_base64url_decode("_-8", 3, buf, sizeof buf, &n) == OK_OK && n == 2 && buf[0] == 0xff && buf[1] == 0xef);
    assert(ok_base64url_decode("_-9", 3, buf, sizeof buf, &n) == OK_FORMAT);
    assert(ok_base64url_decode("ab+c", 4, buf, sizeof buf, &n) == OK_FORMAT);
    assert(ok_base64url_decode("abcde", 5, buf, sizeof buf, &n) == OK_FORMAT);
    assert(ok_base64url_decode("abcdefgh", 8, buf, 5, &n) == OK_RANGE);
    puts("enr: EIP-778 example and a live Sepolia record verify; tampering is refused");
    return 0;
}
