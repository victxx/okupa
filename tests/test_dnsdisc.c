#include "okupa_dnsdisc.h"
#include "okupa_enr.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The Sepolia tree as published on 2026-11-06 (go-ethereum ships its URL). */
static const char URL[] = "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net";
static const char ROOT[] =
    "enrtree-root:v1 e=D3GY2TQYS5G6DTBQ57SVF7ZNPM l=FDXN3SN67NA5DKA4J2GOK7BVQI seq=1793955491 "
    "sig=obDuPLYH_Iv9MPjbfez98uow3bDw2D8LRKBZJY7Rrth57m8CYeeFI_iSBgMpfXBY6lfyG1_4M4BUykKB57yf7wE";
static const char BRANCH[] = "enrtree-branch:UBBGOSSVS2CC6RAVYSFSJAWXL4,3DIWJVTGM637X6K5O4SQ72NHHU";
static const char LEAF[] =
    "enr:-KO4QORz8HhEW8sRz67YdcNES-E02Ckuics5iq4CKWVuqPA7bpiNbMePwhXjLBoMNBpF4M_0e4nAkpu6JJbL_ve5aFSGAZ7ScNOGg2V0"
    "aMfGhGwdlCOAgmlkgnY0gmlwhJ20auKJc2VjcDI1NmsxoQLitLKIt0ZA04DXHKqh257m20Y9Ml5AO4siHLRTNU9xzoRzbmFwwIN0Y3CCdmCD"
    "dWRwgnZg";

/* A small tree of our own, signed with a test key, served from memory. */
static char tree[4][2][400];
static int served, nodes;

static int fake_lookup(void *ctx, const char *name, char *txt, size_t cap, size_t *len)
{
    int i;
    (void)ctx;
    served++;
    for (i = 0; i < 4; i++)
        if (!strcmp(tree[i][0], name)) {
            *len = strlen(tree[i][1]);
            assert(*len < cap);
            memcpy(txt, tree[i][1], *len + 1);
            return 1;
        }
    return 0;
}

static void fake_found(void *ctx, const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp)
{
    (void)ctx;
    (void)pub;
    assert(ip[0] == 157 && tcp == 30304);
    nodes++;
}

static void name_of(const char *txt, char *out)
{
    static const char b32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint8_t h[32];
    uint32_t acc = 0;
    int bits = 0, i, o = 0;
    ok_keccak256(txt, strlen(txt), h);
    for (i = 0; i < 16; i++) {
        acc = acc << 8 | h[i];
        bits += 8;
        while (bits >= 5) out[o++] = b32[acc >> (bits -= 5) & 31];
    }
    out[o++] = b32[acc << (5 - bits) & 31];
    out[o] = 0;
}

static void walk(void)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static ok_dnsdisc_walk w;
    secp256k1_ecdsa_recoverable_signature sig;
    uint8_t priv[32], pub33[33], digest[32], rs[65], fork[4] = {0x6c, 0x1d, 0x94, 0x23};
    char leaf_name[27], bad_name[27], branch_name[27], url[200], body[200], sigtxt[90];
    size_t n = 33;
    int recid, i, o = 0;
    secp256k1_pubkey pk;

    memset(priv, 0x77, 32);
    assert(secp256k1_ec_pubkey_create(ok_secp(), &pk, priv));
    secp256k1_ec_pubkey_serialize(ok_secp(), pub33, &n, &pk, SECP256K1_EC_COMPRESSED);
    /* enrtree://<base32 key>@test.okupa */
    {
        static const char b32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        uint32_t acc = 0;
        int bits = 0, k = 0;
        k = snprintf(url, sizeof url, "enrtree://");
        for (i = 0; i < 33; i++) {
            acc = acc << 8 | pub33[i];
            bits += 8;
            while (bits >= 5) url[k++] = b32[acc >> (bits -= 5) & 31];
        }
        if (bits) url[k++] = b32[acc << (5 - bits) & 31];
        snprintf(url + k, sizeof url - (size_t)k, "@test.okupa");
    }
    strcpy(tree[2][1], LEAF);
    name_of(LEAF, leaf_name);
    snprintf(tree[2][0], 400, "%s.test.okupa", leaf_name);
    strcpy(tree[3][1], LEAF);
    tree[3][1][40] = tree[3][1][40] == 'A' ? 'B' : 'A'; /* served under the real leaf's name of another branch child */
    strcpy(bad_name, "AAAAAAAAAAAAAAAAAAAAAAAAAA");
    snprintf(tree[3][0], 400, "%s.test.okupa", bad_name);
    snprintf(tree[1][1], 400, "enrtree-branch:%s,%s", bad_name, leaf_name);
    name_of(tree[1][1], branch_name);
    snprintf(tree[1][0], 400, "%s.test.okupa", branch_name);
    snprintf(body, sizeof body, "enrtree-root:v1 e=%s l=AAAAAAAAAAAAAAAAAAAAAAAAAA seq=7", branch_name);
    ok_keccak256(body, strlen(body), digest);
    assert(secp256k1_ecdsa_sign_recoverable(ok_secp(), &sig, digest, priv, NULL, NULL));
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ok_secp(), rs, &recid, &sig);
    rs[64] = (uint8_t)recid;
    for (i = 0; i < 65; i += 3) {
        uint32_t v = (uint32_t)rs[i] << 16 | (i + 1 < 65 ? (uint32_t)rs[i + 1] << 8 : 0) | (i + 2 < 65 ? rs[i + 2] : 0);
        sigtxt[o++] = b64[v >> 18 & 63];
        sigtxt[o++] = b64[v >> 12 & 63];
        if (i + 1 < 65) sigtxt[o++] = b64[v >> 6 & 63];
        if (i + 2 < 65) sigtxt[o++] = b64[v & 63];
    }
    sigtxt[o] = 0;
    strcpy(tree[0][0], "test.okupa");
    snprintf(tree[0][1], 400, "%s sig=%s", body, sigtxt);

    w.lookup = fake_lookup;
    w.found = fake_found;
    assert(ok_dnsdisc_walk_tree(&w, url, fork, 10) == 1 && nodes == 1);
    assert(w.seq == 7 && w.records == 1 && w.refused == 1 && served == 4);
    fork[0] ^= 1; /* nobody on that network */
    nodes = 0;
    assert(ok_dnsdisc_walk_tree(&w, url, fork, 10) == 0 && nodes == 0);
    url[12] = url[12] == 'A' ? 'B' : 'A'; /* another tree key: the root does not verify */
    assert(ok_dnsdisc_walk_tree(&w, url, fork, 10) == 0);
}

int main(void)
{
    uint8_t key[33];
    char domain[64], child[OK_DNSDISC_HASH + 1], bad[sizeof ROOT];
    ok_dnsdisc_root root;
    size_t pos = 0;

    assert(ok_dnsdisc_url(URL, key, domain, sizeof domain) == OK_OK);
    assert(!strcmp(domain, "all.sepolia.ethdisco.net") && key[0] == 0x02 && key[1] == 0x81 && key[32] == 0xf2);
    assert(ok_dnsdisc_url(URL, key, domain, 10) == OK_RANGE);
    assert(ok_dnsdisc_url("enrtree://AKA3@x", key, domain, sizeof domain) == OK_FORMAT);

    /* The root is signed by the tree key, and nothing else verifies. */
    assert(ok_dnsdisc_kind(ROOT, strlen(ROOT)) == OK_DNSDISC_ROOT);
    assert(ok_dnsdisc_root_parse(ROOT, strlen(ROOT), key, &root) == OK_OK);
    assert(!strcmp(root.enr_root, "D3GY2TQYS5G6DTBQ57SVF7ZNPM") && !strcmp(root.link_root, "FDXN3SN67NA5DKA4J2GOK7BVQI"));
    assert(root.seq == 1793955491u);
    memcpy(bad, ROOT, sizeof bad);
    bad[strlen("enrtree-root:v1 e=D")] = 'E'; /* another subtree */
    assert(ok_dnsdisc_root_parse(bad, strlen(bad), key, &root) == OK_CRYPTO);
    memcpy(bad, ROOT, sizeof bad);
    bad[strstr(bad, "seq=") - bad + 4] = '2'; /* another sequence number */
    assert(ok_dnsdisc_root_parse(bad, strlen(bad), key, &root) == OK_CRYPTO);
    key[5] ^= 1;
    assert(ok_dnsdisc_root_parse(ROOT, strlen(ROOT), key, &root) != OK_OK);
    key[5] ^= 1;
    assert(ok_dnsdisc_root_parse(ROOT, strlen(ROOT) - 1, key, &root) != OK_OK);

    /* Children are named by their own hash: the right text matches, an edited one does not. */
    assert(ok_dnsdisc_matches("D3GY2TQYS5G6DTBQ57SVF7ZNPM", 26, BRANCH, strlen(BRANCH)));
    assert(!ok_dnsdisc_matches("D3GY2TQYS5G6DTBQ57SVF7ZNPM", 26, BRANCH, strlen(BRANCH) - 1));
    assert(ok_dnsdisc_matches("QELVAHIVSOUEK3UZGS46A32EIU", 26, LEAF, strlen(LEAF)));
    assert(ok_dnsdisc_kind(LEAF, strlen(LEAF)) == OK_DNSDISC_ENR);
    assert(ok_dnsdisc_kind(URL, strlen(URL)) == OK_DNSDISC_LINK && ok_dnsdisc_kind("v=spf1", 6) == OK_DNSDISC_UNKNOWN);

    /* Branch children in order, then the end; malformed entries are refused. */
    assert(ok_dnsdisc_branch_next(BRANCH, strlen(BRANCH), &pos, child) == OK_OK && !strcmp(child, "UBBGOSSVS2CC6RAVYSFSJAWXL4"));
    assert(ok_dnsdisc_branch_next(BRANCH, strlen(BRANCH), &pos, child) == OK_OK && !strcmp(child, "3DIWJVTGM637X6K5O4SQ72NHHU"));
    assert(ok_dnsdisc_branch_next(BRANCH, strlen(BRANCH), &pos, child) == OK_RANGE);
    pos = 0;
    assert(ok_dnsdisc_branch_next("enrtree-branch:ABC,DEF", 22, &pos, child) == OK_FORMAT);
    pos = 0;
    assert(ok_dnsdisc_branch_next("enrtree-branch:", 15, &pos, child) == OK_RANGE);
    walk();
    puts("dnsdisc: Sepolia tree root verifies against the tree key; children match their hashes; a whole walk over a test tree");
    return 0;
}
