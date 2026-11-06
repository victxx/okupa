#include "okupa_tech.h"
#include "okupa_session.h"
#include <stdio.h>
#include <string.h>

/* The technical screens. Top: what is happening, in plain words, with a
 * small moving diagram. Bottom: the real values from this console. */

#define W 256
#define H 192
#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define BG RGB(3, 3, 4)
#define PANEL RGB(6, 6, 8)
#define TEXT RGB(27, 27, 26)
#define DIM RGB(15, 15, 16)
#define ACCENT RGB(31, 22, 6)  /* amber */
#define GOOD RGB(10, 26, 12)
#define BAD RGB(29, 9, 7)
#define LINK RGB(12, 20, 31)
#define OG RGB(24, 12, 31)

typedef uint16_t *frame;

static void rect(frame f, int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < W && j >= 0 && j < H) f[j * W + i] = c;
}

static void disc(frame f, int cx, int cy, int r, uint16_t c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) rect(f, cx + x, cy + y, 1, 1, c);
}

static void text(frame f, const uint8_t (*font)[8], int x, int y, const char *s, uint16_t c, int scale)
{
    int row, col;
    if (!font) return;
    for (; *s && x < W; s++, x += 8 * scale) {
        unsigned ch = (unsigned char)*s & 127;
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (font[ch][row] >> col & 1) rect(f, x + col * scale, y + row * scale, scale, scale, c);
    }
}

static void centred(frame f, const uint8_t (*font)[8], int cx, int y, const char *s, uint16_t c)
{
    text(f, font, cx - (int)strlen(s) * 4, y, s, c, 1);
}

/* Paragraph broken at spaces into lines of 31 characters; returns the next y. */
static int para(frame f, const uint8_t (*font)[8], int x, int y, const char *s, uint16_t c, int max_lines)
{
    char line[33];
    int lines = 0;
    while (*s && lines < max_lines) {
        size_t n = strlen(s), cut = n > 31 ? 31 : n;
        if (n > 31) {
            while (cut > 0 && s[cut] != ' ') cut--;
            if (cut == 0) cut = 31;
        }
        memcpy(line, s, cut);
        line[cut] = 0;
        text(f, font, x, y, line, c, 1);
        s += cut;
        while (*s == ' ') s++;
        y += 10;
        lines++;
    }
    return y;
}

static void hexs(char *out, const uint8_t *b, size_t n)
{
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = 0;
}

/* "label    value" on one line of the bottom screen. */
static void kv(frame f, const uint8_t (*font)[8], int y, const char *label, const char *value, uint16_t c)
{
    text(f, font, 4, y, label, DIM, 1);
    text(f, font, 4 + 8 * (int)strlen(label) + 4, y, value, c, 1);
}

/* 32 bytes as two lines of 32 hex digits. */
static void hash2(frame f, const uint8_t (*font)[8], int y, const uint8_t h[32], uint16_t c)
{
    char a[33], b[33];
    hexs(a, h, 16);
    hexs(b, h + 16, 16);
    text(f, font, 0, y, a, c, 1);
    text(f, font, 0, y + 9, b, c, 1);
}

static void gwei(char *out, size_t cap, uint64_t wei)
{
    snprintf(out, cap, "%lu.%02lu gwei", (unsigned long)(wei / 1000000000ull),
             (unsigned long)(wei % 1000000000ull / 10000000ull));
}

/* ---- Diagram pieces --------------------------------------------------------- */

static void dsi(frame f, int x, int y)
{
    rect(f, x, y, 44, 30, RGB(22, 22, 23));
    rect(f, x + 4, y + 3, 36, 24, RGB(4, 8, 14));
    rect(f, x, y + 32, 44, 30, RGB(22, 22, 23));
    rect(f, x + 4, y + 35, 36, 24, RGB(4, 8, 14));
}

static const int node_x[6] = {176, 214, 238, 196, 230, 168}, node_y[6] = {132, 126, 150, 160, 178, 176};

static void nodes(frame f, int lit)
{
    int i, j;
    for (i = 0; i < 6; i++)
        for (j = i + 1; j < 6; j++)
            if ((i + j) % 3 == 0) {
                int k;
                for (k = 0; k <= 16; k++)
                    rect(f, node_x[i] + (node_x[j] - node_x[i]) * k / 16, node_y[i] + (node_y[j] - node_y[i]) * k / 16, 1,
                         1, DIM);
            }
    for (i = 0; i < 6; i++) disc(f, node_x[i], node_y[i], 5, i < lit ? ACCENT : DIM);
}

/* A packet travelling along a line, with its name above the middle. */
static void packet(frame f, const uint8_t (*font)[8], int x0, int y0, int x1, int y1, uint32_t ticks, const char *name,
                   uint16_t c)
{
    int k, t = (int)(ticks % 60);
    for (k = 0; k <= 40; k++) rect(f, x0 + (x1 - x0) * k / 40, y0 + (y1 - y0) * k / 40, 1, 1, RGB(9, 9, 10));
    rect(f, x0 + (x1 - x0) * t / 60 - 3, y0 + (y1 - y0) * t / 60 - 3, 7, 7, c);
    if (name) centred(f, font, (x0 + x1) / 2, (y0 + y1) / 2 - 14, name, c);
}

/* ---- The walk-through --------------------------------------------------------- */

static const char *const step_title[OK_TECH_STEPS] = {
    "A key from you", "Find the nodes", "Shake hands", "Say who we are", "Sign a letter",
    "Hand it over", "In a block?", "Ask 0G's mind", "What it skips"};

static const char *const step_text[OK_TECH_STEPS] = {
    "You scribble 480 strokes. Each touch and a 33 MHz timer go into keccak-256, giving a secp256k1 key. "
    "It is born on this SD card and never leaves it.",
    "No server says where Ethereum is. The DSi reads a node list signed in DNS (EIP-1459), checks every "
    "signature, then asks nodes over UDP who they know (discv4).",
    "RLPx, like every node: ECIES auth and ack with throwaway keys, then each frame is AES-256-CTR "
    "with a keccak MAC. Plain TCP from the DSi's Wi-Fi.",
    "Hello (devp2p 5, snappy), then eth/69 Status: chain id, genesis and the fork id, a CRC32 of every "
    "fork (EIP-2124). We keep no blocks and say so.",
    "An EIP-1559 transaction to itself, zero value, with an OKPA act in its data (a hello or a 64x64 "
    "drawing). Signed on the ARM9 with libsecp256k1.",
    "Public nodes are full: most say \"too many peers\" a moment after Status. The DSi hands the "
    "transaction over in that moment. The node gossips it on.",
    "While a node keeps us, the DSi asks for new headers and bodies, re-hashes each header and rebuilds "
    "the transactions trie. Our hash inside = it is in.",
    "The okupa's next move comes from a model on 0G Compute, over TLS 1.2 from the DSi. The reply is "
    "signed in a TEE; the DSi recovers the signer before acting.",
    "No sync, no EVM, no state: no balances, no consensus. A block is checked by hashing, not "
    "finalised. Everything else here runs on this DSi, in C."};

static void step_diagram(frame f, const uint8_t (*font)[8], const ok_tech *t, const ok_facts *fa)
{
    uint32_t k = t->ticks;
    dsi(f, 12, 124);
    switch (t->step) {
    case 0: {
        int i;
        for (i = 0; i < 24; i++) {
            int a = (int)((k + (uint32_t)i * 7) % 36);
            rect(f, 16 + a, 160 + (int)((i * 13 + k / 2) % 22), 2, 2, ACCENT);
        }
        packet(f, font, 60, 140, 150, 140, k, "keccak", ACCENT);
        text(f, font, 156, 136, fa->address[0] ? "key ok" : "no key yet", fa->address[0] ? GOOD : DIM, 1);
        break;
    }
    case 1:
        nodes(f, (int)(k / 20 % 7));
        packet(f, font, 60, 140, 168, 140, k, "TXT / UDP", LINK);
        break;
    case 2:
        nodes(f, 1);
        packet(f, font, 60, 136, 172, 132, k, "auth", ACCENT);
        packet(f, font, 172, 140, 60, 160, k + 30, "ack", LINK);
        break;
    case 3:
        nodes(f, 1);
        packet(f, font, 60, 136, 172, 132, k, "Hello+Status", ACCENT);
        break;
    case 4:
        rect(f, 70, 140, 60, 26, RGB(28, 26, 20));
        text(f, font, 74, 144, "0x02...", RGB(6, 5, 5), 1);
        text(f, font, 74, 154, "signed", RGB(6, 5, 5), 1);
        break;
    case 5: {
        int i, s = (int)(k % 90);
        nodes(f, 6);
        packet(f, font, 60, 140, 176, 132, k, "Transactions", ACCENT);
        for (i = 1; i < 6 && s > 30; i++)
            disc(f, 176 + (node_x[i] - 176) * (s - 30) / 60, 132 + (node_y[i] - 132) * (s - 30) / 60, 2, ACCENT);
        break;
    }
    case 6: {
        int i;
        nodes(f, 1);
        packet(f, font, 172, 132, 60, 140, k, "headers+bodies", LINK);
        for (i = 0; i < 3; i++) {
            rect(f, 92 + i * 22, 166, 18, 14, i == 2 && fa->incl_state == 2 ? GOOD : RGB(12, 12, 14));
            text(f, font, 93 + i * 22, 169, "#", TEXT, 1);
        }
        break;
    }
    case 7:
        rect(f, 190, 128, 50, 30, OG);
        text(f, font, 199, 138, "0G", TEXT, 2);
        packet(f, font, 60, 140, 190, 140, k, "TLS 1.2", OG);
        text(f, font, 150, 166, "TEE signature", fa->og_verified ? GOOD : DIM, 1);
        break;
    default:
        text(f, font, 70, 132, "x sync   x EVM", BAD, 1);
        text(f, font, 70, 146, "x state  x finality", BAD, 1);
        text(f, font, 70, 164, "+ keys + p2p + tx", GOOD, 1);
        text(f, font, 70, 176, "+ block check + 0G", GOOD, 1);
        break;
    }
}

static void step_data(frame f, const uint8_t (*font)[8], const ok_tech *t, const ok_facts *fa)
{
    char v[80];
    const ok_fact_peer *p = &fa->peers[0];
    int y = 40;
    text(f, font, 4, 24, "On this console:", ACCENT, 1);
    switch (t->step) {
    case 0:
        snprintf(v, sizeof v, "%d / 480", fa->scribble_samples);
        kv(f, font, y, "Strokes", fa->address[0] && !fa->scribble_samples ? "from the SD key" : v, TEXT);
        text(f, font, 4, y + 14, "Address:", DIM, 1);
        text(f, font, 4, y + 26, fa->address[0] ? fa->address : "none yet (Dev > Wallet)", fa->address[0] ? TEXT : DIM, 1);
        break;
    case 1:
        snprintf(v, sizeof v, "%s", fa->dns_verified ? "signature ok" : "not read yet");
        kv(f, font, y, "DNS root", v, fa->dns_verified ? GOOD : DIM);
        snprintf(v, sizeof v, "%d records, %d nodes", fa->dns_records, fa->dns_nodes);
        kv(f, font, y + 12, "DNS", v, TEXT);
        snprintf(v, sizeof v, "%d out, %d in", fa->v4_sent, fa->v4_received);
        kv(f, font, y + 24, "discv4", v, TEXT);
        snprintf(v, sizeof v, "%d known", fa->v4_nodes);
        kv(f, font, y + 36, "Table", v, TEXT);
        break;
    case 2:
        if (!fa->peer_count) {
            text(f, font, 4, y, "No node dialled yet", DIM, 1);
            break;
        }
        snprintf(v, sizeof v, "%u.%u.%u.%u:%u", p->ip[0], p->ip[1], p->ip[2], p->ip[3], p->port);
        kv(f, font, y, "Node", v, TEXT);
        snprintf(v, sizeof v, "%lu ms", (unsigned long)p->ms_tcp);
        kv(f, font, y + 12, "TCP", v, TEXT);
        snprintf(v, sizeof v, p->ms_rlpx ? "%lu ms" : "-", (unsigned long)p->ms_rlpx);
        kv(f, font, y + 24, "auth+ack", v, TEXT);
        snprintf(v, sizeof v, "%lu in, %lu out", (unsigned long)fa->bytes_in, (unsigned long)fa->bytes_out);
        kv(f, font, y + 36, "Bytes", v, TEXT);
        break;
    case 3: {
        int i, shown = 0;
        for (i = 0; i < fa->peer_count && !shown; i++)
            if (fa->peers[i].stage >= OK_STAGE_HELLO) {
                text(f, font, 4, y, "Last node's client:", DIM, 1);
                text(f, font, 4, y + 12, fa->peers[i].client, TEXT, 1);
                snprintf(v, sizeof v, "%lu", (unsigned long)fa->peers[i].latest);
                kv(f, font, y + 26, "Its head", fa->peers[i].latest ? v : "-", TEXT);
                shown = 1;
            }
        if (!shown) text(f, font, 4, y, "No Hello yet", DIM, 1);
        snprintf(v, sizeof v, "%02x%02x%02x%02x", fa->fork_hash[0], fa->fork_hash[1], fa->fork_hash[2], fa->fork_hash[3]);
        kv(f, font, y + 40, "Our fork id", v, TEXT);
        break;
    }
    case 4:
        if (!fa->tx_len) {
            text(f, font, 4, y, "Nothing signed yet:", DIM, 1);
            text(f, font, 4, y + 12, "Dev > Wallet > Send hello", DIM, 1);
            text(f, font, 4, y + 24, "or Draw > Send", DIM, 1);
            break;
        }
        snprintf(v, sizeof v, "%s, nonce %lu", fa->tx_kind, (unsigned long)fa->tx_nonce);
        kv(f, font, y, "Act", v, TEXT);
        snprintf(v, sizeof v, "%lu.%03lu ms", (unsigned long)(fa->us_sign / 1000), (unsigned long)(fa->us_sign % 1000));
        kv(f, font, y + 12, "Signed in", v, GOOD);
        text(f, font, 4, y + 26, "Hash:", DIM, 1);
        hash2(f, font, y + 38, fa->tx_hash, TEXT);
        break;
    case 5:
        snprintf(v, sizeof v, "%d", fa->tx_handed);
        kv(f, font, y, "Handed to", v, fa->tx_handed ? GOOD : DIM);
        snprintf(v, sizeof v, "%d tried, %d full", fa->tried, fa->full);
        kv(f, font, y + 12, "Nodes", v, TEXT);
        snprintf(v, sizeof v, "%d", fa->reached_status);
        kv(f, font, y + 24, "Reached Status", v, TEXT);
        break;
    case 6:
        snprintf(v, sizeof v, "%lu", (unsigned long)fa->blocks_checked);
        kv(f, font, y, "Blocks checked", v, TEXT);
        if (fa->incl_state == 2) {
            snprintf(v, sizeof v, "%lu, tx %lu/%lu", (unsigned long)fa->incl_block, (unsigned long)fa->incl_index,
                     (unsigned long)fa->incl_block_txs);
            kv(f, font, y + 12, "In block", v, GOOD);
            kv(f, font, y + 24, "Trie root", "recomputed, matches", GOOD);
        } else {
            kv(f, font, y + 12, "Found", fa->incl_state == 3 ? "root MISMATCH" : "not yet", fa->incl_state == 3 ? BAD : DIM);
        }
        break;
    case 7:
        kv(f, font, y, "Model", fa->og_model[0] ? fa->og_model : "no decision yet", fa->og_model[0] ? TEXT : DIM);
        kv(f, font, y + 12, "TEE sig", fa->og_model[0] ? (fa->og_verified ? "verified on DSi" : "NOT verified") : "-",
           fa->og_verified ? GOOD : DIM);
        kv(f, font, y + 24, "Chose", fa->og_decision[0] ? fa->og_decision : "-", TEXT);
        break;
    default:
        kv(f, font, y, "CPU", fa->dsi_mode ? "ARM9 133 MHz (DSi)" : "ARM9 67 MHz (DS)", TEXT);
        kv(f, font, y + 12, "RAM", "16 MB", TEXT);
        kv(f, font, y + 24, "Language", "C99, no OS", TEXT);
        kv(f, font, y + 36, "Build", fa->build, TEXT);
        break;
    }
}

/* ---- The pages ---------------------------------------------------------------- */

static const char *const tab[OK_TECH_PAGES] = {"How", "ETH", "Find", "Peer", "Tx", "Blk", "0G", "DSi"};

static const char *const page_title[OK_TECH_PAGES] = {"", "Ethereum", "Discovery", "Peers", "Transaction",
                                                       "Inclusion", "0G mind", "The machine"};

static const char *const page_text[OK_TECH_PAGES] = {
    "",
    "The DSi speaks to Ethereum mainnet as a peer, not through an RPC. It knows the chain by its id, its "
    "genesis hash and its fork id. It keeps no blocks: its Status says earliest = latest = genesis.",
    "Where the nodes come from, with no server of ours: a DNS tree signed by the Ethereum Foundation "
    "(every record checked against the root's key) and discv4, Ethereum's own UDP table.",
    "The last nodes dialled and how far each got: TCP, auth, ack (RLPx), Hello, Status, our "
    "transaction, held. 'full' is their 'too many peers'. Client names come from their Hello.",
    "The last transaction signed on this console, byte for byte: type 2 (EIP-1559), zero value, to "
    "itself. Its hash is keccak-256 of these bytes.",
    "Checked by the DSi itself over P2P: block headers re-hashed, the body's transactions put back into "
    "a Merkle-Patricia trie whose root must equal the header's. Proof of inclusion in that header, not finality.",
    "The okupa's mind is a model on 0G Compute. The DSi calls it over TLS 1.2 (BearSSL, pinned roots), "
    "and checks the TEE's signature over the request and reply hashes before using the answer.",
    "A Nintendo DSi from 2009: ARM9 at 133 MHz, 16 MB of RAM, 802.11 Wi-Fi. Everything runs in C with "
    "no OS: keccak, secp256k1, RLPx, snappy, the trie, TLS. These checks ran here, just now."};

static void stage_bar(frame f, int x, int y, int stage)
{
    int i;
    for (i = OK_STAGE_TCP; i <= OK_STAGE_HELD; i++)
        rect(f, x + (i - 1) * 7, y, 6, 6, i <= stage ? (i >= OK_STAGE_STATUS ? GOOD : ACCENT) : RGB(9, 9, 10));
}

static void page_data(frame f, const uint8_t (*font)[8], const ok_tech *t, const ok_facts *fa)
{
    char v[80];
    int y = 22, i;
    switch (t->page) {
    case OK_TECH_NET:
        snprintf(v, sizeof v, "%s, chain %lu", fa->network[0] ? fa->network : "?", (unsigned long)fa->chain_id);
        kv(f, font, y, "Network", v, TEXT);
        text(f, font, 4, y + 14, "Genesis:", DIM, 1);
        hash2(f, font, y + 26, fa->genesis, TEXT);
        snprintf(v, sizeof v, "%02x%02x%02x%02x next %lu", fa->fork_hash[0], fa->fork_hash[1], fa->fork_hash[2],
                 fa->fork_hash[3], (unsigned long)fa->fork_next);
        kv(f, font, y + 50, "Fork id", v, TEXT);
        snprintf(v, sizeof v, "%lu", (unsigned long)fa->best_head);
        kv(f, font, y + 64, "Head seen", fa->best_head ? v : "-", TEXT);
        if (fa->best_head) hash2(f, font, y + 76, fa->best_head_hash, DIM);
        kv(f, font, y + 100, "Our range", "genesis..genesis", DIM);
        break;
    case OK_TECH_FIND:
        text(f, font, 4, y, "DNS tree:", DIM, 1);
        para(f, font, 4, y + 12, fa->dns_tree[0] ? fa->dns_tree : "-", TEXT, 2);
        snprintf(v, sizeof v, "%lu %s", (unsigned long)fa->dns_seq, fa->dns_verified ? "(signed root ok)" : "");
        kv(f, font, y + 36, "Root seq", fa->dns_seq ? v : "-", fa->dns_verified ? GOOD : DIM);
        snprintf(v, sizeof v, "%d records, %d nodes", fa->dns_records, fa->dns_nodes);
        kv(f, font, y + 48, "Walked", v, TEXT);
        snprintf(v, sizeof v, "%d sent, %d received", fa->v4_sent, fa->v4_received);
        kv(f, font, y + 64, "discv4", v, TEXT);
        snprintf(v, sizeof v, "%d", fa->v4_nodes);
        kv(f, font, y + 76, "In table", v, TEXT);
        snprintf(v, sizeof v, "%d cand, %d tried", fa->candidates, fa->tried);
        kv(f, font, y + 92, "Dials", v, TEXT);
        snprintf(v, sizeof v, "%d Status, %d full", fa->reached_status, fa->full);
        kv(f, font, y + 104, "Result", v, TEXT);
        break;
    case OK_TECH_PEERS:
        text(f, font, 4, y, "node           T a A H S x h", DIM, 1);
        for (i = 0; i < fa->peer_count && i < OK_FACTS_PEERS; i++) {
            const ok_fact_peer *p = &fa->peers[i];
            int yy = y + 12 + i * 18;
            snprintf(v, sizeof v, "%u.%u.%u.%u", p->ip[0], p->ip[1], p->ip[2], p->ip[3]);
            text(f, font, 4, yy, v, TEXT, 1);
            stage_bar(f, 124, yy + 1, p->stage);
            text(f, font, 176, yy, p->disconnect == 4 ? "full" : p->disconnect >= 0 ? "left" : "", p->disconnect == 4 ? BAD : DIM, 1);
            snprintf(v, sizeof v, "%.26s", p->client[0] ? p->client : "");
            text(f, font, 12, yy + 9, v, DIM, 1);
        }
        if (!fa->peer_count) text(f, font, 4, y + 14, "No node dialled yet", DIM, 1);
        break;
    case OK_TECH_TX:
        if (!fa->tx_len) {
            text(f, font, 4, y, "Nothing signed yet", DIM, 1);
            break;
        }
        snprintf(v, sizeof v, "%s, %lu bytes", fa->tx_kind, (unsigned long)fa->tx_len);
        kv(f, font, y, "Type 2", v, TEXT);
        snprintf(v, sizeof v, "%lu, gas %lu", (unsigned long)fa->tx_nonce, (unsigned long)fa->tx_gas);
        kv(f, font, y + 11, "Nonce", v, TEXT);
        gwei(v, sizeof v, fa->tx_max_fee);
        kv(f, font, y + 22, "Max fee", v, TEXT);
        snprintf(v, sizeof v, "%d", fa->tx_v);
        kv(f, font, y + 33, "y parity", v, TEXT);
        {
            int line, first = t->scroll * 16;
            char hx[33];
            text(f, font, 4, y + 46, "Raw bytes (left/right):", DIM, 1);
            for (line = 0; line < 9; line++) {
                int at = first + line * 16, n = (int)fa->tx_len - at;
                if (n <= 0) break;
                hexs(hx, fa->tx + at, (size_t)(n > 16 ? 16 : n));
                text(f, font, 0, y + 58 + line * 9, hx, line & 1 ? DIM : TEXT, 1);
            }
        }
        break;
    case OK_TECH_BLOCK:
        snprintf(v, sizeof v, "%lu", (unsigned long)fa->blocks_checked);
        kv(f, font, y, "Blocks checked", v, TEXT);
        if (fa->incl_state != 2 && fa->incl_state != 3) {
            kv(f, font, y + 14, "Our tx", fa->incl_state == 1 ? "watching new blocks" : "nothing to watch", DIM);
            break;
        }
        snprintf(v, sizeof v, "%lu", (unsigned long)fa->incl_block);
        kv(f, font, y + 14, "Block", v, TEXT);
        text(f, font, 4, y + 26, "Header hash (re-hashed here):", DIM, 1);
        hash2(f, font, y + 38, fa->incl_block_hash, TEXT);
        text(f, font, 4, y + 60, "Tx root (rebuilt here):", DIM, 1);
        hash2(f, font, y + 72, fa->incl_tx_root, fa->incl_root_ok ? GOOD : BAD);
        snprintf(v, sizeof v, "%lu of %lu", (unsigned long)fa->incl_index, (unsigned long)fa->incl_block_txs);
        kv(f, font, y + 96, "Our tx index", v, TEXT);
        kv(f, font, y + 108, "Root", fa->incl_root_ok ? "MATCHES header" : "DOES NOT match", fa->incl_root_ok ? GOOD : BAD);
        break;
    case OK_TECH_0G:
        if (!fa->og_model[0]) {
            text(f, font, 4, y, "No 0G decision yet", DIM, 1);
            break;
        }
        kv(f, font, y, "Host", fa->og_host, TEXT);
        kv(f, font, y + 11, "Model", fa->og_model, TEXT);
        kv(f, font, y + 22, "TLS", fa->og_tls, TEXT);
        text(f, font, 4, y + 34, "TEE signer (recovered):", DIM, 1);
        text(f, font, 4, y + 45, fa->og_signer, fa->og_verified ? GOOD : BAD, 1);
        text(f, font, 4, y + 58, "sha256(request):", DIM, 1);
        hash2(f, font, y + 69, fa->og_req_sha, TEXT);
        text(f, font, 4, y + 89, "sha256(reply):", DIM, 1);
        hash2(f, font, y + 100, fa->og_resp_sha, TEXT);
        snprintf(v, sizeof v, "%s, %lu ms", fa->og_decision, (unsigned long)fa->og_ms);
        kv(f, font, y + 122, "Chose", v, TEXT);
        break;
    default:
        kv(f, font, y, "CPU", fa->dsi_mode ? "ARM9 133 MHz" : "ARM9 67 MHz", TEXT);
        snprintf(v, sizeof v, "%lu KB used", (unsigned long)(fa->heap_used / 1024));
        kv(f, font, y + 11, "Heap", v, TEXT);
        snprintf(v, sizeof v, "%u.%u.%u.%u", fa->ip[0], fa->ip[1], fa->ip[2], fa->ip[3]);
        kv(f, font, y + 22, "Wi-Fi", fa->ip[0] ? v : "offline", TEXT);
        kv(f, font, y + 33, "Build", fa->build, TEXT);
        for (i = 0; i < fa->checks_n && i < 10; i++) text(f, font, 0, y + 48 + i * 10, fa->checks[i], GOOD, 1);
        if (!fa->checks_n) text(f, font, 4, y + 48, "checks not run yet", DIM, 1);
        break;
    }
}

/* ---- Frame --------------------------------------------------------------------- */

#define TAB_W 32
#define BAR_Y 174

void ok_tech_init(ok_tech *t)
{
    memset(t, 0, sizeof *t);
}

void ok_tech_page(ok_tech *t, int delta)
{
    t->page = (t->page + delta + OK_TECH_PAGES) % OK_TECH_PAGES;
    t->scroll = 0;
}

void ok_tech_step(ok_tech *t, int delta)
{
    if (t->page == OK_TECH_HOW) {
        t->step += delta;
        if (t->step < 0) t->step = 0;
        if (t->step >= OK_TECH_STEPS) t->step = OK_TECH_STEPS - 1;
    } else if (t->page == OK_TECH_TX) {
        t->scroll += delta;
        if (t->scroll < 0) t->scroll = 0;
        if (t->scroll > 64) t->scroll = 64;
    } else {
        ok_tech_page(t, delta);
    }
}

int ok_tech_touch(ok_tech *t, int x, int y)
{
    if (y < 16) {
        int p = x / TAB_W;
        if (p >= 0 && p < OK_TECH_PAGES && p != t->page) {
            t->page = p;
            t->scroll = 0;
            return 1;
        }
        return 0;
    }
    if (y >= BAR_Y) {
        if (x < 80) ok_tech_step(t, -1);
        else if (x >= 176) ok_tech_step(t, +1);
        else return 0;
        return 1;
    }
    return 0;
}

void ok_tech_render(const ok_tech *t, const ok_facts *fa, const uint8_t (*font)[8], uint16_t *top, uint16_t *bottom)
{
    char v[40];
    int i;
    rect(top, 0, 0, W, H, BG);
    rect(bottom, 0, 0, W, H, BG);
    /* Tabs. */
    for (i = 0; i < OK_TECH_PAGES; i++) {
        rect(bottom, i * TAB_W, 0, TAB_W - 1, 15, i == t->page ? ACCENT : PANEL);
        text(bottom, font, i * TAB_W + (TAB_W - 8 * (int)strlen(tab[i])) / 2, 4, tab[i], i == t->page ? BG : TEXT, 1);
    }
    /* Bottom bar. */
    rect(bottom, 0, BAR_Y, W, H - BAR_Y, PANEL);
    text(bottom, font, 6, BAR_Y + 6, "< Back", TEXT, 1);
    text(bottom, font, W - 6 - 48, BAR_Y + 6, "Next >", TEXT, 1);
    centred(bottom, font, 128, BAR_Y + 6, "SELECT", DIM);

    if (t->page == OK_TECH_HOW) {
        snprintf(v, sizeof v, "%d/%d", t->step + 1, OK_TECH_STEPS);
        text(top, font, 4, 4, v, DIM, 1);
        text(top, font, 4, 16, step_title[t->step], ACCENT, 2);
        para(top, font, 4, 38, step_text[t->step], TEXT, 8);
        step_diagram(top, font, t, fa);
        step_data(bottom, font, t, fa);
        return;
    }
    text(top, font, 4, 6, page_title[t->page], ACCENT, 2);
    para(top, font, 4, 30, page_text[t->page], TEXT, 15);
    page_data(bottom, font, t, fa);
}
