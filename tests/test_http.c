#include "okupa_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char out[512], buf[512], v[64];
    ok_http_response r;
    size_t n, body;
    const char *b = "{\"a\":1}";

    assert(ok_http_request("POST", "x.example", "/v1/proxy/chat/completions", "Authorization: Bearer app-sk-AAA\r\n"
                           "Content-Type: application/json\r\n", b, strlen(b), out, sizeof out, &n) == OK_OK);
    assert(!memcmp(out, "POST /v1/proxy/chat/completions HTTP/1.1\r\nHost: x.example\r\n", 58));
    assert(strstr(out, "Content-Length: 7\r\n\r\n{\"a\":1}") && n == strlen(out));
    assert(ok_http_request("GET", "x.example", "/q", NULL, NULL, 0, out, sizeof out, &n) == OK_OK);
    assert(n >= 4 && !memcmp(out + n - 4, "\r\n\r\n", 4) && !strstr(out, "Content-Length"));
    assert(ok_http_request("GET", "x.example", "/q", NULL, NULL, 0, out, 20, &n) == OK_RANGE);

    /* Content-Length, a header read case-insensitively, partial reads. */
    strcpy(buf, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nzg-res-key:  chat-42 \r\nContent-Length: 7\r\n\r\n{\"x\":2}");
    n = strlen(buf);
    assert(ok_http_response_read(buf, n - 1, &r, &body) == OK_RANGE);
    assert(ok_http_response_read(buf, 20, &r, &body) == OK_RANGE);
    assert(ok_http_response_read(buf, n, &r, &body) == OK_OK && r.status == 200 && body == 7 && !memcmp(r.body, "{\"x\":2}", 7));
    assert(ok_http_header(&r, "ZG-Res-Key", v, sizeof v) == OK_OK && !strcmp(v, "chat-42"));
    assert(ok_http_header(&r, "Content-Length", v, sizeof v) == OK_OK && !strcmp(v, "7"));
    assert(ok_http_header(&r, "X-Missing", v, sizeof v) == OK_FORMAT);

    /* Chunked, decoded in place; incomplete until the zero chunk. */
    strcpy(buf, "HTTP/1.1 402 Payment Required\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n{\"e\"\r\nA;x=1\r\n:\"no funds\r\n1\r\n}\r\n0\r\n\r\n");
    n = strlen(buf);
    assert(ok_http_response_read(buf, n - 7, &r, &body) == OK_RANGE);
    strcpy(buf, "HTTP/1.1 402 Payment Required\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n{\"e\"\r\nA;x=1\r\n:\"no funds\r\n1\r\n}\r\n0\r\n\r\n");
    assert(ok_http_response_read(buf, n, &r, &body) == OK_OK && r.status == 402);
    assert(body == 15 && !memcmp(r.body, "{\"e\":\"no funds}", 15));

    strcpy(buf, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n");
    assert(ok_http_response_read(buf, strlen(buf), &r, &body) == OK_FORMAT);
    strcpy(buf, "SMTP 220 hi\r\n\r\n");
    assert(ok_http_response_read(buf, strlen(buf), &r, &body) == OK_FORMAT);
    strcpy(buf, "HTTP/1.1 200 OK\r\nContent-Length: 12x\r\n\r\n");
    assert(ok_http_response_read(buf, strlen(buf), &r, &body) == OK_FORMAT);
    puts("http: request bytes, Content-Length and chunked bodies, headers by name, partial reads");
    {
        static char keep[256];
        size_t kn;
        assert(ok_http_request_alive("GET", "a.example", "/x", NULL, NULL, 0, keep, sizeof keep, &kn) == OK_OK);
        assert(strstr(keep, "\r\nConnection: keep-alive\r\n") && !strstr(keep, "close"));
    }
    return 0;
}
