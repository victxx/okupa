#include "okupa_dnsdisc.h"
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
    puts("dnsdisc: Sepolia tree root verifies against the tree key; children match their hashes");
    return 0;
}
