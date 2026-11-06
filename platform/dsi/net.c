#include "net.h"
#include "okupa_dns.h"
#include "okupa_discv4.h"
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

#define TREE "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net"
#define MAX_NODES 48 /* public nodes are often full: try many */

static char (*out)[33];
static int out_count;
static void (*redraw)(void);
static struct in_addr dns_server;
static uint16_t dns_id = 0x4f4b;
static struct {
    uint8_t pub[64], ip[4];
    uint16_t tcp;
} nodes[MAX_NODES];
static int node_count;

static void line(int i, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *log_path;

/* Every line also goes to a log on the SD card, closed each time, so the last
 * step before a crash can be read on a computer. */
static void line(int i, const char *fmt, ...)
{
    va_list ap;
    FILE *f;
    if (i < 0 || i >= out_count) return;
    va_start(ap, fmt);
    vsnprintf(out[i], 33, fmt, ap);
    va_end(ap);
    if (log_path && (f = fopen(log_path, "a"))) {
        fprintf(f, "%lu %s\n", (unsigned long)time(NULL), out[i]);
        fclose(f);
    }
    redraw();
}

void ok_dsi_network_log(const char *path)
{
    log_path = path;
}

/* Entropy for handshake keys only (never wallet keys): the DSi has no random
 * generator, so we hash timer readings taken around Wi-Fi and network events,
 * the clock and the MAC address. Weak by design; enough for throwaway keys. */
static ok_keccak pool;
static uint32_t pool_counter;

static void stir(void)
{
    uint32_t t = cpuGetTiming();
    time_t now = time(NULL);
    ok_keccak_update(&pool, &t, sizeof t);
    ok_keccak_update(&pool, &now, sizeof now);
}

static void draw(uint8_t *outb, size_t n)
{
    while (n) {
        ok_keccak k = pool;
        uint8_t h[32];
        size_t m = n < 32 ? n : 32;
        pool_counter++;
        stir();
        ok_keccak_update(&k, &pool_counter, sizeof pool_counter);
        ok_keccak_final(&k, h);
        memcpy(outb, h, m);
        outb += m;
        n -= m;
    }
}

static void draw_key(uint8_t key[32])
{
    uint8_t pub[64];
    do draw(key, 32);
    while (ok_keys_public(key, pub) != OK_OK);
}

static char chosen_ssid[33], chosen_password[64];
static int chosen;

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

/* Waits up to 30 s for an address; report(seconds) is called twice a second. */
static int wait_online(void (*report)(int seconds))
{
    int frames;
    for (frames = 0; frames < 60 * 30; frames++) {
        int status = Wifi_AssocStatus();
        swiWaitForVBlank();
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

static void report_test(int seconds)
{
    line(0, "Wi-Fi: connecting... %ds", seconds);
}

static int wifi_up(void)
{
    if (!wifi_init()) return 0;
    if (online()) return 1;
    Wifi_EnableWifi();
    if (chosen) start_connect(chosen_ssid, chosen_password);
    else Wifi_AutoConnect();
    return wait_online(report_test);
}

void ok_dsi_wifi_use(const char *ssid, const char *password)
{
    snprintf(chosen_ssid, sizeof chosen_ssid, "%s", ssid);
    snprintf(chosen_password, sizeof chosen_password, "%s", password ? password : "");
    chosen = ssid[0] != 0;
}

static int bars(int rssi)
{
    if (rssi > 0) return rssi > 192 ? 3 : rssi > 128 ? 2 : rssi > 64 ? 1 : 0; /* DS mode: 0..255 */
    return rssi >= -55 ? 3 : rssi >= -67 ? 2 : rssi >= -80 ? 1 : 0;              /* DSi mode: dBm */
}

int ok_dsi_wifi_scan(ok_ui_ap *aps, int max, char status[33], void (*show)(void))
{
    static Wifi_AccessPoint found_ap[24];
    int frames, n, i, j, count = 0;
    if (!wifi_init()) {
        snprintf(status, 33, "Wi-Fi did not start");
        show();
        return 0;
    }
    Wifi_EnableWifi();
    Wifi_ScanMode();
    for (frames = 0; frames < 60 * 4; frames++) {
        swiWaitForVBlank();
        if (frames % 30 == 0) {
            snprintf(status, 33, "Searching... %d found", Wifi_GetNumAP());
            show();
        }
    }
    n = Wifi_GetNumAP();
    if (n > 24) n = 24;
    for (i = 0; i < n; i++) Wifi_GetAPData(i, &found_ap[i]);
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
    if (!wifi_init()) return 0;
    connect_status = status;
    connect_show = show;
    Wifi_DisconnectAP();
    Wifi_EnableWifi();
    start_connect(ssid, password);
    if (!wait_online(report_connect)) {
        snprintf(status, 33, "Could not connect");
        show();
        return 0;
    }
    ip.s_addr = Wifi_GetIP();
    snprintf(status, 33, "Connected: %s", inet_ntoa(ip));
    show();
    ok_dsi_wifi_use(ssid, password);
    return 1;
}

/* Bounded wait that never trusts a blocking call: ask without waiting once
 * per frame, until the deadline. The screen keeps updating meanwhile. */
static int wait_for(int fd, short events, int ms)
{
    int frames;
    for (frames = 0; frames <= ms * 60 / 1000; frames++) {
        struct pollfd p = {fd, events, 0};
        int rc = poll(&p, 1, 0);
        stir();
        if (rc > 0) return (p.revents & events) != 0;
        if (rc < 0) return 0;
        swiWaitForVBlank();
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
        if (!ok && tries == 1) break;
    }
    close(fd);
    return ok;
}

static int add_node(const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp)
{
    int i;
    for (i = 0; i < node_count; i++)
        if (!memcmp(nodes[i].pub, pub, 64)) return 0;
    if (node_count >= MAX_NODES || !tcp) return 0;
    memcpy(nodes[node_count].pub, pub, 64);
    memcpy(nodes[node_count].ip, ip, 4);
    nodes[node_count].tcp = tcp;
    node_count++;
    return 1;
}

static void found(void *ctx, const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp)
{
    (void)ctx;
    add_node(pub, ip, tcp);
}

/* Peers that once let us in, kept on the SD card: tried first next time. */
static const char *peers_path;

void ok_dsi_network_peers(const char *path)
{
    peers_path = path;
}

static int load_peers(void)
{
    char text[200];
    int added = 0;
    FILE *f = peers_path ? fopen(peers_path, "r") : NULL;
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
    char text[200];
    FILE *f;
    int k, n;
    if (!peers_path) return;
    n = snprintf(text, sizeof text, "enode://");
    for (k = 0; k < 64; k++) n += snprintf(text + n, sizeof text - (size_t)n, "%02x", nodes[i].pub[k]);
    snprintf(text + n, sizeof text - (size_t)n, "@%u.%u.%u.%u:%u\n", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2],
             nodes[i].ip[3], nodes[i].tcp);
    if ((f = fopen(peers_path, "r"))) {
        char have[200];
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
    }
}

/* Discovery v4 over UDP for the core's crawler. */
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

static void crawl(int seconds, uint8_t fork[4])
{
    static ok_v4_crawl c;
    uint8_t priv[32], seed[4];
    time_t start = time(NULL), now;
    int frames = 0, i;
    if ((udp_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return;
    fcntl(udp_fd, F_SETFL, fcntl(udp_fd, F_GETFL, 0) | O_NONBLOCK);
    draw_key(priv);
    draw(seed, sizeof seed);
    c.send = udp_send;
    c.recv = udp_recv;
    c.found = crawl_found;
    ok_v4_crawl_init(&c, priv, fork, (uint32_t)seed[0] << 24 | (uint32_t)seed[1] << 16 | seed[2] << 8 | seed[3]);
    ok_wipe(priv, sizeof priv);
    for (i = 0; i < ok_sepolia_bootnode_count; i++) ok_v4_crawl_add_enr(&c, ok_sepolia_bootnodes[i]);
    while ((now = time(NULL)) - start < seconds) {
        ok_v4_crawl_step(&c, (uint32_t)now);
        stir();
        if (frames++ % 30 == 0)
            line(3, "discv4 %lds: %d seen, %u Sepolia", (long)(now - start), c.count, c.found_count);
        swiWaitForVBlank();
    }
    line(3, "discv4: %d seen, %u Sepolia", c.count, c.found_count);
    ok_wipe(c.priv, sizeof c.priv);
    close(udp_fd);
    udp_fd = -1;
}

/* A connected TCP stream for the core's session. */
static int sock_read(void *ctx, uint8_t *buf, size_t n)
{
    int fd = *(int *)ctx, k;
    if (!wait_for(fd, POLLIN, 15000)) return 0;
    k = recv(fd, buf, n, 0);
    return k > 0 ? k : 0;
}

static int sock_write(void *ctx, const uint8_t *buf, size_t n)
{
    int fd = *(int *)ctx, k;
    if (!wait_for(fd, POLLOUT, 15000)) return 0;
    k = send(fd, buf, n, 0);
    return k > 0 ? k : 0;
}

static int current_line;
static void sock_log(void *ctx, const char *text)
{
    (void)ctx;
    line(current_line, "  %.30s", text);
}

static int dial(int i, int row)
{
    static ok_rlpx_entropy e;
    struct sockaddr_in addr;
    uint8_t priv[32];
    ok_session_report rep;
    ok_io io;
    int fd, error = 0, flags;
    socklen_t elen = sizeof error;

    line(row, "%u.%u.%u.%u: connecting", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return 0;
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(nodes[i].tcp);
    memcpy(&addr.sin_addr.s_addr, nodes[i].ip, 4);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) && errno != EINPROGRESS) {
        close(fd);
        line(row, "%u.%u.%u.%u: no TCP", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
        return 0;
    }
    if (!wait_for(fd, POLLOUT, 8000) || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &elen) || error) {
        close(fd);
        line(row, "%u.%u.%u.%u: no TCP", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
        return 0;
    }
    draw_key(priv);
    draw_key(e.eph);
    draw(e.nonce, 32);
    draw_key(e.ecies);
    draw(e.iv, 16);
    io.read = sock_read;
    io.write = sock_write;
    io.log = sock_log;
    io.ctx = &fd;
    current_line = row;
    ok_session_dial(&io, priv, nodes[i].pub, &e, &ok_sepolia, (uint64_t)time(NULL), "okupa/0.1 (DSi)", &rep);
    ok_wipe(priv, sizeof priv);
    ok_wipe(&e, sizeof e);
    shutdown(fd, SHUT_WR);
    close(fd);
    if (rep.outcome == OK_SESSION_PASS) {
        remember_peer(i);
        line(row, "%u.%u.%u.%u: PASS", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3]);
        line(out_count - 1, "PASS %.27s", rep.client);
        return 1;
    }
    if (rep.outcome == OK_SESSION_DISCONNECTED)
        line(row, "%u.%u.%u.%u: %s", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3],
             rep.disconnect == 4 ? "full" : "disconnect");
    else
        line(row, "%u.%u.%u.%u: %s%s", nodes[i].ip[0], nodes[i].ip[1], nodes[i].ip[2], nodes[i].ip[3],
             ok_session_outcome_name(rep.outcome), rep.handshake ? " (rlpx ok)" : "");
    return 0;
}

/* Dials nodes[from..node_count) in turn, from a different start each run;
 * rows 4..count-2 scroll. 1 as soon as one lets us in. */
static int try_nodes(int from, int count)
{
    int n = node_count - from, rows = count - 5, full = 0, k;
    int start = n ? (int)(cpuGetTiming() % (unsigned)n) : 0;
    for (k = 0; k < n; k++) {
        int row = 4 + k % rows;
        if (k >= rows) line(row, " ");
        if (dial(from + (start + k) % n, row)) return 1;
        full += strstr(out[row], "full") != NULL;
        line(count - 1, "Tried %d of %d, %d full", k + 1, n, full);
    }
    return 0;
}

void ok_dsi_network_test(char lines[][33], int count, void (*show)(void))
{
    static ok_dnsdisc_walk w;
    struct in_addr ip, gw, mask, dns2;
    uint8_t fork[4], mac[6];
    uint64_t next;
    int n, from;

    out = lines;
    out_count = count;
    redraw = show;
    node_count = 0;
    memset(lines, 0, (size_t)count * 33);
    ok_keccak_init(&pool);
    stir();

    line(0, "Wi-Fi: connecting...");
    if (!wifi_up()) {
        line(0, "Wi-Fi: no connection");
        line(1, chosen ? "Check the password in Wi-Fi" : "Dev > Wi-Fi to pick a network");
        return;
    }
    /* Only once dswifi is up: asking it anything before init is a data abort. */
    Wifi_GetData(WIFIGETDATA_MACADDRESS, sizeof mac, mac);
    ok_keccak_update(&pool, mac, sizeof mac);
    ip = Wifi_GetIPInfo(&gw, &mask, &dns_server, &dns2);
    line(0, "Wi-Fi: %s", inet_ntoa(ip));
    /* After a reconnect dswifi can report 255.255.255.255 (or 0) as the DNS
     * server: never send there. The router usually answers DNS itself; the
     * walk itself is verified, so any resolver will do. */
    if (dns_server.s_addr == 0 || dns_server.s_addr == 0xffffffffu) {
        dns_server = (gw.s_addr && gw.s_addr != 0xffffffffu) ? gw : dns2;
        if (dns_server.s_addr == 0 || dns_server.s_addr == 0xffffffffu) dns_server.s_addr = htonl(0x01010101); /* 1.1.1.1 */
        line(1, "DNS: %s (fallback)", inet_ntoa(dns_server));
    } else {
        line(1, "DNS: %s", inet_ntoa(dns_server));
    }

    ok_forkid(&ok_sepolia, UINT64_MAX, (uint64_t)time(NULL), fork, &next);

    /* 1. Peers that let us in before. */
    n = load_peers();
    line(2, "Known peers: %d", n);
    if (try_nodes(0, count)) return;

    /* 2. The signed DNS list. */
    w.lookup = lookup;
    w.found = found;
    line(2, "Sepolia list: reading DNS...");
    from = node_count;
    ok_dnsdisc_walk_tree(&w, TREE, fork, 24);
    if (!w.seq && dns_server.s_addr != htonl(0x01010101)) {
        dns_server.s_addr = htonl(0x01010101); /* the local resolver did not answer: try 1.1.1.1 */
        line(1, "DNS: %s (fallback)", inet_ntoa(dns_server));
        ok_dnsdisc_walk_tree(&w, TREE, fork, 24);
    }
    if (w.seq) line(2, "DNS list %lu: %d nodes", (unsigned long)w.seq, node_count - from);
    else line(2, "DNS list: no verified root");
    if (try_nodes(from, count)) return;

    /* 3. Lesser-known nodes through discovery v4. */
    from = node_count;
    crawl(45, fork);
    line(2, "discv4: %d new nodes", node_count - from);
    if (try_nodes(from, count)) return;
    line(count - 1, "No node had room. Try again.");
}
