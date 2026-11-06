/* Puts a file on 0G Storage (mainnet) with the core alone: root, submission
 * signed here, segment to the nodes. The same code the DSi runs.
 *   build/okupa-store <file (up to 256 KB)> <0G key file> */
#define _POSIX_C_SOURCE 200809L
#include "okupa_store.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    ssize_t n = read(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    ssize_t n = write(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

static int dial(const char *host, const char *port)
{
    struct addrinfo hints, *ai;
    struct timeval tv = {30, 0};
    int fd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &ai)) return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        freeaddrinfo(ai);
        if (fd >= 0) close(fd);
        return -1;
    }
    freeaddrinfo(ai);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

/* HTTPS to the chain's JSON-RPC, a fresh connection each time (resumed). */
static int https(void *ctx, const char *host, const char *head, size_t n, const char *body, size_t len, char *resp,
                 size_t cap, size_t *got)
{
    static ok_tls tls;
    uint8_t seed[32];
    FILE *f = fopen("/dev/urandom", "rb");
    int fd = dial(host, "443"), e;
    (void)ctx;
    if (fd < 0 || !f || fread(seed, 1, 32, f) != 32) {
        if (f) fclose(f);
        if (fd >= 0) close(fd);
        return -3;
    }
    fclose(f);
    ok_tls_resume(&tls, host, (int64_t)time(NULL), seed, 32, sock_read, sock_write, &fd);
    e = ok_think_exchange(&tls, head, n, body, len, resp, cap, got);
    close(fd);
    return e;
}

/* Plain HTTP to a storage node, "ip:port". */
static int http(void *ctx, const char *host, const char *head, size_t n, const char *body, size_t len, char *resp,
                size_t cap, size_t *got)
{
    char name[64], *colon;
    int fd;
    ssize_t k;
    (void)ctx;
    snprintf(name, sizeof name, "%s", host);
    if (!(colon = strchr(name, ':'))) return -2;
    *colon = 0;
    if ((fd = dial(name, colon + 1)) < 0) return -3;
    if (write(fd, head, n) != (ssize_t)n || (len && write(fd, body, len) != (ssize_t)len)) {
        close(fd);
        return -1;
    }
    *got = 0;
    while (*got < cap && (k = read(fd, resp + *got, cap - *got)) > 0) {
        *got += (size_t)k;
        if (ok_think_complete(resp, *got)) break;
    }
    close(fd);
    resp[*got] = 0;
    return *got ? 0 : -1;
}

static void pause_ms(int ms) { usleep((useconds_t)ms * 1000); }
static void say(const char *step) { printf("  %s\n", step); }

static int unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    if (s[0] == '0' && s[1] == 'x') s += 2;
    if (strlen(s) < 2 * n) return 0;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static uint8_t data[OK_ZGS_MAX];
    static char buf[OK_ZGS_MAX * 2];
    static ok_store s;
    ok_store_net net;
    uint8_t key[32];
    char text[80];
    size_t size;
    FILE *f;
    if (argc < 3) {
        fprintf(stderr, "usage: okupa-store <file> <0G key file>\n");
        return 2;
    }
    if (!(f = fopen(argv[1], "rb"))) return 2;
    size = fread(data, 1, sizeof data, f);
    fclose(f);
    if (!(f = fopen(argv[2], "r")) || !fgets(text, sizeof text, f)) return 2;
    fclose(f);
    text[strcspn(text, "\r\n")] = 0;
    if (!unhex(text, key, 32)) return 2;
    memset(&net, 0, sizeof net);
    net.rpc_host = "evmrpc.0g.ai";
    net.node[0] = "34.71.110.60:5678"; /* mainnet: two shards, two nodes each */
    net.node[1] = "34.66.131.173:5678";
    net.node[2] = "34.169.236.186:5678";
    net.node[3] = "34.60.163.4:5678";
    net.node_shard[0] = net.node_shard[1] = 0;
    net.node_shard[2] = net.node_shard[3] = 1;
    net.node_count = 4;
    net.shards = 2;
    unhex("62d4144db0f0a6fbbaeb6296c785c71b3d57c526", net.flow, 20);
    unhex("F574ff1fD8602F0131bAaB74f9F7CF24993321dE", net.market, 20);
    net.chain_id = 16661;
    if (ok_store_put(&s, &net, key, data, size, buf, sizeof buf, https, http, NULL, pause_ms, say) != OK_OK) {
        fprintf(stderr, "okupa-store: setup failed\n");
        return 1;
    }
    ok_wipe(key, sizeof key);
    printf("%s: root %s\n", s.stage == OK_STORE_DONE ? "STORED" : "FAILED", s.root);
    printf("tx %s nonce %llu gas %llu at %llu wei, fee %llu wei, seq %llu, finalized %d, already %d\n", s.tx,
           (unsigned long long)s.nonce, (unsigned long long)s.gas, (unsigned long long)s.gas_price,
           (unsigned long long)s.fee, (unsigned long long)s.seq, s.finalized, s.already);
    if (s.error[0]) printf("error: %s\n", s.error);
    return s.stage == OK_STORE_DONE ? 0 : 1;
}
