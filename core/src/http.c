#include "okupa_http.h"
#include <stdio.h>
#include <string.h>

ok_result ok_http_request(const char *method, const char *host, const char *path, const char *headers,
                          const char *body, size_t body_len, char *out, size_t cap, size_t *len)
{
    int k;
    if (!method || !host || !path || !out || !len || (!body && body_len)) return OK_ARGUMENT;
    k = snprintf(out, cap, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: okupa/0.1\r\nConnection: close\r\n%s",
                 method, path, host, headers ? headers : "");
    if (k < 0 || (size_t)k >= cap) return OK_RANGE;
    if (body) {
        int m = snprintf(out + k, cap - (size_t)k, "Content-Length: %lu\r\n\r\n", (unsigned long)body_len);
        if (m < 0 || (size_t)(k + m) + body_len >= cap) return OK_RANGE;
        k += m;
        memcpy(out + k, body, body_len);
        *len = (size_t)k + body_len;
    } else {
        if ((size_t)k + 2 >= cap) return OK_RANGE;
        memcpy(out + k, "\r\n", 2);
        *len = (size_t)k + 2;
    }
    out[*len] = 0; /* for logs; the length is what counts */
    return OK_OK;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

static const char *find(const char *s, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(s + i, needle, k)) return s + i;
    return NULL;
}

ok_result ok_http_header(const ok_http_response *r, const char *name, char *out, size_t cap)
{
    const char *s, *end;
    size_t k;
    if (!r || !name || !out || cap == 0) return OK_ARGUMENT;
    k = strlen(name);
    s = r->headers;
    end = r->headers + r->headers_len;
    while (s < end) {
        const char *eol = find(s, (size_t)(end - s), "\r\n");
        size_t i, line;
        if (!eol) eol = end;
        line = (size_t)(eol - s);
        for (i = 0; i < k && i < line && lower(s[i]) == lower(name[i]); i++) {}
        if (i == k && line > k && s[k] == ':') {
            const char *v = s + k + 1;
            size_t n;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            n = (size_t)(eol - v);
            while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
            if (n >= cap) return OK_RANGE;
            memcpy(out, v, n);
            out[n] = 0;
            return OK_OK;
        }
        s = eol + 2;
    }
    return OK_FORMAT;
}

/* Decodes chunked data in place; OK_RANGE if the last chunk has not arrived. */
static ok_result dechunk(char *buf, size_t n, size_t *out_len)
{
    size_t i = 0, o = 0;
    for (;;) {
        size_t size = 0, digits = 0;
        const char *eol = find(buf + i, n - i, "\r\n");
        if (!eol) return OK_RANGE;
        while (buf + i < eol && buf[i] != ';') {
            char c = buf[i++];
            int d = c >= '0' && c <= '9' ? c - '0' : lower(c) >= 'a' && lower(c) <= 'f' ? lower(c) - 'a' + 10 : -1;
            if (d < 0 || ++digits > 7) return OK_FORMAT;
            size = size << 4 | (size_t)d;
        }
        if (!digits) return OK_FORMAT;
        i = (size_t)(eol - buf) + 2;
        if (size == 0) {
            *out_len = o;
            return OK_OK; /* trailers, if any, are ignored */
        }
        if (n - i < size + 2) return OK_RANGE;
        if (memcmp(buf + i + size, "\r\n", 2)) return OK_FORMAT;
        memmove(buf + o, buf + i, size);
        o += size;
        i += size + 2;
    }
}

ok_result ok_http_response_read(char *buf, size_t n, ok_http_response *r, size_t *body_len)
{
    const char *end, *eol;
    char value[32];
    size_t head, rest;
    int status = 0, i;
    if (!buf || !r || !body_len) return OK_ARGUMENT;
    if (!(end = find(buf, n, "\r\n\r\n"))) return n > 16384 ? OK_FORMAT : OK_RANGE;
    if (n < 12 || memcmp(buf, "HTTP/1.", 7) || buf[8] != ' ') return OK_FORMAT;
    for (i = 9; i < 12; i++) {
        if (buf[i] < '0' || buf[i] > '9') return OK_FORMAT;
        status = status * 10 + buf[i] - '0';
    }
    eol = find(buf, n, "\r\n");
    r->status = status;
    r->headers = eol + 2;
    r->headers_len = (size_t)(end - eol); /* includes the last header's CRLF */
    head = (size_t)(end - buf) + 4;
    rest = n - head;
    r->body = buf + head;
    if (ok_http_header(r, "Transfer-Encoding", value, sizeof value) == OK_OK) {
        ok_result res;
        if (strcmp(value, "chunked") && strcmp(value, "Chunked")) return OK_FORMAT;
        res = dechunk(buf + head, rest, body_len);
        if (res != OK_OK) return res;
    } else if (ok_http_header(r, "Content-Length", value, sizeof value) == OK_OK) {
        size_t want = 0;
        for (i = 0; value[i]; i++) {
            if (value[i] < '0' || value[i] > '9' || i > 9) return OK_FORMAT;
            want = want * 10 + (size_t)(value[i] - '0');
        }
        if (!i) return OK_FORMAT;
        if (rest < want) return OK_RANGE;
        *body_len = want;
    } else {
        *body_len = rest; /* Connection: close delimits it; the caller reads to the end */
    }
    return OK_OK;
}
