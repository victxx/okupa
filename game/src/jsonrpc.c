#include "okupa_jsonrpc.h"
#include "okupa_http.h"
#include <stdio.h>
#include <string.h>

int ok_jsonrpc_call(ok_think_fetch_fn fetch, void *ctx, const char *host, const char *body, size_t body_len, char *buf,
                    size_t cap, ok_json *result, char *error, size_t error_cap)
{
    char head[512], headers[128];
    ok_http_response r;
    ok_json root, err;
    size_t n, got, len;
    int e;
    snprintf(headers, sizeof headers, "Content-Type: application/json\r\nContent-Length: %lu\r\n", (unsigned long)body_len);
    if (ok_http_request_alive("POST", host, "/", headers, NULL, 0, head, sizeof head, &n) != OK_OK) {
        snprintf(error, error_cap, "bad host");
        return 0;
    }
    e = fetch(ctx, host, head, n, body, body_len, buf, cap - 1, &got);
    if (e || ok_http_response_read(buf, got, &r, &len) != OK_OK || r.status != 200) {
        snprintf(error, error_cap, "%.20s: %s %d", host, e ? "net" : "http", e ? e : r.status);
        return 0;
    }
    r.body[len] = 0;
    if (ok_json_parse(r.body, len, &root) != OK_OK) {
        snprintf(error, error_cap, "bad JSON from %.20s", host);
        return 0;
    }
    if (ok_json_member(&root, "error", &err) == OK_OK) {
        ok_json m;
        char msg[48] = "";
        if (ok_json_member(&err, "message", &m) == OK_OK) ok_json_string(&m, msg, sizeof msg, &n);
        snprintf(error, error_cap, "%.60s", msg[0] ? msg : "RPC error");
        return 0;
    }
    if (ok_json_member(&root, "result", result) != OK_OK) {
        snprintf(error, error_cap, "no result from %.20s", host);
        return 0;
    }
    return 1;
}
