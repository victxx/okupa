#include "okupa_checks.h"
#include "okupa_ecdh.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_rlpx.h"
#include "okupa_sha256.h"
#include "okupa_tx.h"
#include <stdio.h>
#include <string.h>

static const uint8_t keccak_abc[32] = {
    0x4e, 0x03, 0x65, 0x7a, 0xea, 0x45, 0xa9, 0x4f, 0xc7, 0xd4, 0x7b, 0xa8, 0x26, 0xc8, 0xd6, 0x67,
    0xc0, 0xd1, 0xe6, 0xe3, 0x3a, 0x64, 0xa0, 0x36, 0xec, 0x44, 0xf5, 0x8f, 0xa1, 0x2d, 0x6c, 0x45};
static const uint8_t sha256_abc[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
/* The signed Sepolia hello from the tx test (eth-account 0.13.7), key 0x46 * 32. */
static const uint8_t tx_hash[32] = {
    0x52, 0x38, 0x1a, 0xfb, 0x4c, 0x8a, 0xaa, 0xf4, 0x12, 0x2b, 0xa0, 0x09, 0x6e, 0x30, 0x24, 0x5f,
    0x28, 0x76, 0x14, 0x58, 0xdc, 0x4d, 0x65, 0x8e, 0x98, 0x95, 0x5c, 0xfb, 0x3d, 0xc5, 0xa2, 0x68};
/* ECDH of key 0x01 * 32 with the public key of 0x46 * 32 (plain curve maths). */
static const uint8_t ecdh_x[32] = {
    0x37, 0x7c, 0xe3, 0x56, 0xbf, 0x12, 0x2a, 0x35, 0x5c, 0x1e, 0x9a, 0xcc, 0x36, 0x31, 0xd3, 0x18,
    0x22, 0xc8, 0x59, 0x4f, 0x83, 0xa7, 0x9b, 0xf6, 0x72, 0x28, 0x32, 0xbf, 0x8b, 0xa0, 0x06, 0x9f};

static int line(char lines[][33], int max, int *n, const char *name, int ok, uint32_t us)
{
    unsigned short ms = (unsigned short)(us / 1000 > 60000 ? 60000 : us / 1000);
    if (*n < max) snprintf(lines[(*n)++], 33, "%-16s %-4s %5hu ms", name, ok ? "ok" : "FAIL", ms);
    return !ok;
}

int ok_checks_run(char lines[][33], int max, uint32_t (*now_us)(void))
{
    static const uint8_t hello[] = {'O', 'K', 'P', 'A', 1, 0};
    static uint8_t out[512], packet[OK_RLPX_MAX_PACKET];
    uint8_t a[32], b[32], pub[64], pub_b[64], h[32];
    ok_rlpx_entropy e;
    ok_rlpx_remote r;
    ok_tx tx;
    size_t len;
    uint32_t t;
    int n = 0, fails = 0, ok, i;

    t = now_us();
    ok_keccak256("abc", 3, h);
    fails += line(lines, max, &n, "keccak256", !memcmp(h, keccak_abc, 32), now_us() - t);
    t = now_us();
    ok_sha256("abc", 3, h);
    fails += line(lines, max, &n, "sha256", !memcmp(h, sha256_abc, 32), now_us() - t);

    memset(a, 0x46, 32);
    t = now_us();
    ok = ok_keys_public(a, pub) == OK_OK;
    fails += line(lines, max, &n, "public key", ok, now_us() - t);

    memset(&tx, 0, sizeof tx);
    tx.chain_id = 11155111;
    tx.max_priority_fee = 1000000000;
    tx.max_fee = 30000000000ull;
    tx.gas_limit = 62300;
    memcpy(tx.to, "\x6e\x2c\x21\x07\x10\x50\x8f\xd1\x5f\x37\xb1\x2a\x2b\xeb\x31\x2e\xc4\xbe\xaf\x19", 20);
    tx.data = hello;
    tx.data_len = sizeof hello;
    t = now_us();
    ok = ok_tx_sign(&tx, a, out, sizeof out, &len, h) == OK_OK && len == 119 && !memcmp(h, tx_hash, 32);
    fails += line(lines, max, &n, "sign tx", ok, now_us() - t);

    memset(b, 0x01, 32);
    t = now_us();
    ok = ok_ecdh(b, pub, h) == OK_OK && !memcmp(h, ecdh_x, 32);
    fails += line(lines, max, &n, "ecdh", ok, now_us() - t);

    /* An RLPx auth to ourselves: two ECDHs, a signature and ECIES, then open it. */
    for (i = 0; i < 32; i++) {
        e.eph[i] = (uint8_t)(i + 1);
        e.nonce[i] = (uint8_t)(i * 3);
        e.ecies[i] = (uint8_t)(i + 7);
    }
    memset(e.iv, 0x3c, 16);
    ok = ok_keys_public(b, pub_b) == OK_OK;
    t = now_us();
    ok = ok && ok_rlpx_auth_write(a, pub_b, &e, packet, sizeof packet, &len) == OK_OK;
    fails += line(lines, max, &n, "rlpx auth write", ok, now_us() - t);
    t = now_us();
    ok = ok && ok_rlpx_auth_read(b, packet, len, &r) == OK_OK && !memcmp(r.pub, pub, 64);
    fails += line(lines, max, &n, "rlpx auth read", ok, now_us() - t);

    if (n < max) snprintf(lines[n++], 33, "%s", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails;
}
