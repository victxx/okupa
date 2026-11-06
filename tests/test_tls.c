#include "okupa_tls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A "server" that answers the ClientHello with plain HTTP. */
static size_t sent;
static int fake_read(void *ctx, unsigned char *buf, size_t len)
{
    static const char reply[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
    size_t n = len < sizeof reply - 1 ? len : sizeof reply - 1;
    (void)ctx;
    memcpy(buf, reply, n);
    return (int)n;
}
static int fake_write(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    sent += len;
    return (int)len;
}

int main(void)
{
    static ok_tls t;
    uint8_t seed[32] = {1};
    size_t n;
    char buf[64];
    size_t i;

    /* The fixed roots: eight CAs, each with a name and a key. */
    assert(ok_tls_anchor_count == 8);
    for (i = 0; i < ok_tls_anchor_count; i++)
        assert(ok_tls_anchors[i].dn.len > 0 && (ok_tls_anchors[i].flags & BR_X509_TA_CA));

    /* Speaking TLS to something that is not TLS fails cleanly, after a ClientHello. */
    ok_tls_open(&t, "example.org", 1800000000, seed, sizeof seed, fake_read, fake_write, NULL);
    assert(ok_tls_write(&t, "GET / HTTP/1.1\r\n\r\n", 18) == OK_CRYPTO);
    assert(sent > 0 && ok_tls_error(&t) != 0);
    assert(ok_tls_read(&t, buf, sizeof buf, &n) == OK_CRYPTO && n == 0);
    puts("tls: eight pinned roots; a non-TLS peer is refused without a crash (live servers: tools/https.c)");
    return 0;
}
