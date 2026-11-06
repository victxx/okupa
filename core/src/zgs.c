#include "okupa_zgs.h"
#include "okupa_keccak.h"
#include <stdio.h>
#include <string.h>

static uint32_t splits(size_t total, uint32_t unit) { return (uint32_t)((total - 1) / unit + 1); }

static uint32_t next_pow2(uint32_t x)
{
    uint32_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

/* computePaddedSize in the SDK: up to a sixteenth of the next power of two. */
static uint32_t padded(uint32_t chunks)
{
    uint32_t p2 = next_pow2(chunks), min;
    if (p2 == chunks) return chunks;
    min = p2 >= 16 ? p2 / 16 : 1;
    return splits(chunks, min) * min;
}

static void leaf(const uint8_t *data, size_t size, uint32_t i, uint8_t out[32])
{
    uint8_t chunk[OK_ZGS_CHUNK];
    size_t at = (size_t)i * OK_ZGS_CHUNK;
    memset(chunk, 0, sizeof chunk);
    if (at < size) memcpy(chunk, data + at, size - at < OK_ZGS_CHUNK ? size - at : OK_ZGS_CHUNK);
    ok_keccak256(chunk, sizeof chunk, out);
}

static void pair(const uint8_t l[32], const uint8_t r[32], uint8_t out[32])
{
    uint8_t both[64];
    memcpy(both, l, 32);
    memcpy(both + 32, r, 32);
    ok_keccak256(both, 64, out);
}

/* The SDK's MerkleTree.build over leaves [from, from + n): pairs from the
 * left, a lone last node goes up as it is; then repeatedly take two from the
 * front, push their parent at the back, and move a leftover front to the back.
 * work holds n hashes. */
static void root_of(const uint8_t *data, size_t size, uint32_t from, uint32_t n, uint8_t (*work)[32], uint8_t out[32])
{
    uint32_t q = 0, i, head = 0, count;
    static uint8_t a[32], b[32];
    for (i = 0; i < n; i += 2) {
        if (ok_yield && (i & 127) == 126) ok_yield();
        if (i == n - 1) {
            leaf(data, size, from + i, work[q++]);
        } else {
            leaf(data, size, from + i, a);
            leaf(data, size, from + i + 1, b);
            pair(a, b, work[q++]);
        }
    }
    /* work is a ring of n entries: head..head+count. */
    count = q;
    while (count > 1) {
        uint32_t level = count, m = count / 2, k;
        for (k = 0; k < m; k++) {
            uint8_t parent[32];
            pair(work[head % n], work[(head + 1) % n], parent);
            head += 2;
            memcpy(work[(head + count - 2) % n], parent, 32);
            count -= 1;
        }
        if (level % 2 == 1) { /* this level was odd: the one left at the front goes to the back */
            uint8_t first[32];
            memcpy(first, work[head % n], 32);
            head += 1;
            memcpy(work[(head + count - 1) % n], first, 32);
        }
    }
    memcpy(out, work[head % n], 32);
}

ok_result ok_zgs_file_hash(const uint8_t *data, size_t size, ok_zgs_file *f)
{
    static uint8_t work[OK_ZGS_SEGMENT_CHUNKS][32];
    uint32_t left, next, offset = 0;
    if (!data || !f || !size || size > OK_ZGS_MAX) return OK_ARGUMENT;
    memset(f, 0, sizeof *f);
    f->size = size;
    f->chunks = splits(size, OK_ZGS_CHUNK);
    f->padded_chunks = padded(f->chunks);
    if (f->padded_chunks > OK_ZGS_SEGMENT_CHUNKS) return OK_RANGE;
    /* One segment: the file root is the tree over all padded chunks. */
    root_of(data, size, 0, f->padded_chunks, work, f->root);
    /* The submission nodes: the padded chunks as powers of two, biggest first. */
    left = f->padded_chunks;
    next = next_pow2(f->chunks);
    while (left > 0 && f->node_count < OK_ZGS_NODES) {
        if (left >= next) {
            int h = 0;
            while ((1u << h) < next) h++;
            f->node_height[f->node_count] = (uint8_t)h;
            root_of(data, size, offset, next, work, f->node_root[f->node_count]);
            f->node_count++;
            offset += next;
            left -= next;
        }
        next /= 2;
    }
    return left ? OK_RANGE : OK_OK;
}

static void word(uint8_t *p, uint64_t v)
{
    int i;
    memset(p, 0, 32);
    for (i = 0; i < 8; i++) p[31 - i] = (uint8_t)(v >> (8 * i));
}

ok_result ok_zgs_submit_calldata(const ok_zgs_file *f, const uint8_t submitter[20], uint8_t *out, size_t cap,
                                 size_t *len)
{
    static const uint8_t selector[4] = {0xbc, 0x8c, 0x11, 0xf8}; /* submit(((uint256,bytes,(bytes32,uint256)[]),address)) */
    size_t need, o = 0;
    int i;
    if (!f || !submitter || !out || !len || !f->node_count) return OK_ARGUMENT;
    need = 4 + 32 * (1 + 2 + 3 + 1 + 1 + 2 * (size_t)f->node_count);
    if (cap < need) return OK_RANGE;
    memcpy(out, selector, 4);
    o = 4;
    word(out + o, 0x20), o += 32;               /* the Submission tuple, after its offset */
    word(out + o, 0x40), o += 32;               /* Submission.data: offset within the tuple */
    word(out + o, 0), memcpy(out + o + 12, submitter, 20), o += 32; /* Submission.submitter */
    word(out + o, f->size), o += 32;            /* data.length */
    word(out + o, 0x60), o += 32;               /* data.tags: offset */
    word(out + o, 0x80), o += 32;               /* data.nodes: offset (after the empty tags) */
    word(out + o, 0), o += 32;                  /* tags: length 0 */
    word(out + o, (uint64_t)f->node_count), o += 32;
    for (i = 0; i < f->node_count; i++) {
        memcpy(out + o, f->node_root[i], 32), o += 32;
        word(out + o, f->node_height[i]), o += 32;
    }
    *len = o;
    return OK_OK;
}

uint64_t ok_zgs_fee(const ok_zgs_file *f, uint64_t price)
{
    uint64_t sectors = 0;
    int i;
    for (i = 0; i < f->node_count; i++) sectors += 1ull << f->node_height[i];
    return sectors * price;
}

static void hex32(const uint8_t *in, char *out)
{
    static const char d[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[64] = 0;
}

ok_result ok_zgs_upload_body(const ok_zgs_file *f, const uint8_t *data, uint64_t tx_seq, char *out, size_t cap,
                             size_t *len)
{
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char root[65];
    size_t n = (size_t)f->chunks * OK_ZGS_CHUNK, i, o;
    int k;
    if (!f || !data || !out || !len || !f->size) return OK_ARGUMENT;
    if (cap < (n + 2) / 3 * 4 + 512) return OK_RANGE;
    hex32(f->root, root);
    k = snprintf(out, cap, "{\"jsonrpc\":\"2.0\",\"method\":\"zgs_uploadSegmentsByTxSeq\",\"params\":[[{\"root\":\"0x%s\",\"data\":\"",
                 root);
    if (k < 0) return OK_RANGE;
    o = (size_t)k;
    /* The data chunks, the last zero-padded to a whole chunk, in base64. */
    for (i = 0; i < n; i += 3) {
        uint8_t b[3];
        int j;
        uint32_t v;
        if (ok_yield && i % 6144 == 6141) ok_yield();
        for (j = 0; j < 3; j++) b[j] = i + (size_t)j < f->size ? data[i + (size_t)j] : 0;
        v = (uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2];
        out[o++] = a[v >> 18 & 63];
        out[o++] = a[v >> 12 & 63];
        out[o++] = i + 1 < n ? a[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? a[v & 63] : '=';
    }
    k = snprintf(out + o, cap - o, "\",\"index\":0,\"proof\":{\"lemma\":[\"0x%s\"],\"path\":[]},\"fileSize\":%lu}],%llu],\"id\":1}",
                 root, (unsigned long)f->size, (unsigned long long)tx_seq);
    if (k < 0 || o + (size_t)k >= cap) return OK_RANGE;
    *len = o + (size_t)k;
    return OK_OK;
}
