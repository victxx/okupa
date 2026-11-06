/* Host peer over the same core the console will run: RLPx handshake, Hello,
 * eth/69 Status and Ping/Pong, then a clean Disconnect. Sockets and entropy
 * live here, never in core/. It sends no transactions.
 *
 *   build/okupa-peer enode://<128 hex>@<ipv4>:<port>   dial a node (we initiate)
 *   build/okupa-peer --listen <port>                  answer one dial (for tests)
 *   build/okupa-peer --listen <port> --key <hex file>  with a fixed identity
 */
#include "okupa_eth.h"
#include "okupa_keys.h"
#include "okupa_p2p.h"
#include "okupa_rlpx.h"
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
#define BUF 65536

static uint8_t wire[BUF + 64], msg[BUF];

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

static void write_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k <= 0) die("write");
        p += k;
        n -= (size_t)k;
    }
}

static int read_all(int fd, uint8_t *p, size_t n)
{
    while (n) {
        ssize_t k = read(fd, p, n);
        if (k <= 0) return 0;
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

/* One handshake packet: 2-byte size, then the rest. */
static size_t read_packet(int fd, uint8_t *buf)
{
    size_t n;
    if (!read_all(fd, buf, 2)) die("connection closed during the handshake");
    n = ok_rlpx_packet_len(buf);
    if (n > OK_RLPX_MAX_PACKET || !read_all(fd, buf + 2, n - 2)) die("bad handshake packet");
    return n;
}

static void send_frame(int fd, ok_rlpx_session *s, const uint8_t *data, size_t n)
{
    size_t len;
    if (ok_rlpx_frame_write(s, data, n, wire, sizeof wire, &len) != OK_OK) die("frame too large");
    write_all(fd, wire, len);
}

/* Reads a frame and splits it; returns 0 on a closed or broken connection. */
static int recv_message(int fd, ok_rlpx_session *s, int snappy, uint64_t *id, size_t *len)
{
    size_t size;
    ok_result r;
    if (!read_all(fd, wire, OK_RLPX_HEADER)) return 0;
    if (ok_rlpx_header_read(s, wire, &size) != OK_OK) die("header MAC");
    if (size > BUF) die("frame too large");
    if (!read_all(fd, wire + OK_RLPX_HEADER, ok_rlpx_frame_len(size) - OK_RLPX_HEADER)) return 0;
    if (ok_rlpx_body_read(s, wire + OK_RLPX_HEADER, size) != OK_OK) die("frame MAC");
    r = ok_p2p_read(wire + OK_RLPX_HEADER, size, snappy, id, msg, sizeof msg, len);
    if (r != OK_OK) die(ok_result_name(r));
    return 1;
}

static void send_message(int fd, ok_rlpx_session *s, uint64_t id, const uint8_t *payload, size_t n, int snappy)
{
    static uint8_t data[BUF];
    size_t len;
    if (ok_p2p_write(id, payload, n, snappy, data, sizeof data, &len) != OK_OK) die("message too large");
    send_frame(fd, s, data, len);
}

/* Both sides after the handshake: Hello, Status, one Ping answered, then Disconnect. */
static int converse(int fd, ok_rlpx_session *s, const uint8_t pub[64], int initiator)
{
    static uint8_t out[BUF];
    int snappy = 0, have_status = 0, have_pong = 0, sent_status = 0;
    uint64_t id, reason;
    size_t len;
    ok_p2p_hello hello;
    ok_eth_status st;
    const char *bad;

    if (ok_p2p_hello_write(pub, CLIENT, out, sizeof out, &len) != OK_OK) die("hello");
    send_frame(fd, s, out, len);
    say("sent Hello (%s, eth/%d)", CLIENT, OK_P2P_ETH_VERSION);

    while (!(have_status && have_pong && (initiator || sent_status))) {
        if (!recv_message(fd, s, snappy, &id, &len)) {
            say("connection closed by the peer");
            return have_status;
        }
        if (id == OK_P2P_HELLO) {
            if (ok_p2p_hello_read(msg, len, &hello) != OK_OK) die("their Hello");
            say("got Hello: %.*s, p2p v%llu, eth/%llu", (int)hello.client_len, (const char *)hello.client,
                (unsigned long long)hello.version, (unsigned long long)hello.eth);
            if (!hello.eth) die("no shared eth version");
            snappy = hello.version >= OK_P2P_VERSION;
            if (ok_eth_status_write(&ok_sepolia, (uint64_t)time(NULL), out, sizeof out, &len) != OK_OK) die("status");
            send_message(fd, s, OK_P2P_ETH_OFFSET + OK_ETH_STATUS, out, len, snappy);
            sent_status = 1;
            say("sent Status (Sepolia, earliest = latest = genesis)");
            if (ok_p2p_ping(snappy, out, sizeof out, &len) != OK_OK) die("ping");
            send_frame(fd, s, out, len);
            say("sent Ping");
        } else if (id == OK_P2P_DISCONNECT) {
            if (ok_p2p_disconnect_reason(msg, len, &reason) != OK_OK) reason = 0xff;
            say("got Disconnect: %s (0x%02llx)", ok_p2p_reason_name(reason), (unsigned long long)reason);
            return 0;
        } else if (id == OK_P2P_PING) {
            if (ok_p2p_pong(snappy, out, sizeof out, &len) != OK_OK) die("pong");
            send_frame(fd, s, out, len);
            say("got Ping, sent Pong");
        } else if (id == OK_P2P_PONG) {
            have_pong = 1;
            say("got Pong");
        } else if (id == OK_P2P_ETH_OFFSET + OK_ETH_STATUS) {
            if (ok_eth_status_read(msg, len, &st) != OK_OK) die("their Status");
            bad = ok_eth_status_mismatch(&ok_sepolia, &st);
            say("got Status: network %llu, fork %02x%02x%02x%02x next %llu, blocks %llu..%llu%s%s",
                (unsigned long long)st.network_id, st.fork_hash[0], st.fork_hash[1], st.fork_hash[2],
                st.fork_hash[3], (unsigned long long)st.fork_next, (unsigned long long)st.earliest,
                (unsigned long long)st.latest, bad ? ", MISMATCH " : "", bad ? bad : "");
            if (bad) return 0;
            have_status = 1;
        } else {
            say("got message 0x%02llx (%zu bytes), ignored", (unsigned long long)id, len);
        }
    }
    if (ok_p2p_disconnect(0x08, snappy, out, sizeof out, &len) == OK_OK) send_frame(fd, s, out, len);
    say("sent Disconnect (quitting)");
    return 1;
}

static void timeouts(int fd)
{
    struct timeval tv = {15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

static int dial(const char *enode, const uint8_t priv[32], const uint8_t pub[64])
{
    static uint8_t auth[OK_RLPX_MAX_PACKET], ack[OK_RLPX_MAX_PACKET], tmp[OK_RLPX_MAX_PACKET];
    char ip[64] = {0};
    unsigned port = 0;
    uint8_t remote_pub[64];
    struct sockaddr_in addr;
    ok_rlpx_entropy e;
    ok_rlpx_remote remote;
    ok_rlpx_session s;
    size_t auth_len, ack_len;
    int fd, ok;

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

    random_key(e.eph);
    random_bytes(e.nonce, 32);
    random_key(e.ecies);
    random_bytes(e.iv, 16);
    if (ok_rlpx_auth_write(priv, remote_pub, &e, auth, sizeof auth, &auth_len) != OK_OK) die("auth");
    write_all(fd, auth, auth_len);
    say("sent auth (%zu bytes)", auth_len);
    ack_len = read_packet(fd, ack);
    memcpy(tmp, ack, ack_len);
    if (ok_rlpx_ack_read(priv, tmp, ack_len, &remote) != OK_OK) die("ack");
    say("got ack (%zu bytes, version %llu)", ack_len, (unsigned long long)remote.version);
    if (ok_rlpx_session_init(&s, 1, e.eph, e.nonce, &remote, auth, auth_len, ack, ack_len) != OK_OK) die("secrets");
    ok_wipe(&e, sizeof e);
    ok = converse(fd, &s, pub, 1);
    ok_wipe(&s, sizeof s);
    close(fd);
    return ok;
}

static int listen_once(unsigned port, const uint8_t priv[32], const uint8_t pub[64])
{
    static uint8_t auth[OK_RLPX_MAX_PACKET], ack[OK_RLPX_MAX_PACKET], tmp[OK_RLPX_MAX_PACKET];
    struct sockaddr_in addr;
    ok_rlpx_entropy e;
    ok_rlpx_remote remote;
    ok_rlpx_session s;
    size_t auth_len, ack_len;
    int srv, fd, one = 1, ok;

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

    auth_len = read_packet(fd, auth);
    memcpy(tmp, auth, auth_len);
    if (ok_rlpx_auth_read(priv, tmp, auth_len, &remote) != OK_OK) die("auth");
    say("got auth (%zu bytes, version %llu)", auth_len, (unsigned long long)remote.version);
    random_key(e.eph);
    random_bytes(e.nonce, 32);
    random_key(e.ecies);
    random_bytes(e.iv, 16);
    if (ok_rlpx_ack_write(remote.pub, &e, ack, sizeof ack, &ack_len) != OK_OK) die("ack");
    write_all(fd, ack, ack_len);
    say("sent ack (%zu bytes)", ack_len);
    if (ok_rlpx_session_init(&s, 0, e.eph, e.nonce, &remote, auth, auth_len, ack, ack_len) != OK_OK) die("secrets");
    ok_wipe(&e, sizeof e);
    ok = converse(fd, &s, pub, 0);
    ok_wipe(&s, sizeof s);
    close(fd);
    return ok;
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
        else enode = argv[i];
    }
    if (!listen_port && !enode) {
        fprintf(stderr, "usage: okupa-peer enode://<pub>@<ip>:<port> | --listen <port> [--key <file>]\n");
        return 2;
    }
    if (key) load_key(key, priv);
    else random_key(priv);
    if (ok_keys_public(priv, pub) != OK_OK) die("node key");
    ok = listen_port ? listen_once((unsigned)listen_port, priv, pub) : dial(enode, priv, pub);
    ok_wipe(priv, sizeof priv);
    say(ok ? "PASS session complete" : "FAIL session incomplete");
    return ok ? 0 : 1;
}
