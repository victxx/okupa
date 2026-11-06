#include "okupa_session.h"
#include "okupa_keys.h"
#include "okupa_p2p.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF 65536
/* Incoming messages can be whole block bodies (often 100-300 KB on mainnet,
 * sometimes much more): taken from the heap once, as large as it will give
 * (the DSi's static area is only 4 MB; its heap has the rest of the 16). */
static uint8_t *wire, *msg, out[BUF];
static size_t rbuf;
#define RBUF rbuf

static int buffers(void)
{
    static const size_t sizes[] = {2u << 20, 1u << 20, 256u << 10, BUF};
    size_t i;
    if (wire) return 1;
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        wire = malloc(sizes[i] + 64);
        msg = malloc(sizes[i]);
        if (wire && msg) {
            rbuf = sizes[i];
            return 1;
        }
        free(wire);
        free(msg);
        wire = msg = NULL;
    }
    return 0;
}
static uint8_t auth_wire[OK_RLPX_MAX_PACKET], ack_wire[OK_RLPX_MAX_PACKET], scratch[OK_RLPX_MAX_PACKET];

static void say(const ok_io *io, const char *fmt, ...)
{
    char line[160];
    va_list ap;
    if (!io->log) return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    io->log(io->ctx, line);
}

/* 1 read, 0 closed. With idle_ok, a quiet line before the first byte gives
 * -1 instead (holding a session); once a message has started, quiet is fatal. */
static int read_some(const ok_io *io, uint8_t *p, size_t n, int idle_ok)
{
    size_t got = 0;
    while (got < n) {
        int k = io->read(io->ctx, p + got, n - got);
        if (k == OK_IO_IDLE && idle_ok && got == 0) return -1;
        if (k <= 0) return 0;
        got += (size_t)k;
    }
    return 1;
}

static int read_all(const ok_io *io, uint8_t *p, size_t n)
{
    return read_some(io, p, n, 0) == 1;
}

static int write_all(const ok_io *io, const uint8_t *p, size_t n)
{
    while (n) {
        int k = io->write(io->ctx, p, n);
        if (k <= 0) return 0;
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

static size_t read_packet(const ok_io *io, uint8_t *buf)
{
    size_t n;
    if (!read_all(io, buf, 2)) return 0;
    n = ok_rlpx_packet_len(buf);
    if (n > OK_RLPX_MAX_PACKET || !read_all(io, buf + 2, n - 2)) return 0;
    return n;
}

static int send_frame(const ok_io *io, ok_rlpx_session *s, const uint8_t *data, size_t n)
{
    size_t len;
    return ok_rlpx_frame_write(s, data, n, wire, RBUF + 64, &len) == OK_OK && write_all(io, wire, len);
}

/* 1 message, 0 closed, -1 broken (bad MAC, too large, malformed), 2 idle
 * (only when idle_ok: nothing arrived). */
static int recv_message_idle(const ok_io *io, ok_rlpx_session *s, int snappy, uint64_t *id, size_t *len, int idle_ok)
{
    size_t size;
    if (ok_yield) ok_yield();
    int r = read_some(io, wire, OK_RLPX_HEADER, idle_ok);
    if (r < 0) return 2;
    if (!r) return 0;
    if (ok_rlpx_header_read(s, wire, &size) != OK_OK || size > RBUF) return -1;
    if (!read_all(io, wire + OK_RLPX_HEADER, ok_rlpx_frame_len(size) - OK_RLPX_HEADER)) return 0;
    if (ok_rlpx_body_read(s, wire + OK_RLPX_HEADER, size) != OK_OK) return -1;
    return ok_p2p_read(wire + OK_RLPX_HEADER, size, snappy, id, msg, RBUF, len) == OK_OK ? 1 : -1;
}

static int recv_message(const ok_io *io, ok_rlpx_session *s, int snappy, uint64_t *id, size_t *len)
{
    return recv_message_idle(io, s, snappy, id, len, 0);
}

static int send_message(const ok_io *io, ok_rlpx_session *s, uint64_t id, const uint8_t *payload, size_t n, int snappy)
{
    static uint8_t data[BUF];
    size_t len;
    return ok_p2p_write(id, payload, n, snappy, data, sizeof data, &len) == OK_OK && send_frame(io, s, data, len);
}

static void stage(const ok_io *io, int st)
{
    if (io->stage) io->stage(io->ctx, st);
    if (ok_yield) ok_yield();
}

/* ---- Following blocks while held -------------------------------------------- */

#define WATCH_HEADERS 4

typedef struct {
    uint64_t next, request;
    int waiting; /* 0 nothing asked, 1 headers, 2 a body */
    int more;    /* the last batch was full: ask again at once */
    uint32_t asked;
    ok_header h[WATCH_HEADERS];
    int count, at;
    int mismatches; /* bodies that did not match their header, for this block */
} watcher;

static void ask_body(const ok_io *io, ok_rlpx_session *s, int snappy, watcher *w)
{
    size_t len;
    w->request++;
    w->asked = io->seconds ? io->seconds(io->ctx) : 0; /* the timeout counts from this request */
    if (ok_eth_get_bodies(w->request, (const uint8_t(*)[32])&w->h[w->at].hash, 1, out, sizeof out, &len) == OK_OK &&
        send_message(io, s, OK_P2P_ETH_OFFSET + OK_ETH_GET_BLOCK_BODIES, out, len, snappy))
        w->waiting = 2;
    else
        w->waiting = 0;
}

static void watch_step(const ok_io *io, ok_rlpx_session *s, int snappy, watcher *w, ok_session_report *rep)
{
    uint32_t t = io->seconds ? io->seconds(io->ctx) : 0;
    size_t len;
    if (!io->watch || rep->found) return;
    if (w->waiting && t - w->asked > 20) w->waiting = 0; /* no answer: ask again */
    if (w->waiting || (!w->more && t - w->asked < 6)) return;
    w->request++;
    w->asked = t;
    w->more = 0;
    if (ok_eth_get_headers(w->request, w->next, WATCH_HEADERS, out, sizeof out, &len) == OK_OK &&
        send_message(io, s, OK_P2P_ETH_OFFSET + OK_ETH_GET_BLOCK_HEADERS, out, len, snappy))
        w->waiting = 1;
}

/* A response to one of our requests: headers (then their bodies, one at a time). */
static void watch_answer(const ok_io *io, ok_rlpx_session *s, int snappy, watcher *w, ok_session_report *rep,
                         uint64_t id, size_t len)
{
    ok_rlp_item list, item;
    uint64_t request;
    size_t pos = 0, before;
    if (!io->watch || ok_eth_response(msg, len, &request, &list) != OK_OK || request != w->request) return;
    if (id == OK_P2P_ETH_OFFSET + OK_ETH_BLOCK_HEADERS && w->waiting == 1) {
        w->count = 0;
        w->waiting = 0;
        while (w->count < WATCH_HEADERS) {
            ok_header *h = &w->h[w->count];
            before = pos;
            if (ok_rlp_next(&list, &pos, &item) != OK_OK || ok_header_read(list.data + before, pos - before, h) != OK_OK)
                break;
            /* In order and linked: each one's parent is the one before. */
            if (h->number != w->next + (uint64_t)w->count ||
                (w->count && memcmp(h->parent, w->h[w->count - 1].hash, 32)))
                break;
            w->count++;
        }
        if (w->count) {
            w->at = 0;
            ask_body(io, s, snappy, w);
        }
        say(io, "got %d headers from %llu", w->count, (unsigned long long)w->next);
    } else if (id == OK_P2P_ETH_OFFSET + OK_ETH_BLOCK_BODIES && w->waiting == 2) {
        ok_body_check c;
        w->waiting = 0;
        if (ok_rlp_next(&list, &pos, &item) != OK_OK || ok_body_check_tx(&item, &w->h[w->at], io->watch, &c) != OK_OK)
            return; /* not served: ask again later */
        rep->blocks++;
        if (io->block) io->block(io->ctx, &w->h[w->at], &c);
        if (c.found >= 0 && c.root_ok) {
            rep->found = 1;
            say(io, "FOUND in block %llu at index %d, root ok", (unsigned long long)w->h[w->at].number, (int)c.found);
        }
        /* A body that does not match its header proves nothing: ask again
         * (another time, or another peer) instead of moving past the block,
         * but give up on it after three tries. */
        if (!c.root_ok && ++w->mismatches < 3) {
            w->count = 0;
            return;
        }
        w->mismatches = 0;
        w->next = w->h[w->at].number + 1;
        if (++w->at < w->count && !rep->found) ask_body(io, s, snappy, w);
        else w->more = w->count == WATCH_HEADERS;
    }
}

static int finish(ok_session_report *rep, int outcome)
{
    rep->outcome = outcome;
    return outcome;
}

static int converse(const ok_io *io, ok_rlpx_session *s, const uint8_t pub[64], int initiator,
                    const ok_eth_network *net, uint64_t now, const char *client, ok_session_report *rep)
{
    int snappy = 0, sent_status = 0, got;
    uint64_t id;
    size_t len;
    ok_p2p_hello hello;
    const char *bad;

    if (ok_p2p_hello_write(pub, client, out, sizeof out, &len) != OK_OK || !send_frame(io, s, out, len))
        return finish(rep, OK_SESSION_CLOSED);
    say(io, "sent Hello (%s, eth/%d)", client, OK_P2P_ETH_VERSION);
    while (!(rep->status && rep->pong && (initiator || sent_status))) {
        if ((got = recv_message(io, s, snappy, &id, &len)) <= 0) {
            say(io, got ? "broken frame from the peer" : "connection closed by the peer");
            return finish(rep, got ? OK_SESSION_BROKEN : OK_SESSION_CLOSED);
        }
        if (id == OK_P2P_HELLO) {
            if (ok_p2p_hello_read(msg, len, &hello) != OK_OK) return finish(rep, OK_SESSION_BROKEN);
            rep->hello = 1;
            stage(io, OK_STAGE_HELLO);
            snprintf(rep->client, sizeof rep->client, "%.*s", (int)hello.client_len, (const char *)hello.client);
            say(io, "got Hello: %s, p2p v%llu, eth/%llu", rep->client, (unsigned long long)hello.version,
                (unsigned long long)hello.eth);
            if (!hello.eth) return finish(rep, OK_SESSION_MISMATCH);
            snappy = hello.version >= OK_P2P_VERSION;
            if (ok_eth_status_write(net, now, out, sizeof out, &len) != OK_OK ||
                !send_message(io, s, OK_P2P_ETH_OFFSET + OK_ETH_STATUS, out, len, snappy))
                return finish(rep, OK_SESSION_CLOSED);
            sent_status = 1;
            say(io, "sent Status (network %llu, earliest = latest = genesis)", (unsigned long long)net->network_id);
            if (ok_p2p_ping(snappy, out, sizeof out, &len) != OK_OK || !send_frame(io, s, out, len))
                return finish(rep, OK_SESSION_CLOSED);
            say(io, "sent Ping");
        } else if (id == OK_P2P_DISCONNECT) {
            if (ok_p2p_disconnect_reason(msg, len, &rep->disconnect) != OK_OK) rep->disconnect = 0xff;
            say(io, "got Disconnect: %s (0x%02llx)", ok_p2p_reason_name(rep->disconnect),
                (unsigned long long)rep->disconnect);
            return finish(rep, OK_SESSION_DISCONNECTED);
        } else if (id == OK_P2P_PING) {
            if (ok_p2p_pong(snappy, out, sizeof out, &len) != OK_OK || !send_frame(io, s, out, len))
                return finish(rep, OK_SESSION_CLOSED);
            say(io, "got Ping, sent Pong");
        } else if (id == OK_P2P_PONG) {
            rep->pong = 1;
            say(io, "got Pong");
        } else if (id == OK_P2P_ETH_OFFSET + OK_ETH_STATUS) {
            if (ok_eth_status_read(msg, len, &rep->remote) != OK_OK) return finish(rep, OK_SESSION_BROKEN);
            bad = ok_eth_status_mismatch(net, &rep->remote);
            say(io, "got Status: network %llu, fork %02x%02x%02x%02x next %llu, blocks %llu..%llu%s%s",
                (unsigned long long)rep->remote.network_id, rep->remote.fork_hash[0], rep->remote.fork_hash[1],
                rep->remote.fork_hash[2], rep->remote.fork_hash[3], (unsigned long long)rep->remote.fork_next,
                (unsigned long long)rep->remote.earliest, (unsigned long long)rep->remote.latest,
                bad ? ", MISMATCH " : "", bad ? bad : "");
            if (bad) return finish(rep, OK_SESSION_MISMATCH);
            rep->status = 1;
            stage(io, OK_STAGE_STATUS);
            /* Many public peers are full and hang up a moment after Status:
             * hand the transactions over right now, then carry on. */
            if (io->tx_count && !rep->tx_written &&
                ok_eth_transactions(io->txs, io->tx_lens, io->tx_count, out, sizeof out, &len) == OK_OK &&
                send_message(io, s, OK_P2P_ETH_OFFSET + OK_ETH_TRANSACTIONS, out, len, snappy)) {
                rep->tx_written = 1;
                stage(io, OK_STAGE_TX);
                say(io, "sent Transactions (%lu)", (unsigned long)io->tx_count);
            }
        } else {
            say(io, "got message 0x%02llx (%lu bytes), ignored", (unsigned long long)id, (unsigned long)len);
        }
    }
    /* Admitted. Holding: answer pings, follow new blocks if asked to, and
     * let everything else pass until the caller is done or the peer goes away. */
    {
        static watcher w;
        memset(&w, 0, sizeof w);
        w.next = io->watch_from ? io->watch_from : rep->remote.latest;
        w.more = 1;
        if (io->hold) stage(io, OK_STAGE_HELD);
    while (io->hold && io->hold(io->ctx)) {
        watch_step(io, s, snappy, &w, rep);
        if ((got = recv_message_idle(io, s, snappy, &id, &len, 1)) == 2) {
            /* A quiet line: show we are still here. */
            if (ok_p2p_ping(snappy, out, sizeof out, &len) != OK_OK || !send_frame(io, s, out, len))
                return finish(rep, OK_SESSION_PASS);
            continue;
        }
        if (got <= 0) {
            say(io, "peer left after %u messages", rep->held);
            return finish(rep, OK_SESSION_PASS);
        }
        rep->held++;
        if (id == OK_P2P_PING) {
            if (ok_p2p_pong(snappy, out, sizeof out, &len) != OK_OK || !send_frame(io, s, out, len))
                return finish(rep, OK_SESSION_PASS);
        } else if (id == OK_P2P_DISCONNECT) {
            if (ok_p2p_disconnect_reason(msg, len, &rep->disconnect) != OK_OK) rep->disconnect = 0xff;
            say(io, "peer left: %s", ok_p2p_reason_name(rep->disconnect));
            return finish(rep, OK_SESSION_PASS);
        } else if (id == OK_P2P_ETH_OFFSET + OK_ETH_BLOCK_HEADERS || id == OK_P2P_ETH_OFFSET + OK_ETH_BLOCK_BODIES) {
            watch_answer(io, s, snappy, &w, rep, id, len);
        }
    }
    }
    if (ok_p2p_disconnect(0x08, snappy, out, sizeof out, &len) == OK_OK) send_frame(io, s, out, len);
    say(io, "sent Disconnect (quitting)");
    return finish(rep, OK_SESSION_PASS);
}

int ok_session_dial(const ok_io *io, const uint8_t priv[32], const uint8_t remote_pub[64], const ok_rlpx_entropy *e,
                    const ok_eth_network *net, uint64_t now, const char *client, ok_session_report *rep)
{
    static ok_rlpx_session s;
    ok_rlpx_remote remote;
    uint8_t pub[64];
    size_t auth_len, ack_len;
    int r;
    memset(rep, 0, sizeof *rep);
    if (!buffers()) return finish(rep, OK_SESSION_BROKEN);
    if (ok_keys_public(priv, pub) != OK_OK ||
        ok_rlpx_auth_write(priv, remote_pub, e, auth_wire, sizeof auth_wire, &auth_len) != OK_OK)
        return finish(rep, OK_SESSION_BROKEN);
    if (ok_yield) ok_yield();
    if (!write_all(io, auth_wire, auth_len)) return finish(rep, OK_SESSION_CLOSED);
    stage(io, OK_STAGE_AUTH);
    say(io, "sent auth (%lu bytes)", (unsigned long)auth_len);
    if (!(ack_len = read_packet(io, ack_wire))) {
        say(io, "connection closed during the handshake");
        return finish(rep, OK_SESSION_CLOSED);
    }
    memcpy(scratch, ack_wire, ack_len);
    if (ok_yield) ok_yield(); /* the ECIES and ECDH steps are the heavy ones: breathe between them */
    if (ok_rlpx_ack_read(priv, scratch, ack_len, &remote) != OK_OK ||
        ok_rlpx_session_init(&s, 1, e->eph, e->nonce, &remote, auth_wire, auth_len, ack_wire, ack_len) != OK_OK)
        return finish(rep, OK_SESSION_BROKEN);
    rep->handshake = 1;
    stage(io, OK_STAGE_ACK);
    say(io, "got ack (%lu bytes, version %llu)", (unsigned long)ack_len, (unsigned long long)remote.version);
    r = converse(io, &s, pub, 1, net, now, client, rep);
    ok_wipe(&s, sizeof s);
    return r;
}

int ok_session_answer(const ok_io *io, const uint8_t priv[32], const ok_rlpx_entropy *e,
                      const ok_eth_network *net, uint64_t now, const char *client, ok_session_report *rep)
{
    static ok_rlpx_session s;
    ok_rlpx_remote remote;
    uint8_t pub[64];
    size_t auth_len, ack_len;
    int r;
    memset(rep, 0, sizeof *rep);
    if (!buffers() || ok_keys_public(priv, pub) != OK_OK) return finish(rep, OK_SESSION_BROKEN);
    if (!(auth_len = read_packet(io, auth_wire))) return finish(rep, OK_SESSION_CLOSED);
    memcpy(scratch, auth_wire, auth_len);
    if (ok_rlpx_auth_read(priv, scratch, auth_len, &remote) != OK_OK) return finish(rep, OK_SESSION_BROKEN);
    say(io, "got auth (%lu bytes, version %llu)", (unsigned long)auth_len, (unsigned long long)remote.version);
    if (ok_rlpx_ack_write(remote.pub, e, ack_wire, sizeof ack_wire, &ack_len) != OK_OK ||
        !write_all(io, ack_wire, ack_len))
        return finish(rep, OK_SESSION_CLOSED);
    say(io, "sent ack (%lu bytes)", (unsigned long)ack_len);
    if (ok_rlpx_session_init(&s, 0, e->eph, e->nonce, &remote, auth_wire, auth_len, ack_wire, ack_len) != OK_OK)
        return finish(rep, OK_SESSION_BROKEN);
    rep->handshake = 1;
    r = converse(io, &s, pub, 0, net, now, client, rep);
    ok_wipe(&s, sizeof s);
    return r;
}

const char *ok_session_outcome_name(int outcome)
{
    static const char *names[] = {"PASS", "closed", "disconnected", "mismatch", "broken"};
    return outcome >= 0 && outcome <= OK_SESSION_BROKEN ? names[outcome] : "?";
}
