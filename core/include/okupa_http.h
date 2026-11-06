#ifndef OKUPA_HTTP_H
#define OKUPA_HTTP_H

#include "okupa.h"

/* HTTP/1.1 as far as one JSON POST or GET needs: build the request, then read
 * a complete response (Content-Length or chunked) already in a buffer. The
 * transport (TCP, TLS) belongs to the platform. */
ok_result ok_http_request(const char *method, const char *host, const char *path, const char *headers,
                          const char *body, size_t body_len, char *out, size_t cap, size_t *len);

typedef struct {
    int status;
    char *body;          /* inside the buffer, decoded in place */
    const char *headers; /* the raw header block, for ok_http_header */
    size_t headers_len;
} ok_http_response;

/* OK_RANGE: the response is not complete yet (read more and call again).
 * On OK_OK, r->body holds *body_len bytes (chunked encoding removed). Without
 * a length the body runs to the end of the buffer (Connection: close). */
ok_result ok_http_response_read(char *buf, size_t n, ok_http_response *r, size_t *body_len);
/* A header's value (case-insensitive name), NUL-terminated into out. */
ok_result ok_http_header(const ok_http_response *r, const char *name, char *out, size_t cap);

#endif
