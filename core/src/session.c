#include "okupa_session.h"
#include "okupa_keys.h"
#include "okupa_p2p.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define BUF 65536

static uint8_t wire[BUF + 64], msg[BUF], out[BUF];
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
    return ok_rlpx_frame_write(s, data, n, wire, sizeof wire, &len) == OK_OK && write_all(io, wire, len);
}

/* 1 message, 0 closed, -1 broken (bad MAC, too large, malformed), 2 idle
 * (only when idle_ok: nothing arrived). */
static int recv_message_idle(const ok_io *io, ok_rlpx_session *s, int snappy, uint64_t *id, size_t *len, int idle_ok)
{
    size_t size;
    int r = read_some(io, wire, OK_RLPX_HEADER, idle_ok);
    if (r < 0) return 2;
    if (!r) return 0;
    if (ok_rlpx_header_read(s, wire, &size) != OK_OK || size > BUF) return -1;
    if (!read_all(io, wire + OK_RLPX_HEADER, ok_rlpx_frame_len(size) - OK_RLPX_HEADER)) return 0;
    if (ok_rlpx_body_read(s, wire + OK_RLPX_HEADER, size) != OK_OK) return -1;
    return ok_p2p_read(wire + OK_RLPX_HEADER, size, snappy, id, msg, sizeof msg, len) == OK_OK ? 1 : -1;
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
            /* Many public peers are full and hang up a moment after Status:
             * hand the transactions over right now, then carry on. */
            if (io->tx_count && !rep->tx_written &&
                ok_eth_transactions(io->txs, io->tx_lens, io->tx_count, out, sizeof out, &len) == OK_OK &&
                send_message(io, s, OK_P2P_ETH_OFFSET + OK_ETH_TRANSACTIONS, out, len, snappy)) {
                rep->tx_written = 1;
                say(io, "sent Transactions (%lu)", (unsigned long)io->tx_count);
            }
        } else {
            say(io, "got message 0x%02llx (%lu bytes), ignored", (unsigned long long)id, (unsigned long)len);
        }
    }
    /* Admitted. Holding: answer pings and let everything else pass until the
     * caller is done or the peer goes away. */
    while (io->hold && io->hold(io->ctx)) {
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
    if (ok_keys_public(priv, pub) != OK_OK ||
        ok_rlpx_auth_write(priv, remote_pub, e, auth_wire, sizeof auth_wire, &auth_len) != OK_OK)
        return finish(rep, OK_SESSION_BROKEN);
    if (!write_all(io, auth_wire, auth_len)) return finish(rep, OK_SESSION_CLOSED);
    say(io, "sent auth (%lu bytes)", (unsigned long)auth_len);
    if (!(ack_len = read_packet(io, ack_wire))) {
        say(io, "connection closed during the handshake");
        return finish(rep, OK_SESSION_CLOSED);
    }
    memcpy(scratch, ack_wire, ack_len);
    if (ok_rlpx_ack_read(priv, scratch, ack_len, &remote) != OK_OK ||
        ok_rlpx_session_init(&s, 1, e->eph, e->nonce, &remote, auth_wire, auth_len, ack_wire, ack_len) != OK_OK)
        return finish(rep, OK_SESSION_BROKEN);
    rep->handshake = 1;
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
    if (ok_keys_public(priv, pub) != OK_OK) return finish(rep, OK_SESSION_BROKEN);
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
