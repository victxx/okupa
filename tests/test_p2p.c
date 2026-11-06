#include "okupa_keys.h"
#include "okupa_p2p.h"
#include "okupa_snappy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t i, n = strlen(s) / 2;
    assert(n <= cap);
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
    return n;
}

/* EIP-8: a Hello from a future version with extra elements (msg-data only). */
static const char *FUTURE_HELLO =
    "f87137916b6e6574682f76302e39312f706c616e39cdc5836574683dc6846d6f726b1682270fb840"
    "fda1cff674c90c9a197539fe3dfb53086ace64f83ed7c6eabec741f7f381cc803e52ab2cd55d5569"
    "bce4347107a310dfd5f88a010cd2ffd1005ca406f1842877c883666f6f836261720304";

int main(void)
{
    static uint8_t buf[4096], msg[4096], big[300];
    uint8_t priv[32], pub[64];
    ok_p2p_hello h;
    uint64_t id, reason;
    size_t n, len, i;

    n = hex(FUTURE_HELLO, buf, sizeof buf);
    assert(ok_p2p_hello_read(buf, n, &h) == OK_OK);
    assert(h.version == 55 && h.client_len == 17 && !memcmp(h.client, "kneth/v0.91/plan9", 17));
    assert(h.eth == 0); /* offers eth/61 and mork/22 only */
    assert(h.id[0] == 0xfd && h.id[63] == 0x77);

    /* Our Hello reads back; it is sent uncompressed, with id 0x80 (RLP of 0). */
    memset(priv, 7, 32);
    assert(ok_keys_public(priv, pub) == OK_OK);
    assert(ok_p2p_hello_write(pub, "okupa/0.1", buf, sizeof buf, &n) == OK_OK && buf[0] == 0x80);
    assert(ok_p2p_read(buf, n, 0, &id, msg, sizeof msg, &len) == OK_OK && id == OK_P2P_HELLO);
    assert(ok_p2p_hello_read(msg, len, &h) == OK_OK);
    assert(h.version == OK_P2P_VERSION && h.eth == OK_P2P_ETH_VERSION && !memcmp(h.id, pub, 64));
    assert(h.client_len == 9 && !memcmp(h.client, "okupa/0.1", 9));
    assert(ok_p2p_hello_write(pub, "okupa/0.1", buf, 50, &n) == OK_RANGE);
    assert(ok_p2p_hello_read(msg, len - 1, &h) == OK_FORMAT);

    /* Ping, Pong and Disconnect, compressed and not. */
    assert(ok_p2p_ping(0, buf, sizeof buf, &n) == OK_OK && n == 2 && !memcmp(buf, "\x02\xc0", 2));
    assert(ok_p2p_ping(1, buf, sizeof buf, &n) == OK_OK && n == 4 && !memcmp(buf, "\x02\x01\x00\xc0", 4));
    assert(ok_p2p_read(buf, n, 1, &id, msg, sizeof msg, &len) == OK_OK && id == OK_P2P_PING && len == 1 && msg[0] == 0xc0);
    assert(ok_p2p_pong(1, buf, sizeof buf, &n) == OK_OK);
    assert(ok_p2p_read(buf, n, 1, &id, msg, sizeof msg, &len) == OK_OK && id == OK_P2P_PONG);
    assert(ok_p2p_disconnect(4, 1, buf, sizeof buf, &n) == OK_OK);
    assert(ok_p2p_read(buf, n, 1, &id, msg, sizeof msg, &len) == OK_OK && id == OK_P2P_DISCONNECT);
    assert(ok_p2p_disconnect_reason(msg, len, &reason) == OK_OK && reason == 4);
    assert(!strcmp(ok_p2p_reason_name(reason), "too many peers"));
    assert(!strcmp(ok_p2p_reason_name(0x10), "subprotocol") && !strcmp(ok_p2p_reason_name(99), "unknown"));

    /* Disconnect reasons as peers actually send them. */
    assert(ok_p2p_disconnect_reason((const uint8_t *)"\xc1\x03", 2, &reason) == OK_OK && reason == 3);
    assert(ok_p2p_disconnect_reason((const uint8_t *)"\x05", 1, &reason) == OK_OK && reason == 5);
    assert(ok_p2p_disconnect_reason((const uint8_t *)"\xc0", 1, &reason) == OK_OK && reason == 0);
    assert(ok_p2p_disconnect_reason(NULL, 0, &reason) == OK_OK && reason == 0);
    assert(ok_p2p_disconnect_reason((const uint8_t *)"\xc2\x03", 2, &reason) == OK_FORMAT);

    /* An eth message (ids from 0x10): payloads round-trip through Snappy. */
    for (i = 0; i < sizeof big; i++) big[i] = (uint8_t)i;
    assert(ok_p2p_write(OK_P2P_ETH_OFFSET + 2, big, sizeof big, 1, buf, sizeof buf, &n) == OK_OK && buf[0] == 0x12);
    assert(ok_p2p_read(buf, n, 1, &id, msg, sizeof msg, &len) == OK_OK && id == 0x12 && len == sizeof big);
    assert(!memcmp(msg, big, len));
    assert(ok_p2p_read(buf, n, 1, &id, msg, sizeof big - 1, &len) == OK_RANGE);
    /* A Snappy header announcing more than 16 MiB is refused before decoding. */
    assert(ok_snappy_encode(big, 10, buf + 1, sizeof buf - 1, &n) == OK_OK);
    buf[0] = 0x12;
    buf[1] = 0x81; buf[2] = 0x80; buf[3] = 0x80; buf[4] = 0x08; /* 16 MiB + 1 */
    memmove(buf + 5, buf + 2, n - 1);
    assert(ok_p2p_read(buf, n + 4, 1, &id, msg, sizeof msg, &len) == OK_RANGE);
    assert(ok_p2p_read((const uint8_t *)"\xc0", 1, 1, &id, msg, sizeof msg, &len) == OK_FORMAT);
    puts("p2p: EIP-8 future Hello read; our Hello, Ping, Pong, Disconnect and compressed messages round-trip");
    return 0;
}
