#include "okupa_p2p.h"
#include "okupa_rlp.h"
#include "okupa_snappy.h"
#include <string.h>

ok_result ok_p2p_hello_write(const uint8_t node_pub[64], const char *client, uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t hello, caps, eth;
    if (!node_pub || !client || !out || !len) return OK_ARGUMENT;
    ok_rlp_init(&w, out, cap);
    ok_rlp_uint(&w, OK_P2P_HELLO);
    hello = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, OK_P2P_VERSION);
    ok_rlp_bytes(&w, (const uint8_t *)client, strlen(client));
    caps = ok_rlp_list_begin(&w);
    eth = ok_rlp_list_begin(&w);
    ok_rlp_bytes(&w, (const uint8_t *)"eth", 3);
    ok_rlp_uint(&w, OK_P2P_ETH_VERSION);
    ok_rlp_list_end(&w, eth);
    ok_rlp_list_end(&w, caps);
    ok_rlp_uint(&w, 0); /* not listening */
    ok_rlp_bytes(&w, node_pub, 64);
    ok_rlp_list_end(&w, hello);
    return ok_rlp_done(&w, len);
}

ok_result ok_p2p_hello_read(const uint8_t *data, size_t n, ok_p2p_hello *hello)
{
    ok_rlp_item list, item, caps, cap, name, version;
    size_t pos = 0, cpos = 0, p;
    uint64_t port;
    if (!data || !hello) return OK_ARGUMENT;
    if (ok_rlp_decode(data, n, &list) != OK_OK || !list.list) return OK_FORMAT;
    if (ok_rlp_next(&list, &pos, &item) != OK_OK || ok_rlp_to_uint(&item, &hello->version) != OK_OK) return OK_FORMAT;
    if (ok_rlp_next(&list, &pos, &item) != OK_OK || item.list) return OK_FORMAT;
    hello->client = item.data;
    hello->client_len = item.len;
    if (ok_rlp_next(&list, &pos, &caps) != OK_OK || !caps.list) return OK_FORMAT;
    hello->eth = 0;
    while (ok_rlp_next(&caps, &cpos, &cap) == OK_OK) {
        uint64_t v;
        p = 0;
        if (!cap.list || ok_rlp_next(&cap, &p, &name) != OK_OK || name.list ||
            ok_rlp_next(&cap, &p, &version) != OK_OK || ok_rlp_to_uint(&version, &v) != OK_OK)
            return OK_FORMAT;
        if (name.len == 3 && !memcmp(name.data, "eth", 3) && v == OK_P2P_ETH_VERSION) hello->eth = v;
    }
    if (ok_rlp_next(&list, &pos, &item) != OK_OK || ok_rlp_to_uint(&item, &port) != OK_OK) return OK_FORMAT;
    if (ok_rlp_next(&list, &pos, &item) != OK_OK || item.list || item.len != 64) return OK_FORMAT;
    memcpy(hello->id, item.data, 64);
    return OK_OK;
}

ok_result ok_p2p_write(uint64_t id, const uint8_t *payload, size_t n, int snappy, uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_writer w;
    size_t head, body;
    ok_result r;
    if (!payload || !out || !len) return OK_ARGUMENT;
    if (n > OK_P2P_MAX_MESSAGE) return OK_RANGE;
    ok_rlp_init(&w, out, cap);
    ok_rlp_uint(&w, id);
    if ((r = ok_rlp_done(&w, &head)) != OK_OK) return r;
    if (snappy) {
        r = ok_snappy_encode(payload, n, out + head, cap - head, &body);
    } else {
        r = cap - head < n ? OK_RANGE : OK_OK;
        if (r == OK_OK) memmove(out + head, payload, body = n);
    }
    if (r == OK_OK) *len = head + body;
    return r;
}

ok_result ok_p2p_read(const uint8_t *data, size_t n, int snappy, uint64_t *id, uint8_t *out, size_t cap, size_t *len)
{
    ok_rlp_item item;
    size_t head, size;
    ok_result r;
    if (!data || !id || !out || !len) return OK_ARGUMENT;
    if (ok_rlp_item_at(data, n, &item, &head) != OK_OK || ok_rlp_to_uint(&item, id) != OK_OK) return OK_FORMAT;
    data += head;
    n -= head;
    if (!snappy) {
        if (n > cap) return OK_RANGE;
        memmove(out, data, n);
        *len = n;
        return OK_OK;
    }
    if ((r = ok_snappy_length(data, n, &size)) != OK_OK) return r;
    if (size > OK_P2P_MAX_MESSAGE || size > cap) return OK_RANGE;
    return ok_snappy_decode(data, n, out, cap, len);
}

ok_result ok_p2p_ping(int snappy, uint8_t *out, size_t cap, size_t *len)
{
    static const uint8_t empty = 0xc0;
    return ok_p2p_write(OK_P2P_PING, &empty, 1, snappy, out, cap, len);
}

ok_result ok_p2p_pong(int snappy, uint8_t *out, size_t cap, size_t *len)
{
    static const uint8_t empty = 0xc0;
    return ok_p2p_write(OK_P2P_PONG, &empty, 1, snappy, out, cap, len);
}

ok_result ok_p2p_disconnect(uint64_t reason, int snappy, uint8_t *out, size_t cap, size_t *len)
{
    uint8_t payload[16];
    ok_rlp_writer w;
    size_t mark, n;
    ok_result r;
    ok_rlp_init(&w, payload, sizeof payload);
    mark = ok_rlp_list_begin(&w);
    ok_rlp_uint(&w, reason);
    ok_rlp_list_end(&w, mark);
    if ((r = ok_rlp_done(&w, &n)) != OK_OK) return r;
    return ok_p2p_write(OK_P2P_DISCONNECT, payload, n, snappy, out, cap, len);
}

ok_result ok_p2p_disconnect_reason(const uint8_t *payload, size_t n, uint64_t *reason)
{
    ok_rlp_item item, child;
    size_t pos = 0;
    if (!reason || (!payload && n)) return OK_ARGUMENT;
    *reason = 0;
    if (n == 0) return OK_OK;
    if (ok_rlp_decode(payload, n, &item) != OK_OK) return OK_FORMAT;
    if (!item.list) return ok_rlp_to_uint(&item, reason) == OK_OK ? OK_OK : OK_FORMAT;
    if (ok_rlp_next(&item, &pos, &child) != OK_OK) return OK_OK;
    return ok_rlp_to_uint(&child, reason) == OK_OK ? OK_OK : OK_FORMAT;
}

const char *ok_p2p_reason_name(uint64_t reason)
{
    static const char *names[] = {"requested", "tcp error", "protocol breach", "useless peer", "too many peers",
                                  "already connected", "incompatible version", "null identity", "quitting",
                                  "unexpected identity", "connected to self", "ping timeout"};
    if (reason < sizeof names / sizeof names[0]) return names[reason];
    return reason == 0x10 ? "subprotocol" : "unknown";
}
