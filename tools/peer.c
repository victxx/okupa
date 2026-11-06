/* Host peer: the core's session (RLPx handshake, Hello, eth/69 Status,
 * Ping/Pong, Disconnect) over a socket. Sockets and entropy live here, never
 * in core/. It sends no transactions.
 *
 *   build/okupa-peer enode://<128 hex>@<ipv4>:<port>   dial a node (we initiate)
 *   build/okupa-peer --listen <port>                  answer one dial (for tests)
 *   build/okupa-peer --listen <port> --key <hex file>  with a fixed identity
 *   build/okupa-peer --hold <seconds> enode://...     stay connected after admission
 */
#include "okupa_keys.h"
#include "okupa_session.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define CLIENT "okupa/0.1"

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("okupa-peer: ");
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
}

static void die(const char *what)
{
    say("FAIL %s", what);
    exit(1);
}

static void random_bytes(uint8_t *out, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(out, 1, n, f) != n) die("no entropy");
    fclose(f);
}

static void random_key(uint8_t key[32])
{
    uint8_t pub[64];
    do random_bytes(key, 32);
    while (ok_keys_public(key, pub) != OK_OK);
}

static void fresh(ok_rlpx_entropy *e)
{
    random_key(e->eph);
    random_bytes(e->nonce, 32);
    random_key(e->ecies);
    random_bytes(e->iv, 16);
}

static int unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

static void hex_line(const char *label, const uint8_t *p, size_t n)
{
    size_t i;
    printf("okupa-peer: %s ", label);
    for (i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
    fflush(stdout);
}

/* The socket side of the core's session. */
static int sock_read(void *ctx, uint8_t *buf, size_t n)
{
    ssize_t k = read(*(int *)ctx, buf, n);
    if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return OK_IO_IDLE; /* the 15 s receive timeout */
    return k > 0 ? (int)k : 0;
}

static int sock_write(void *ctx, const uint8_t *buf, size_t n)
{
    ssize_t k = write(*(int *)ctx, buf, n);
    return k > 0 ? (int)k : 0;
}

static time_t hold_until;
static int hold_on(void *ctx)
{
    (void)ctx;
    return time(NULL) < hold_until;
}

static void sock_log(void *ctx, const char *line)
{
    (void)ctx;
    say("%s", line);
}

static void timeouts(int fd)
{
    struct timeval tv = {15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

static int dial(const char *enode, const uint8_t priv[32])
{
    char ip[64] = {0};
    unsigned port = 0;
    uint8_t remote_pub[64];
    struct sockaddr_in addr;
    ok_rlpx_entropy e;
    ok_session_report rep;
    ok_io io;
    int fd;

    if (strncmp(enode, "enode://", 8) || strlen(enode) < 8 + 128 + 1 || enode[8 + 128] != '@' ||
        !unhex(enode + 8, remote_pub, 64) || sscanf(enode + 8 + 129, "%63[0-9.]:%u", ip, &port) != 2)
        die("expected enode://<128 hex>@<ipv4>:<port>");
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) die("bad IPv4 address");
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) die("socket");
    timeouts(fd);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr)) die("connect");
    say("connected to %s:%u", ip, port);
    memset(&io, 0, sizeof io);
    io.read = sock_read;
    io.write = sock_write;
    io.log = sock_log;
    io.hold = hold_until ? hold_on : NULL;
    io.ctx = &fd;
    fresh(&e);
    ok_session_dial(&io, priv, remote_pub, &e, &ok_sepolia, (uint64_t)time(NULL), CLIENT, &rep);
    ok_wipe(&e, sizeof e);
    close(fd);
    return rep.outcome == OK_SESSION_PASS;
}

static int listen_once(unsigned port, const uint8_t priv[32], const uint8_t pub[64])
{
    struct sockaddr_in addr;
    ok_rlpx_entropy e;
    ok_session_report rep;
    ok_io io;
    int srv, fd, one = 1;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if ((srv = socket(AF_INET, SOCK_STREAM, 0)) < 0) die("socket");
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) || listen(srv, 1)) die("bind");
    hex_line("listening on 127.0.0.1, node", pub, 64);
    if ((fd = accept(srv, NULL, NULL)) < 0) die("accept");
    close(srv);
    timeouts(fd);
    memset(&io, 0, sizeof io);
    io.read = sock_read;
    io.write = sock_write;
    io.log = sock_log;
    io.ctx = &fd;
    fresh(&e);
    ok_session_answer(&io, priv, &e, &ok_sepolia, (uint64_t)time(NULL), CLIENT, &rep);
    ok_wipe(&e, sizeof e);
    close(fd);
    return rep.outcome == OK_SESSION_PASS;
}

static void load_key(const char *path, uint8_t key[32])
{
    char text[80] = {0};
    FILE *f = fopen(path, "r");
    if (!f || !fgets(text, sizeof text, f) || !unhex(text, key, 32)) die("key file: 64 hex characters");
    fclose(f);
}

int main(int argc, char **argv)
{
    uint8_t priv[32], pub[64];
    int ok, i, listen_port = 0;
    const char *enode = NULL, *key = NULL;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--listen") && i + 1 < argc) listen_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--key") && i + 1 < argc) key = argv[++i];
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_until = time(NULL) + atoi(argv[++i]);
        else enode = argv[i];
    }
    if (!listen_port && !enode) {
        fprintf(stderr, "usage: okupa-peer enode://<pub>@<ip>:<port> | --listen <port> [--key <file>]\n");
        return 2;
    }
    if (key) load_key(key, priv);
    else random_key(priv);
    if (ok_keys_public(priv, pub) != OK_OK) die("node key");
    ok = listen_port ? listen_once((unsigned)listen_port, priv, pub) : dial(enode, priv);
    ok_wipe(priv, sizeof priv);
    say(ok ? "PASS session complete" : "FAIL session incomplete");
    return ok ? 0 : 1;
}
