#include "okupa_dnsdisc.h"
#include "okupa_enr.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "secp256k1.h"
#include "secp256k1_recovery.h"
#include <string.h>

static const char base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int prefix(const char *txt, size_t n, const char *p)
{
    size_t k = strlen(p);
    return n >= k && !memcmp(txt, p, k);
}

static ok_result base32_decode(const char *in, size_t n, uint8_t *out, size_t want)
{
    uint32_t acc = 0;
    size_t i, o = 0;
    int bits = 0;
    for (i = 0; i < n; i++) {
        const char *at = memchr(base32, in[i], 32);
        if (!at || !in[i]) return OK_FORMAT;
        acc = acc << 5 | (uint32_t)(at - base32);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (o == want) return OK_FORMAT;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return o == want && !(acc & ((1u << bits) - 1)) ? OK_OK : OK_FORMAT;
}

static void base32_encode(const uint8_t *in, size_t n, char *out)
{
    uint32_t acc = 0;
    size_t i, o = 0;
    int bits = 0;
    for (i = 0; i < n; i++) {
        acc = acc << 8 | in[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = base32[(acc >> bits) & 31];
        }
    }
    if (bits) out[o++] = base32[(acc << (5 - bits)) & 31];
    out[o] = 0;
}

static int is_hash(const char *s, size_t n)
{
    size_t i;
    if (n != OK_DNSDISC_HASH) return 0;
    for (i = 0; i < n; i++)
        if (!s[i] || !memchr(base32, s[i], 32)) return 0;
    return 1;
}

ok_result ok_dnsdisc_url(const char *url, uint8_t key[33], char *domain, size_t cap)
{
    const char *at;
    size_t n;
    if (!url || !key || !domain) return OK_ARGUMENT;
    if (!prefix(url, strlen(url), "enrtree://") || !(at = strchr(url, '@'))) return OK_FORMAT;
    if (base32_decode(url + 10, (size_t)(at - url - 10), key, 33) != OK_OK) return OK_FORMAT;
    n = strlen(at + 1);
    if (n == 0 || n >= cap) return OK_RANGE;
    memcpy(domain, at + 1, n + 1);
    return OK_OK;
}

int ok_dnsdisc_kind(const char *txt, size_t n)
{
    if (prefix(txt, n, "enrtree-root:v1 ")) return OK_DNSDISC_ROOT;
    if (prefix(txt, n, "enrtree-branch:")) return OK_DNSDISC_BRANCH;
    if (prefix(txt, n, "enr:")) return OK_DNSDISC_ENR;
    if (prefix(txt, n, "enrtree://")) return OK_DNSDISC_LINK;
    return OK_DNSDISC_UNKNOWN;
}

/* Reads "name=value" at *at, where value runs to the next space or the end. */
static int field(const char *txt, size_t n, size_t *at, const char *name, const char **value, size_t *len)
{
    size_t k = strlen(name), end;
    if (*at + k > n || memcmp(txt + *at, name, k)) return 0;
    *value = txt + *at + k;
    for (end = *at + k; end < n && txt[end] != ' '; end++) {}
    *len = end - (*at + k);
    *at = end < n ? end + 1 : end;
    return 1;
}

ok_result ok_dnsdisc_root_parse(const char *txt, size_t n, const uint8_t key[33], ok_dnsdisc_root *root)
{
    const secp256k1_context *ctx = ok_secp();
    secp256k1_ecdsa_recoverable_signature sig;
    secp256k1_pubkey signer, tree;
    const char *e, *l, *s, *g;
    size_t at = 16, el, ll, sl, gl, i, len, signed_len;
    uint8_t rsv[65], digest[32], a[33], b[33];
    size_t alen = 33, blen = 33;
    if (!txt || !key || !root) return OK_ARGUMENT;
    if (!ctx) return OK_CRYPTO;
    if (ok_dnsdisc_kind(txt, n) != OK_DNSDISC_ROOT) return OK_FORMAT;
    if (!field(txt, n, &at, "e=", &e, &el) || !field(txt, n, &at, "l=", &l, &ll) ||
        !field(txt, n, &at, "seq=", &s, &sl))
        return OK_FORMAT;
    signed_len = at - 1; /* the signature covers the text before " sig=" */
    if (!field(txt, n, &at, "sig=", &g, &gl) || at != n || !is_hash(e, el) || !is_hash(l, ll) || sl == 0 || sl > 19)
        return OK_FORMAT;
    root->seq = 0;
    for (i = 0; i < sl; i++) {
        if (s[i] < '0' || s[i] > '9' || (i == 0 && s[i] == '0' && sl > 1)) return OK_FORMAT;
        root->seq = root->seq * 10 + (uint64_t)(s[i] - '0');
    }
    if (ok_base64url_decode(g, gl, rsv, sizeof rsv, &len) != OK_OK || len != 65) return OK_FORMAT;
    ok_keccak256(txt, signed_len, digest);
    if (rsv[64] > 3 || !secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig, rsv, rsv[64]) ||
        !secp256k1_ecdsa_recover(ctx, &signer, &sig, digest) || !secp256k1_ec_pubkey_parse(ctx, &tree, key, 33))
        return OK_CRYPTO;
    secp256k1_ec_pubkey_serialize(ctx, a, &alen, &signer, SECP256K1_EC_COMPRESSED);
    secp256k1_ec_pubkey_serialize(ctx, b, &blen, &tree, SECP256K1_EC_COMPRESSED);
    if (memcmp(a, b, 33)) return OK_CRYPTO;
    memcpy(root->enr_root, e, el);
    root->enr_root[el] = 0;
    memcpy(root->link_root, l, ll);
    root->link_root[ll] = 0;
    return OK_OK;
}

int ok_dnsdisc_matches(const char *hash, size_t hash_len, const char *txt, size_t n)
{
    uint8_t digest[32];
    char want[OK_DNSDISC_HASH + 1];
    if (!hash || !txt || !is_hash(hash, hash_len)) return 0;
    ok_keccak256(txt, n, digest);
    base32_encode(digest, 16, want);
    return !memcmp(want, hash, OK_DNSDISC_HASH);
}

ok_result ok_dnsdisc_branch_next(const char *txt, size_t n, size_t *pos, char child[OK_DNSDISC_HASH + 1])
{
    size_t start, end;
    if (!txt || !pos || !child) return OK_ARGUMENT;
    if (ok_dnsdisc_kind(txt, n) != OK_DNSDISC_BRANCH) return OK_FORMAT;
    start = *pos ? *pos : strlen("enrtree-branch:");
    if (start >= n) return OK_RANGE;
    for (end = start; end < n && txt[end] != ','; end++) {}
    if (!is_hash(txt + start, end - start)) return OK_FORMAT;
    memcpy(child, txt + start, OK_DNSDISC_HASH);
    child[OK_DNSDISC_HASH] = 0;
    *pos = end < n ? end + 1 : n;
    return OK_OK;
}
