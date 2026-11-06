#include "okupa_block.h"
#include "okupa_keccak.h"
#include <stdlib.h>
#include <string.h>

/* ---- Header ---------------------------------------------------------------- */

static ok_result fixed(const ok_rlp_item *it, uint8_t *out, size_t n)
{
    if (it->list || it->len != n) return OK_FORMAT;
    memcpy(out, it->data, n);
    return OK_OK;
}

ok_result ok_header_read(const uint8_t *rlp, size_t n, ok_header *h)
{
    ok_rlp_item list, it;
    size_t pos = 0;
    int i;
    ok_result r;
    if (!rlp || !h) return OK_ARGUMENT;
    memset(h, 0, sizeof *h);
    if ((r = ok_rlp_decode(rlp, n, &list)) != OK_OK) return r;
    if (!list.list) return OK_FORMAT;
    for (i = 0; i <= 11; i++) {
        if (ok_rlp_next(&list, &pos, &it) != OK_OK) return OK_FORMAT;
        if (i == 0 && (r = fixed(&it, h->parent, 32)) != OK_OK) return r;
        if (i == 4 && (r = fixed(&it, h->tx_root, 32)) != OK_OK) return r;
        if (i == 8 && (r = ok_rlp_to_uint(&it, &h->number)) != OK_OK) return r;
        if (i == 11 && (r = ok_rlp_to_uint(&it, &h->time)) != OK_OK) return r;
    }
    ok_keccak256(rlp, n, h->hash);
    return OK_OK;
}

/* ---- Ordered trie root ---------------------------------------------------- */

typedef struct {
    uint8_t key[3], klen;
    const uint8_t *v;
    size_t vlen;
} leaf;

static leaf leaves[OK_BLOCK_MAX_TXS];

static int nib(const leaf *l, int d)
{
    return d & 1 ? l->key[d / 2] & 15 : l->key[d / 2] >> 4;
}

static int cmp(const void *a, const void *b)
{
    const leaf *x = a, *y = b;
    int c = memcmp(x->key, y->key, x->klen < y->klen ? x->klen : y->klen);
    return c ? c : (int)x->klen - (int)y->klen;
}

/* RLP header for a payload of n bytes; base 0x80 string, 0xc0 list. */
static size_t head(uint8_t *out, uint8_t base, size_t n)
{
    size_t k = 0, t;
    if (n < 56) {
        out[0] = (uint8_t)(base + n);
        return 1;
    }
    for (t = n; t; t >>= 8) k++;
    out[0] = (uint8_t)(base + 55 + k);
    for (t = 0; t < k; t++) out[1 + t] = (uint8_t)(n >> (8 * (k - 1 - t)));
    return 1 + k;
}

/* Hex-prefix encoding of nibbles [from, to) of a key. */
static size_t hex_prefix(const leaf *l, int from, int to, int is_leaf, uint8_t out[8])
{
    int n = to - from, i, o = 0;
    size_t len = 0;
    uint8_t flag = (uint8_t)((is_leaf ? 2 : 0) + (n & 1));
    if (n & 1) {
        out[len++] = (uint8_t)(flag << 4 | nib(l, from));
        o = 1;
    } else {
        out[len++] = (uint8_t)(flag << 4);
    }
    for (i = from + o; i < to; i += 2) out[len++] = (uint8_t)(nib(l, i) << 4 | nib(l, i + 1));
    return len;
}

/* A short byte string as an RLP item. */
static size_t rlp_str(uint8_t *out, const uint8_t *s, size_t n)
{
    size_t h;
    if (n == 1 && s[0] < 0x80) {
        out[0] = s[0];
        return 1;
    }
    h = head(out, 0x80, n);
    memcpy(out + h, s, n);
    return h + n;
}

/* Turns a node's RLP into how its parent refers to it: inline below 32
 * bytes, else 0xa0 || keccak. The root is always hashed. */
static size_t reference(const uint8_t *node, size_t n, int force, uint8_t out[33])
{
    if (n < 32 && !force) {
        memcpy(out, node, n);
        return n;
    }
    out[0] = 0xa0;
    ok_keccak256(node, n, out + 1);
    return 33;
}

static size_t node_ref(int lo, int hi, int depth, int force, uint8_t out[33])
{
    uint8_t buf[17 * 33 + 8], hp[8], hdr[9];
    size_t n = 0, h;
    int i, p;
    if (hi - lo == 1) {
        /* Leaf: [hp(rest of key), value]. Values (transactions) are long,
         * so hash them in place instead of copying. */
        const leaf *l = &leaves[lo];
        uint8_t vh[9], hps[9];
        size_t hpn = hex_prefix(l, depth, 2 * l->klen, 1, hp), hpsn = rlp_str(hps, hp, hpn), vhn, total;
        ok_keccak k;
        if (l->vlen == 1 && l->v[0] < 0x80) vhn = 0;
        else vhn = head(vh, 0x80, l->vlen);
        total = hpsn + vhn + l->vlen;
        h = head(hdr, 0xc0, total);
        if (h + total < 32 && !force) {
            memcpy(out, hdr, h);
            memcpy(out + h, hps, hpsn);
            memcpy(out + h + hpsn, vh, vhn);
            memcpy(out + h + hpsn + vhn, l->v, l->vlen);
            return h + total;
        }
        if (ok_yield && lo % 32 == 31) ok_yield();
        ok_keccak_init(&k);
        ok_keccak_update(&k, hdr, h);
        ok_keccak_update(&k, hps, hpsn);
        ok_keccak_update(&k, vh, vhn);
        ok_keccak_update(&k, l->v, l->vlen);
        out[0] = 0xa0;
        ok_keccak_final(&k, out + 1);
        return 33;
    }
    /* Shared nibbles (the keys are sorted, so first and last bound them). */
    for (p = 0; depth + p < 2 * leaves[lo].klen && depth + p < 2 * leaves[hi - 1].klen &&
                nib(&leaves[lo], depth + p) == nib(&leaves[hi - 1], depth + p);
         p++)
        ;
    if (p > 0) {
        uint8_t child[33];
        size_t cn = node_ref(lo, hi, depth + p, 0, child), hpn = hex_prefix(&leaves[lo], depth, depth + p, 0, hp);
        n = rlp_str(buf + 9, hp, hpn);
        memcpy(buf + 9 + n, child, cn);
        n += cn;
    } else {
        /* Branch: sixteen children and an empty value (no key here is a
         * prefix of another: rlp(index) keys never are). */
        int k = lo;
        for (i = 0; i < 16; i++) {
            int e = k;
            while (e < hi && nib(&leaves[e], depth) == i) e++;
            if (e > k) n += node_ref(k, e, depth + 1, 0, buf + 9 + n);
            else buf[9 + n++] = 0x80;
            k = e;
        }
        buf[9 + n++] = 0x80;
    }
    h = head(hdr, 0xc0, n);
    memcpy(buf + 9 - h, hdr, h);
    return reference(buf + 9 - h, n + h, force, out);
}

ok_result ok_trie_ordered_root(const uint8_t *const *vals, const size_t *lens, size_t n, uint8_t root[32])
{
    uint8_t ref[33];
    size_t i;
    if (!root || (n && (!vals || !lens))) return OK_ARGUMENT;
    if (n > OK_BLOCK_MAX_TXS) return OK_RANGE;
    if (n == 0) {
        static const uint8_t empty = 0x80;
        ok_keccak256(&empty, 1, root);
        return OK_OK;
    }
    for (i = 0; i < n; i++) {
        leaf *l = &leaves[i];
        if (i == 0) {
            l->key[0] = 0x80;
            l->klen = 1;
        } else if (i < 128) {
            l->key[0] = (uint8_t)i;
            l->klen = 1;
        } else if (i < 256) {
            l->key[0] = 0x81;
            l->key[1] = (uint8_t)i;
            l->klen = 2;
        } else {
            l->key[0] = 0x82;
            l->key[1] = (uint8_t)(i >> 8);
            l->key[2] = (uint8_t)i;
            l->klen = 3;
        }
        l->v = vals[i];
        l->vlen = lens[i];
    }
    qsort(leaves, n, sizeof leaves[0], cmp);
    node_ref(0, (int)n, 0, 1, ref);
    memcpy(root, ref + 1, 32);
    return OK_OK;
}

/* ---- Bodies ------------------------------------------------------------------- */

static const uint8_t *tx_vals[OK_BLOCK_MAX_TXS];
static size_t tx_lens[OK_BLOCK_MAX_TXS];

ok_result ok_body_check_tx(const ok_rlp_item *body, const ok_header *h, const uint8_t want[32], ok_body_check *out)
{
    ok_rlp_item txs, tx;
    size_t pos = 0, tpos = 0;
    ok_result r;
    if (!body || !h || !out) return OK_ARGUMENT;
    memset(out, 0, sizeof *out);
    out->found = -1;
    if (!body->list || ok_rlp_next(body, &pos, &txs) != OK_OK || !txs.list) return OK_FORMAT;
    for (;;) {
        size_t before = tpos;
        uint8_t hash[32];
        if ((r = ok_rlp_next(&txs, &tpos, &tx)) != OK_OK) break;
        if (out->count >= OK_BLOCK_MAX_TXS) return OK_RANGE;
        if (tx.list) {
            /* A legacy transaction: its value is the whole RLP list. */
            tx_vals[out->count] = txs.data + before;
            tx_lens[out->count] = tpos - before;
        } else {
            tx_vals[out->count] = tx.data; /* type || payload */
            tx_lens[out->count] = tx.len;
        }
        ok_keccak256(tx_vals[out->count], tx_lens[out->count], hash);
        if (ok_yield && out->count % 32 == 31) ok_yield();
        if (want && out->found < 0 && !memcmp(hash, want, 32)) out->found = (int32_t)out->count;
        out->count++;
    }
    if (r != OK_RANGE) return r;
    if ((r = ok_trie_ordered_root(tx_vals, tx_lens, out->count, out->root)) != OK_OK) return r;
    out->root_ok = !memcmp(out->root, h->tx_root, 32);
    return OK_OK;
}

/* ---- Messages ------------------------------------------------------------------- */

ok_result ok_eth_get_headers(uint64_t request, uint64_t origin, uint64_t amount, uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t outer, inner;
    if (!out || !len) return OK_ARGUMENT;
    ok_rlp_init(&w, out, cap);
    outer = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, request);
    inner = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, origin);
    ok_rlp_uint(&w, amount);
    ok_rlp_uint(&w, 0); /* skip */
    ok_rlp_uint(&w, 0); /* ascending */
    ok_rlp_list_end(&w, inner);
    ok_rlp_list_end(&w, outer);
    return ok_rlp_done(&w, len);
}

ok_result ok_eth_get_bodies(uint64_t request, const uint8_t (*hashes)[32], size_t count, uint8_t *out, size_t cap,
                            size_t *len)
{
    ok_rlp_writer w;
    size_t outer, inner, i;
    if (!out || !len || (count && !hashes)) return OK_ARGUMENT;
    ok_rlp_init(&w, out, cap);
    outer = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, request);
    inner = ok_rlp_list_begin(&w);
    for (i = 0; i < count; i++) ok_rlp_bytes(&w, hashes[i], 32);
    ok_rlp_list_end(&w, inner);
    ok_rlp_list_end(&w, outer);
    return ok_rlp_done(&w, len);
}

ok_result ok_eth_response(const uint8_t *payload, size_t n, uint64_t *request, ok_rlp_item *list)
{
    ok_rlp_item outer, id;
    size_t pos = 0;
    ok_result r;
    if (!payload || !request || !list) return OK_ARGUMENT;
    if ((r = ok_rlp_decode(payload, n, &outer)) != OK_OK) return r;
    if (!outer.list || ok_rlp_next(&outer, &pos, &id) != OK_OK || ok_rlp_to_uint(&id, request) != OK_OK ||
        ok_rlp_next(&outer, &pos, list) != OK_OK || !list->list)
        return OK_FORMAT;
    return OK_OK;
}
