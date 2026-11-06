#include "okupa_tls.h"
#include <stdio.h>
#include <string.h>

void ok_tls_open(ok_tls *t, const char *host, int64_t now, const uint8_t *seed, size_t seed_len,
                 ok_tls_read_fn rd, ok_tls_write_fn wr, void *io_ctx)
{
    br_ssl_client_init_full(&t->client, &t->x509, ok_tls_anchors, ok_tls_anchor_count);
    /* BearSSL counts days from 1 January of year 0; 719528 of them reach 1970. */
    br_x509_minimal_set_time(&t->x509, (uint32_t)(now / 86400 + 719528), (uint32_t)(now % 86400));
    br_ssl_engine_set_buffer(&t->client.eng, t->buffer, sizeof t->buffer, 1);
    br_ssl_engine_inject_entropy(&t->client.eng, seed, seed_len);
    br_ssl_client_reset(&t->client, host, 0);
    br_sslio_init(&t->io, &t->client.eng, rd, io_ctx, wr, io_ctx);
    snprintf(t->host, sizeof t->host, "%s", host);
    t->offered_len = 0;
}

int ok_tls_resume(ok_tls *t, const char *host, int64_t now, const uint8_t *seed, size_t seed_len, ok_tls_read_fn rd,
                  ok_tls_write_fn wr, void *io_ctx)
{
    br_ssl_session_parameters sp;
    br_ssl_engine_get_session_parameters(&t->client.eng, &sp);
    if (strcmp(t->host, host) || !sp.session_id_len || !sp.cipher_suite) {
        ok_tls_open(t, host, now, seed, seed_len, rd, wr, io_ctx);
        return 0;
    }
    /* Same server: keep the engine (and its session), refresh the clock and
     * the entropy, and start again offering the session id. */
    memcpy(t->offered, sp.session_id, sp.session_id_len);
    t->offered_len = sp.session_id_len;
    br_x509_minimal_set_time(&t->x509, (uint32_t)(now / 86400 + 719528), (uint32_t)(now % 86400));
    br_ssl_engine_inject_entropy(&t->client.eng, seed, seed_len);
    br_ssl_client_reset(&t->client, host, 1);
    br_sslio_init(&t->io, &t->client.eng, rd, io_ctx, wr, io_ctx);
    return 1;
}

int ok_tls_resumed(const ok_tls *t)
{
    /* A server that resumes keeps the session id we offered; a full
     * handshake brings a new one. */
    br_ssl_session_parameters sp;
    br_ssl_engine_get_session_parameters(&t->client.eng, &sp);
    return t->offered_len && sp.session_id_len == t->offered_len && !memcmp(sp.session_id, t->offered, t->offered_len);
}

ok_result ok_tls_write(ok_tls *t, const void *data, size_t n)
{
    if (ok_yield) ok_yield(); /* between engine passes on cooperative threads */
    if (br_sslio_write_all(&t->io, data, n) < 0 || br_sslio_flush(&t->io) < 0) return OK_CRYPTO;
    return OK_OK;
}

ok_result ok_tls_read(ok_tls *t, void *buf, size_t cap, size_t *n)
{
    if (ok_yield) ok_yield();
    int k = br_sslio_read(&t->io, buf, cap);
    if (k > 0) {
        *n = (size_t)k;
        return OK_OK;
    }
    *n = 0;
    return br_ssl_engine_current_state(&t->client.eng) == BR_SSL_CLOSED && ok_tls_error(t) == 0 ? OK_OK : OK_CRYPTO;
}

void ok_tls_close(ok_tls *t)
{
    br_sslio_close(&t->io);
}

int ok_tls_error(const ok_tls *t)
{
    return br_ssl_engine_last_error(&t->client.eng);
}
