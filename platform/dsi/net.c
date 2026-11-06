#include "net.h"
#include "okupa_facts.h"
#include "okupa_discv4.h"
#include "okupa_dns.h"
#include "okupa_dnsdisc.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_session.h"
#include <arpa/inet.h>
#include <dswifi9.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <nds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Everything here runs in a background cothread (the "postman") so the game
 * never waits for the network. Cothreads are cooperative: the game loop and
 * the postman only switch at yields, so plain shared variables are safe. */

/* The network the console lives on: mainnet unless network.txt on the SD
 * card says "sepolia". Each has its own signed DNS list, bootnodes and file
 * of peers that let us in. */
typedef struct {
    const char *name;
    const ok_eth_network *net;
    const char *tree;
    const char *const *boot;
    const int *boots;
} profile;
static const profile mainnet = {"mainnet", &ok_mainnet,
                                "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.mainnet.ethdisco.net",
                                ok_mainnet_bootnodes, &ok_mainnet_bootnode_count};
static const profile sepolia = {"sepolia", &ok_sepolia,
                                "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net",
                                ok_sepolia_bootnodes, &ok_sepolia_bootnode_count};
static const profile *net = &mainnet;
#define MAX_NODES 64 /* public nodes are often full: try many */
#define MAX_KNOWN 6  /* Wi-Fi networks remembered */
#define LINES 14

static char lines[LINES][33], summary[33] = "Network: starting";
static char log_path[48], peers_path[48], wifi_path[48], pending_path[48], nonce_path[48], from_path[48],
    included_path[48];
static uint8_t pending[1200], pending_hash[32];
static size_t pending_len;
static int handed;
static uint64_t watch_from; /* next block to look at for the pending transaction */
static int included_event; /* one-shot for the game: the pending transaction was seen in a block */
static uint8_t included_hash[32];
static uint64_t included_block;
static const uint8_t *pending_list[1] = {pending};
static volatile int ui_wifi; /* the player is scanning or connecting: stand back */
static struct in_addr dns_server;
static uint16_t dns_id = 0x4f4b;
static struct {
    uint8_t pub[64], ip[4];
    uint16_t tcp;
} nodes[MAX_NODES];
static int node_count;
static int ready_peers; /* remembered nodes that reached Status: they can take a transaction */
static struct {
    char ssid[33], password[64];
} known[MAX_KNOWN];
static int known_count;

/* Wait for the next frame while letting other threads run: dswifi's network
 * stack and the game loop both need the processor. */
static void next_frame(void)
{
    cothread_yield_irq(IRQ_VBLANK);
}

static void pause_seconds(int seconds)
{
    int frames;
    for (frames = 0; frames < seconds * 60; frames++) next_frame();
}

/* Network lines for the Dev screen; each also goes to a log on the SD card,
 * closed every time, so the last step before a crash can be read later. */
static void line(int i, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void line(int i, const char *fmt, ...)
{
    va_list ap;
    FILE *f;
    if (i < 0 || i >= LINES) return;
    va_start(ap, fmt);
    vsnprintf(lines[i], 33, fmt, ap);
    va_end(ap);
    if (log_path[0] && (f = fopen(log_path, "a"))) {
        fprintf(f, "%lu %s\n", (unsigned long)time(NULL), lines[i]);
        fclose(f);
    }
}

static void say(const char *text)
{
    snprintf(summary, sizeof summary, "%.32s", text);
}

static void load_known_networks(void);

void ok_dsi_net_files(const char *dir)
{
    char path[48], text[16] = {0};
    FILE *f;
    snprintf(path, sizeof path, "%snetwork.txt", dir);
    if ((f = fopen(path, "r"))) {
        if (fgets(text, sizeof text, f) && !strncmp(text, "sepolia", 7)) net = &sepolia;
        fclose(f);
    }
    snprintf(log_path, sizeof log_path, "%snet-log.txt", dir);
    snprintf(peers_path, sizeof peers_path, "%speers-%s.txt", dir, net->name);
    snprintf(wifi_path, sizeof wifi_path, "%swifi.txt", dir);
    snprintf(pending_path, sizeof pending_path, "%spending-%s.hex", dir, net->name);
    snprintf(nonce_path, sizeof nonce_path, "%snonce-%s.txt", dir, net->name);
    snprintf(from_path, sizeof from_path, "%swatch-%s.txt", dir, net->name);
    snprintf(included_path, sizeof included_path, "%sincluded-%s.txt", dir, net->name);
    snprintf(ok_facts_now.network, sizeof ok_facts_now.network, "%s", net->name);
    ok_facts_now.chain_id = net->net->network_id;
    memcpy(ok_facts_now.genesis, net->net->genesis, 32);
    snprintf(ok_facts_now.dns_tree, sizeof ok_facts_now.dns_tree, "%s", strchr(net->tree, '@') + 1);
    load_known_networks();
    /* A transaction still waiting from last time: same bytes, same hash. */
    if ((f = fopen(pending_path, "r"))) {
        char hex[2 * sizeof pending + 4];
        size_t n = 0;
        unsigned v;
        if (fgets(hex, sizeof hex, f))
            while (n < sizeof pending && sscanf(hex + 2 * n, "%2x", &v) == 1) pending[n++] = (uint8_t)v;
        fclose(f);
        if (n > 1) {
            pending_len = n;
            ok_keccak256(pending, n, pending_hash);
        }
    }
    if (pending_len && (f = fopen(from_path, "r"))) {
        unsigned long long v;
        if (fscanf(f, "%llu", &v) == 1) watch_from = v;
        fclose(f);
    }
}

static void save_watch_from(void)
{
    FILE *f = fopen(from_path, "w");
    if (!f) return;
    fprintf(f, "%llu\n", (unsigned long long)watch_from);
    fclose(f);
}

int ok_dsi_net_take_included(uint8_t hash[32], uint64_t *block)
{
    if (!included_event) return 0;
    included_event = 0;
    memcpy(hash, included_hash, 32);
    *block = included_block;
    return 1;
}

uint64_t ok_dsi_net_chain_id(void) { return net->net->network_id; }
int ok_dsi_net_handed(void) { return handed; }

int ok_dsi_net_pending(uint8_t hash[32])
{
    if (pending_len && hash) memcpy(hash, pending_hash, 32);
    return pending_len != 0;
}

uint64_t ok_dsi_net_take_nonce(void)
{
    unsigned long long nonce = 0;
    FILE *f = fopen(nonce_path, "r");
    if (f) {
        if (fscanf(f, "%llu", &nonce) != 1) nonce = 0;
        fclose(f);
    }
    if ((f = fopen(nonce_path, "w"))) {
        fprintf(f, "%llu\n", nonce + 1);
        fclose(f);
    }
    return nonce;
}

void ok_dsi_net_send(const uint8_t *tx, size_t n, const uint8_t hash[32])
{
    FILE *f;
    size_t i;
    if (n > sizeof pending) return;
    memcpy(pending, tx, n);
    memcpy(pending_hash, hash, 32);
    pending_len = n;
    handed = 0;
    watch_from = 0;
    remove(from_path);
    ok_facts_now.incl_state = 0;
    ok_facts_now.blocks_checked = 0;
    if ((f = fopen(pending_path, "w"))) {
        for (i = 0; i < n; i++) fprintf(f, "%02x", pending[i]);
        fprintf(f, "\n");
        fclose(f);
    }
}

const char *ok_dsi_net_name(void) { return net->name; }
const char *ok_dsi_net_summary(void) { return summary; }
const char (*ok_dsi_net_lines(void))[33] { return (const char (*)[33])lines; }

/* Entropy for handshake keys only (never wallet keys): the DSi has no random
 * generator, so we hash timer readings taken around network events, the clock
 * and the MAC address. Weak by design; enough for throwaway keys. */
static ok_keccak pool;
static uint32_t pool_counter;

static void stir(void)
{
    uint32_t t = cpuGetTiming();
    time_t now = time(NULL);
    ok_keccak_update(&pool, &t, sizeof t);
    ok_keccak_update(&pool, &now, sizeof now);
}

static void draw(uint8_t *out, size_t n)
{
    while (n) {
        ok_keccak k = pool;
        uint8_t h[32];
        size_t m = n < 32 ? n : 32;
        pool_counter++;
        stir();
        ok_keccak_update(&k, &pool_counter, sizeof pool_counter);
        ok_keccak_final(&k, h);
        memcpy(out, h, m);
        out += m;
        n -= m;
    }
}

static void draw_key(uint8_t key[32])
{
    uint8_t pub[64];
    do draw(key, 32);
    while (ok_keys_public(key, pub) != OK_OK);
}

/* ---- Wi-Fi ---------------------------------------------------------------- */

static int wifi_init(void)
{
    static int initialised;
    if (!initialised) initialised = Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE);
    return initialised;
}

static int online(void)
{
    return Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED && Wifi_GetIP() != 0 && Wifi_GetIP() != 0xffffffffu;
}

/* Up to 30 s for an address; report(seconds) twice a second. */
static int wait_online(void (*report)(int seconds))
{
    int frames;
    for (frames = 0; frames < 60 * 30; frames++) {
        int status = Wifi_AssocStatus();
        next_frame();
        stir();
        if (status == ASSOCSTATUS_CANNOTCONNECT) return 0;
        if (online()) return 1;
        if (frames % 30 == 0 && report) report(frames / 60);
    }
    return 0;
}

static void start_connect(const char *ssid, const char *password)
{
    Wifi_AccessPoint ap;
    memset(&ap, 0, sizeof ap);
    snprintf(ap.ssid, sizeof ap.ssid, "%s", ssid);
    ap.ssid_len = (u8)strlen(ap.ssid);
    if (password && password[0]) Wifi_ConnectSecureAP(&ap, password, strlen(password));
    else Wifi_ConnectOpenAP(&ap);
}

/* Remembered networks: pairs of lines (name, password) in plain text on the
 * SD card, like the console's own settings; most recent first. */
static void load_known_networks(void)
{
    char ssid[40], password[72];
    FILE *f;
    known_count = 0;
    if (!(f = fopen(wifi_path, "r"))) return;
    while (known_count < MAX_KNOWN && fgets(ssid, sizeof ssid, f)) {
        if (!fgets(password, sizeof password, f)) password[0] = 0;
        ssid[strcspn(ssid, "\r\n")] = 0;
        password[strcspn(password, "\r\n")] = 0;
        if (!ssid[0]) continue;
        snprintf(known[known_count].ssid, sizeof known[0].ssid, "%.32s", ssid);
        snprintf(known[known_count].password, sizeof known[0].password, "%.63s", password);
        known_count++;
    }
    fclose(f);
}

static void remember_network(const char *ssid, const char *password)
{
    int i, j = known_count < MAX_KNOWN ? known_count : MAX_KNOWN - 1;
    FILE *f;
    for (i = 0; i < known_count; i++)
        if (!strcmp(known[i].ssid, ssid)) j = i;
    for (i = j; i > 0; i--) known[i] = known[i - 1];
    snprintf(known[0].ssid, sizeof known[0].ssid, "%s", ssid);
    snprintf(known[0].password, sizeof known[0].password, "%s", password ? password : "");
    if (j == known_count && known_count < MAX_KNOWN) known_count++;
    if (!wifi_path[0] || !(f = fopen(wifi_path, "w"))) return;
    for (i = 0; i < known_count; i++) fprintf(f, "%s\n%s\n", known[i].ssid, known[i].password);
    fclose(f);
}

static int bars(int rssi)
{
    if (rssi > 0) return rssi > 192 ? 3 : rssi > 128 ? 2 : rssi > 64 ? 1 : 0; /* DS mode: 0..255 */
    return rssi >= -55 ? 3 : rssi >= -67 ? 2 : rssi >= -80 ? 1 : 0;              /* DSi mode: dBm */
}

/* Scans for about 3 s into found_ap[]; returns how many. */
static Wifi_AccessPoint found_ap[24];
static int scan(void)
{
    int frames, n, i;
    Wifi_EnableWifi();
    Wifi_ScanMode();
    for (frames = 0; frames < 60 * 3; frames++) next_frame();
    n = Wifi_GetNumAP();
    if (n > 24) n = 24;
    for (i = 0; i < n; i++) Wifi_GetAPData(i, &found_ap[i]);
    return n;
}

static void report_wifi(int seconds)
{
    char text[33];
    snprintf(text, sizeof text, "Wi-Fi: connecting %ds", seconds);
    say(text);
}

/* Joins the strongest remembered network in range, or else the console's own
 * saved settings. */
static int wifi_up(void)
{
    int n, i, k, best = -1, best_bars = -1;
    if (!wifi_init()) return 0;
    if (online()) return 1;
    if (known_count) {
        n = scan();
        for (i = 0; i < n; i++)
            for (k = 0; k < known_count; k++)
                if (!strcmp(found_ap[i].ssid, known[k].ssid) && bars(found_ap[i].rssi) > best_bars) {
                    best = k;
                    best_bars = bars(found_ap[i].rssi);
                }
    }
    Wifi_EnableWifi();
    if (best >= 0) {
        line(0, "Wi-Fi: joining %.17s", known[best].ssid);
        start_connect(known[best].ssid, known[best].password);
    } else {
        line(0, "Wi-Fi: console settings");
        Wifi_AutoConnect();
    }
    return wait_online(report_wifi);
}

int ok_dsi_wifi_scan(ok_ui_ap *aps, int max, char status[33], void (*show)(void))
{
    int n, i, j, count = 0;
    ui_wifi = 1;
    if (!wifi_init()) {
        snprintf(status, 33, "Wi-Fi did not start");
        show();
        ui_wifi = 0;
        return 0;
    }
    snprintf(status, 33, "Searching...");
    show();
    n = scan();
    /* Strongest first; skip hidden names and repeats. */
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (bars(found_ap[j].rssi) * 1000 + found_ap[j].rssi > bars(found_ap[i].rssi) * 1000 + found_ap[i].rssi) {
                Wifi_AccessPoint t = found_ap[i];
                found_ap[i] = found_ap[j];
                found_ap[j] = t;
            }
    for (i = 0; i < n && count < max; i++) {
        int dup = 0;
        if (!found_ap[i].ssid_len) continue;
        for (j = 0; j < count; j++) dup |= !strcmp(aps[j].ssid, found_ap[i].ssid);
        if (dup) continue;
        memcpy(aps[count].ssid, found_ap[i].ssid, 32);
        aps[count].ssid[32] = 0;
        aps[count].secure = found_ap[i].security_type != AP_SECURITY_OPEN;
        aps[count].strength = bars(found_ap[i].rssi);
        count++;
    }
    snprintf(status, 33, count ? "Tap a network" : "No networks found");
    show();
    ui_wifi = 0;
    return count;
}

static char *connect_status;
static void (*connect_show)(void);
static void report_connect(int seconds)
{
    snprintf(connect_status, 33, "Connecting... %ds", seconds);
    connect_show();
}

int ok_dsi_wifi_connect(const char *ssid, const char *password, char status[33], void (*show)(void))
{
    struct in_addr ip;
    int ok;
    ui_wifi = 1;
    if (!wifi_init()) {
        ui_wifi = 0;
        return 0;
    }
    connect_status = status;
    connect_show = show;
    Wifi_DisconnectAP();
    Wifi_EnableWifi();
    start_connect(ssid, password);
    ok = wait_online(report_connect);
    if (ok) {
        ip.s_addr = Wifi_GetIP();
        snprintf(status, 33, "Connected: %s", inet_ntoa(ip));
        remember_network(ssid, password);
    } else {
        snprintf(status, 33, "Could not connect");
    }
    show();
    ui_wifi = 0;
    return ok;
}

/* ---- Sockets -------------------------------------------------------------- */

/* Bounded wait that never trusts a blocking call: ask without waiting once
 * per frame, until the deadline. */
static int wait_for(int fd, short events, int ms)
{
    int frames;
    for (frames = 0; frames <= ms * 60 / 1000; frames++) {
        struct pollfd p = {fd, events, 0};
        int rc = poll(&p, 1, 0);
        stir();
        if (rc > 0) return (p.revents & events) != 0;
        if (rc < 0) return 0;
        next_frame();
    }
    return 0;
}

static int lookup(void *ctx, const char *name, char *txt, size_t cap, size_t *len)
{
    uint8_t q[OK_DNS_MAX], r[OK_DNS_MAX];
    struct sockaddr_in addr;
    size_t qn;
    int fd, tries, ok = 0;
    (void)ctx;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return 0;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr = dns_server;
    for (tries = 0; tries < 2 && !ok; tries++) {
        uint16_t id = dns_id++;
        if (ok_dns_query(name, id, q, sizeof q, &qn) != OK_OK) break;
        sendto(fd, q, qn, 0, (struct sockaddr *)&addr, sizeof addr);
        while (wait_for(fd, POLLIN, 3000)) {
            int n = recv(fd, r, sizeof r, 0);
            ok_result res;
            if (n <= 0) break;
            res = ok_dns_txt(r, (size_t)n, id, txt, cap, len);
            if (res == OK_OK) ok = 1;
            if (res != OK_FORMAT) break;
        }
    }
    close(fd);
    return ok;
}

/* ---- Candidate nodes ------------------------------------------------------ */

/* One node per IP address: some hosts run several and would fill the list. */
static int add_node(const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp)
{
    int i;
    for (i = 0; i < node_count; i++)
        if (!memcmp(nodes[i].pub, pub, 64) || !memcmp(nodes[i].ip, ip, 4)) return 0;
    if (node_count >= MAX_NODES || !tcp) return 0;
    memcpy(nodes[node_count].pub, pub, 64);
    memcpy(nodes[node_count].ip, ip, 4);
    nodes[node_count].tcp = tcp;
    node_count++;
    return 1;
}

static void dns_found(void *ctx, const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp)
{
    (void)ctx;
    add_node(pub, ip, tcp);
}

/* Peers that once let us in, kept on the SD card and tried first. */
static int load_peers(void)
{
    char text[200];
    int added = 0;
    FILE *f = peers_path[0] ? fopen(peers_path, "r") : NULL;
    if (!f) return 0;
    while (fgets(text, sizeof text, f)) {
        uint8_t pub[64], ip[4];
        unsigned a, b, c, d, port, k, v;
        if (strncmp(text, "enode://", 8) || strlen(text) < 8 + 128 + 9) continue;
        for (k = 0; k < 64; k++) {
            if (sscanf(text + 8 + 2 * k, "%2x", &v) != 1) break;
            pub[k] = (uint8_t)v;
        }
        if (k < 64 || sscanf(text + 8 + 128, "@%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) != 5 || port > 65535) continue;
        ip[0] = (uint8_t)a;
        ip[1] = (uint8_t)b;
        ip[2] = (uint8_t)c;
        ip[3] = (uint8_t)d;
        added += add_node(pub, ip, (uint16_t)port);
    }
    fclose(f);
    return added;
}

static void remember_peer(int i)
{
    char text[200], have[200];
    FILE *f;
    int k, n;
    if (!peers_path[0]) return;
    n = snprintf(text, sizeof text, "enode://");
    for (k = 0; k < 64; k++) n += snprintf(text + n, sizeof text - (size_t)n, "%02x", nodes[i].pub[k]);
    snprintf(text + n, sizeof text - (size_t)n, "@%u.%u.%u.%u:%u\n", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2],
             nodes[i].ip[3], nodes[i].tcp);
    if ((f = fopen(peers_path, "r"))) {
        while (fgets(have, sizeof have, f))
            if (!strcmp(have, text)) {
                fclose(f);
                return;
            }
        fclose(f);
    }
    if ((f = fopen(peers_path, "a"))) {
        fputs(text, f);
        fclose(f);
        ready_peers++;
    }
}

/* ---- Discovery v4 ----------------------------------------------------------- */

static int udp_fd = -1;

static int udp_send(void *ctx, const ok_v4_endpoint *to, const uint8_t *packet, size_t n)
{
    struct sockaddr_in a;
    (void)ctx;
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(to->udp);
    memcpy(&a.sin_addr.s_addr, to->ip, 4);
    return sendto(udp_fd, packet, n, 0, (struct sockaddr *)&a, sizeof a);
}

static int udp_recv(void *ctx, uint8_t *buf, size_t cap, ok_v4_endpoint *from)
{
    struct sockaddr_in a;
    socklen_t alen = sizeof a;
    int n = recvfrom(udp_fd, buf, cap, 0, (struct sockaddr *)&a, &alen);
    (void)ctx;
    if (n <= 0) return 0;
    memcpy(from->ip, &a.sin_addr.s_addr, 4);
    from->udp = ntohs(a.sin_port);
    from->tcp = 0;
    return n;
}

static void crawl_found(void *ctx, const ok_enr *e)
{
    (void)ctx;
    add_node(e->pub, e->ip, e->tcp);
}

static void crawl(int seconds, uint8_t fork[4], int seeds_from, int seeds_to)
{
    static ok_v4_crawl c;
    struct sockaddr_in any;
    uint8_t priv[32], seed[4];
    time_t start = time(NULL), now;
    int frames = 0, i;
    if ((udp_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return;
    memset(&any, 0, sizeof any);
    any.sin_len = sizeof any;
    any.sin_family = AF_INET;
    bind(udp_fd, (struct sockaddr *)&any, sizeof any);
    fcntl(udp_fd, F_SETFL, fcntl(udp_fd, F_GETFL, 0) | O_NONBLOCK);
    draw_key(priv);
    draw(seed, sizeof seed);
    c.send = udp_send;
    c.recv = udp_recv;
    c.found = crawl_found;
    ok_v4_crawl_init(&c, priv, fork, (uint32_t)seed[0] << 24 | (uint32_t)seed[1] << 16 | (uint32_t)seed[2] << 8 | seed[3]);
    ok_wipe(priv, sizeof priv);
    for (i = 0; i < *net->boots; i++) ok_v4_crawl_add_enr(&c, net->boot[i]);
    for (i = seeds_from; i < seeds_to; i++) { /* nodes from the DNS list: their UDP port is usually the TCP one */
        ok_v4_endpoint at;
        memcpy(at.ip, nodes[i].ip, 4);
        at.tcp = at.udp = nodes[i].tcp;
        ok_v4_crawl_add_node(&c, nodes[i].pub, &at);
    }
    while ((now = time(NULL)) - start < seconds && !ui_wifi) {
        ok_v4_crawl_step(&c, (uint32_t)now);
        stir();
        if (frames++ % 30 == 0)
            line(3, "v4 %lds: %d seen %u rx %u ok", (long)(now - start), c.count, c.received, c.found_count);
        next_frame();
    }
    line(3, "v4: %d seen %u rx %u bond %u ok", c.count, c.received, c.bonded, c.found_count);
    ok_facts_now.v4_sent = (int)c.sent;
    ok_facts_now.v4_received = (int)c.received;
    ok_facts_now.v4_nodes = c.count;
    ok_wipe(c.priv, sizeof c.priv);
    close(udp_fd);
    udp_fd = -1;
}

/* ---- Sessions --------------------------------------------------------------- */

/* A node that does not answer the handshake within 5 s is not worth the
 * wait: move on. Once admitted, quiet spells of 15 s are normal. */
static int read_ms = 5000;

static int sock_read(void *ctx, uint8_t *buf, size_t n)
{
    int fd = *(int *)ctx, k;
    if (!wait_for(fd, POLLIN, read_ms)) return OK_IO_IDLE;
    k = recv(fd, buf, n, 0);
    if (k > 0) ok_facts_now.bytes_in += (uint32_t)k;
    return k > 0 ? k : 0;
}

static int sock_write(void *ctx, const uint8_t *buf, size_t n)
{
    int fd = *(int *)ctx, k;
    if (!wait_for(fd, POLLOUT, 15000)) return 0;
    k = send(fd, buf, n, 0);
    if (k > 0) ok_facts_now.bytes_out += (uint32_t)k;
    return k > 0 ? k : 0;
}

static int current_row, current_node, admitted;
static char current_client[48];

static void sock_log(void *ctx, const char *text)
{
    (void)ctx;
    line(current_row, "  %.30s", text);
    if (!strncmp(text, "got Hello: ", 11)) snprintf(current_client, sizeof current_client, "%s", text + 11);
}

/* Called only once a node has let us in: from then on we stay connected. */
static int hold(void *ctx)
{
    (void)ctx;
    if (!admitted) {
        char text[33];
        admitted = 1;
        read_ms = 15000;
        remember_peer(current_node);
        snprintf(text, sizeof text, "Online: %.24s", current_client);
        say(text);
        line(LINES - 1, "%s", text);
    }
    return !ui_wifi && online();
}

/* ---- What the technical screens show ------------------------------------------ */

static uint32_t dial_start;

static uint32_t ms_now(void)
{
    return (uint32_t)((uint64_t)cpuGetTiming() / 33514u);
}

static void stage_reached(void *ctx, int st)
{
    ok_fact_peer *p = &ok_facts_now.peers[0];
    uint32_t ms = ms_now() - dial_start;
    (void)ctx;
    p->stage = st;
    if (st == OK_STAGE_ACK) p->ms_rlpx = ms;
    if (st == OK_STAGE_HELLO) p->ms_hello = ms;
    if (st == OK_STAGE_STATUS) {
        p->ms_status = ms;
        ok_facts_now.reached_status++;
    }
}

static uint32_t seconds_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)time(NULL);
}

/* Every block a node gives us while we wait: recomputed hash and root, and
 * whether our transaction is inside. */
static void block_seen(void *ctx, const ok_header *h, const ok_body_check *c)
{
    ok_facts *f = &ok_facts_now;
    (void)ctx;
    f->blocks_checked++;
    f->incl_state = 1;
    if (h->number > f->best_head) {
        f->best_head = h->number;
        memcpy(f->best_head_hash, h->hash, 32);
    }
    watch_from = h->number + 1;
    save_watch_from();
    line(LINES - 2, "Block %llu: %u txs, root %s", (unsigned long long)h->number, (unsigned)c->count,
         c->root_ok ? "ok" : "BAD");
    if (c->found < 0) return;
    f->incl_block = h->number;
    memcpy(f->incl_block_hash, h->hash, 32);
    memcpy(f->incl_tx_root, c->root, 32);
    f->incl_index = (uint32_t)c->found;
    f->incl_block_txs = c->count;
    f->incl_root_ok = c->root_ok;
    f->incl_hash_ok = 1; /* the body was asked for by the hash recomputed from this header */
    f->incl_state = c->root_ok ? 2 : 3;
    if (!c->root_ok) return;
    {
        FILE *fp = fopen(included_path, "a");
        int k;
        if (fp) {
            for (k = 0; k < 32; k++) fprintf(fp, "%02x", pending_hash[k]);
            fprintf(fp, " block %llu index %u of %u\n", (unsigned long long)h->number, (unsigned)c->found,
                    (unsigned)c->count);
            fclose(fp);
        }
    }
    memcpy(included_hash, pending_hash, 32);
    included_block = h->number;
    included_event = 1;
    pending_len = 0;
    remove(pending_path);
    remove(from_path);
    {
        char text[48];
        snprintf(text, sizeof text, "In block %llu!", (unsigned long long)h->number);
        say(text);
        line(LINES - 1, "%s", text);
    }
}

static void begin_peer_fact(int i)
{
    ok_facts *f = &ok_facts_now;
    memmove(&f->peers[1], &f->peers[0], sizeof f->peers[0] * (OK_FACTS_PEERS - 1));
    memset(&f->peers[0], 0, sizeof f->peers[0]);
    memcpy(f->peers[0].ip, nodes[i].ip, 4);
    f->peers[0].port = nodes[i].tcp;
    memcpy(f->peers[0].pub, nodes[i].pub, 8);
    f->peers[0].disconnect = -1;
    if (f->peer_count < OK_FACTS_PEERS) f->peer_count++;
    f->tried++;
    dial_start = ms_now();
}

static void end_peer_fact(const ok_session_report *rep)
{
    ok_fact_peer *p = &ok_facts_now.peers[0];
    snprintf(p->client, sizeof p->client, "%s", rep->client);
    if (rep->outcome == OK_SESSION_DISCONNECTED) p->disconnect = (int)rep->disconnect;
    if (rep->disconnect == 4) ok_facts_now.full++;
    if (rep->status) {
        p->latest = rep->remote.latest;
        memcpy(p->latest_hash, rep->remote.latest_hash, 32);
        memcpy(p->fork_hash, rep->remote.fork_hash, 4);
        p->fork_next = rep->remote.fork_next;
        if (rep->remote.latest > ok_facts_now.best_head) {
            ok_facts_now.best_head = rep->remote.latest;
            memcpy(ok_facts_now.best_head_hash, rep->remote.latest_hash, 32);
        }
    }
    if (rep->tx_written) ok_facts_now.tx_handed = handed;
}

static int dial(int i, int row)
{
    static ok_rlpx_entropy e;
    struct sockaddr_in addr;
    uint8_t priv[32];
    ok_session_report rep;
    ok_io io;
    int fd, error = 0;
    socklen_t elen = sizeof error;

    line(row, "%u.%u.%u.%u: connecting", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
    begin_peer_fact(i);
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return 0;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(nodes[i].tcp);
    memcpy(&addr.sin_addr.s_addr, nodes[i].ip, 4);
    if ((connect(fd, (struct sockaddr *)&addr, sizeof addr) && errno != EINPROGRESS) || !wait_for(fd, POLLOUT, 4000) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &elen) || error) {
        close(fd);
        ok_facts_now.peers[0].stage = OK_STAGE_NONE;
        line(row, "%u.%u.%u.%u: no TCP", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
        return 0;
    }
    ok_facts_now.peers[0].stage = OK_STAGE_TCP;
    ok_facts_now.peers[0].ms_tcp = ms_now() - dial_start;
    draw_key(priv);
    draw_key(e.eph);
    draw(e.nonce, 32);
    draw_key(e.ecies);
    draw(e.iv, 16);
    memset(&io, 0, sizeof io);
    io.read = sock_read;
    io.write = sock_write;
    io.log = sock_log;
    io.hold = hold;
    if (pending_len) {
        io.txs = pending_list;
        io.tx_lens = &pending_len;
        io.tx_count = 1;
        /* And look for it in every new block while a node keeps us. */
        io.watch = pending_hash;
        io.watch_from = watch_from;
    }
    io.seconds = seconds_now;
    io.block = block_seen;
    io.stage = stage_reached;
    io.ctx = &fd;
    current_row = row;
    current_node = i;
    current_client[0] = 0;
    admitted = 0;
    read_ms = 5000;
    ok_session_dial(&io, priv, nodes[i].pub, &e, net->net, (uint64_t)time(NULL), "okupa/0.1 (DSi)", &rep);
    ok_wipe(priv, sizeof priv);
    ok_wipe(&e, sizeof e);
    shutdown(fd, SHUT_WR);
    close(fd);
    /* A node that reached Status can take a transaction even if it is full
     * (it is handed over before it hangs up): worth remembering. */
    if (rep.status) remember_peer(i);
    /* Where to start looking: the head when it was first handed over. */
    if (pending_len && rep.tx_written && !watch_from && rep.remote.latest) {
        watch_from = rep.remote.latest;
        save_watch_from();
    }
    if (rep.tx_written) {
        char text[48];
        handed++;
        snprintf(text, sizeof text, "Hello handed to %d nodes", handed);
        say(text);
        line(LINES - 1, "%s", text);
    }
    end_peer_fact(&rep);
    if (rep.outcome == OK_SESSION_PASS) {
        line(row, "%u.%u.%u.%u: was online", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
        return 1;
    }
    if (rep.outcome == OK_SESSION_DISCONNECTED)
        line(row, "%u.%u.%u.%u: %s%s", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3],
             rep.disconnect == 4 ? "full" : "disconnect", rep.status ? " after Status" : "");
    else
        line(row, "%u.%u.%u.%u: %s%s", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3],
             ok_session_outcome_name(rep.outcome), rep.handshake ? " (rlpx ok)" : "");
    return 0;
}

/* Dials nodes[from..to) from a different start each time; rows 4..LINES-2
 * scroll. 1 if one let us in (and we have since been disconnected). */
static int try_nodes(int from, int to)
{
    int n = to - from, rows = LINES - 5, full = 0, k;
    int start = n > 0 ? (int)(cpuGetTiming() % (unsigned)n) : 0;
    char text[48];
    for (k = 0; k < n && !ui_wifi; k++) {
        int row = 4 + k % rows;
        if (k >= rows) line(row, " ");
        snprintf(text, sizeof text, "Looking for a node %d/%d", k + 1, n);
        say(text);
        if (dial(from + (start + k) % n, row)) return 1;
        full += strstr(lines[row], "full") != NULL;
        line(LINES - 1, "Tried %d of %d, %d full", k + 1, n, full);
    }
    return 0;
}

static void pick_dns(void)
{
    struct in_addr gw, mask, dns2;
    Wifi_GetIPInfo(&gw, &mask, &dns_server, &dns2);
    /* dswifi can report 255.255.255.255 (or 0) as the DNS server after a
     * reconnect: never send there. The router usually answers DNS itself;
     * the walk is verified, so any resolver will do. */
    if (dns_server.s_addr == 0 || dns_server.s_addr == 0xffffffffu) {
        dns_server = (gw.s_addr && gw.s_addr != 0xffffffffu) ? gw : dns2;
        if (dns_server.s_addr == 0 || dns_server.s_addr == 0xffffffffu) dns_server.s_addr = htonl(0x01010101);
    }
    line(1, "DNS: %s", inet_ntoa(dns_server));
}

/* ---- The postman -------------------------------------------------------------- */

static int postman(void *arg)
{
    static ok_dnsdisc_walk w;
    uint8_t fork[4], mac[6];
    uint64_t next;
    int from, dns_from, dns_to;
    (void)arg;
    ok_keccak_init(&pool);
    stir();
    for (;;) {
        while (ui_wifi) next_frame();
        say("Wi-Fi: connecting");
        if (!wifi_up()) {
            say(known_count ? "No Wi-Fi in range" : "No Wi-Fi: Dev > Wi-Fi");
            line(0, "Wi-Fi: none, retry in 20 s");
            pause_seconds(20);
            continue;
        }
        Wifi_GetData(WIFIGETDATA_MACADDRESS, sizeof mac, mac); /* only after init: before it is a data abort */
        ok_keccak_update(&pool, mac, sizeof mac);
        {
            struct in_addr ip;
            ip.s_addr = Wifi_GetIP();
            line(0, "Wi-Fi: %s", inet_ntoa(ip));
        }
        pick_dns();
        ok_forkid(net->net, UINT64_MAX, (uint64_t)time(NULL), fork, &next);
        memcpy(ok_facts_now.fork_hash, fork, 4);
        ok_facts_now.fork_next = next;
        ok_facts_now.ip[0] = (uint8_t)Wifi_GetIP();
        ok_facts_now.ip[1] = (uint8_t)(Wifi_GetIP() >> 8);
        ok_facts_now.ip[2] = (uint8_t)(Wifi_GetIP() >> 16);
        ok_facts_now.ip[3] = (uint8_t)(Wifi_GetIP() >> 24);

        /* 1. Peers that let us in before: usually instant. */
        node_count = 0;
        ready_peers = load_peers();
        line(2, "%s, known peers: %d", net->name, ready_peers);
        if (try_nodes(0, node_count)) continue;

        /* 2. The signed DNS list: its nodes are the busiest, so they seed the
         * crawl and are only tried last. */
        dns_from = node_count;
        w.lookup = lookup;
        w.found = dns_found;
        say("Reading the node list");
        ok_dnsdisc_walk_tree(&w, net->tree, fork, 24);
        if (!w.seq && dns_server.s_addr != htonl(0x01010101)) {
            dns_server.s_addr = htonl(0x01010101);
            line(1, "DNS: 1.1.1.1 (fallback)");
            ok_dnsdisc_walk_tree(&w, net->tree, fork, 24);
        }
        dns_to = node_count;
        ok_facts_now.dns_seq = w.seq;
        ok_facts_now.dns_verified = w.seq != 0;
        ok_facts_now.dns_records = (int)w.records;
        ok_facts_now.dns_nodes = dns_to - dns_from;
        line(2, w.seq ? "DNS list: %d nodes" : "DNS list: no answer (%d)", dns_to - dns_from);

        /* 3. Lesser-known nodes through discovery v4: these have room more often. */
        from = node_count;
        say("Searching for nodes");
        crawl(30, fork, dns_from, dns_to);
        ok_facts_now.candidates = node_count;
        if (try_nodes(from, node_count)) continue;
        if (try_nodes(dns_from, dns_to)) continue;

        if (ready_peers) {
            char text[48];
            snprintf(text, sizeof text, "Ready: %d nodes take letters", ready_peers);
            say(text);
        } else {
            say("Nodes busy, retrying soon");
        }
        line(LINES - 1, "All busy, again in 20 s");
        pause_seconds(20);
    }
    return 0;
}

void ok_dsi_net_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    cothread_create(postman, NULL, 64 * 1024, COTHREAD_DETACHED);
}
