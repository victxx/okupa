#ifndef OKUPA_TLS_H
#define OKUPA_TLS_H

#include "okupa.h"
#include "bearssl.h"

/* A TLS client over BearSSL, with no sockets inside: the platform passes its
 * own read and write functions. Certificates are checked against a fixed set
 * of root CAs (core/src/tls_anchors.c) and the clock the caller gives. */
extern const br_x509_trust_anchor ok_tls_anchors[];
extern const size_t ok_tls_anchor_count;

typedef struct {
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    unsigned char buffer[BR_SSL_BUFSIZE_BIDI];
    char host[128]; /* the server of the session kept for resumption ("" none) */
    unsigned char offered[32]; /* the session id offered for resumption */
    unsigned char offered_len;
} ok_tls;

typedef int (*ok_tls_read_fn)(void *ctx, unsigned char *buf, size_t len);   /* >0 bytes, -1 error/closed */
typedef int (*ok_tls_write_fn)(void *ctx, const unsigned char *buf, size_t len);

/* seed: fresh entropy for this session (at least 32 bytes). now: unix seconds. */
void ok_tls_open(ok_tls *t, const char *host, int64_t now, const uint8_t *seed, size_t seed_len,
                 ok_tls_read_fn rd, ok_tls_write_fn wr, void *io_ctx);
/* The same, but if this ok_tls last talked to host and the handshake
 * finished, it offers to resume that session: the server may then skip the
 * certificate chain and the key exchange (the costly part on a slow CPU).
 * Returns 1 if it offered a resumption. A server that declines just does a
 * full handshake, checked as usual. */
int ok_tls_resume(ok_tls *t, const char *host, int64_t now, const uint8_t *seed, size_t seed_len, ok_tls_read_fn rd,
                  ok_tls_write_fn wr, void *io_ctx);
/* 1 if the last handshake resumed a session (no certificates were sent). */
int ok_tls_resumed(const ok_tls *t);
ok_result ok_tls_write(ok_tls *t, const void *data, size_t n);
/* *n = 0 when the server closed the connection cleanly. */
ok_result ok_tls_read(ok_tls *t, void *buf, size_t cap, size_t *n);
void ok_tls_close(ok_tls *t);
/* BearSSL's error code (0 = none), for logs: BR_ERR_X509_* for certificates. */
int ok_tls_error(const ok_tls *t);

#endif
