#include "okupa_think.h"
#include "okupa_http.h"
#include "okupa_json.h"
#include "okupa_keys.h"
#include "okupa_sha256.h"
#include <stdio.h>
#include <string.h>

static const char *stages[] = {"decided", "setup", "net", "http", "no_chat_id", "signature_net",
                               "signature_format", "unverified", "answer"};

const char *ok_think_stage_name(ok_think_stage s)
{
    return (unsigned)s < sizeof stages / sizeof stages[0] ? stages[s] : "?";
}

/* Characters we let into a URL path or query without escaping. */
static int url_safe(const char *s, int allow_slash)
{
    if (!s || !*s) return 0;
    for (; *s; s++) {
        char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              c == '.' || (allow_slash && c == '/')))
            return 0;
    }
    return 1;
}

/* The request as the broker forwards it: the one "model" member of ours,
 * renamed to the upstream id from the response (unchanged if none). Our
 * request is already in Go's canonical form, so nothing else moves. */
static int forward(ok_think *t, const char *model)
{
    char from[96], to[96];
    const char *at;
    size_t a, b;
    if (!t->upstream_model[0] || !strcmp(t->upstream_model, model)) {
        memcpy(t->forwarded, t->request, t->request_len);
        t->forwarded_len = t->request_len;
        return 1;
    }
    snprintf(from, sizeof from, ",\"model\":\"%s\"", model);
    snprintf(to, sizeof to, ",\"model\":\"%s\"", t->upstream_model);
    a = strlen(from);
    b = strlen(to);
    if (!(at = strstr(t->request, from)) || strstr(at + 1, from)) return 0; /* exactly one */
    if (t->request_len - a + b > sizeof t->forwarded) return 0;
    memcpy(t->forwarded, t->request, (size_t)(at - t->request));
    memcpy(t->forwarded + (at - t->request), to, b);
    memcpy(t->forwarded + (at - t->request) + b, at + a, t->request_len - (size_t)(at - t->request) - a);
    t->forwarded_len = t->request_len - a + b;
    return 1;
}

static ok_think_stage stop(ok_think *t, ok_think_stage s)
{
    t->stage = s;
    memset(&t->decision, 0, sizeof t->decision);
    t->decision.activity = OK_ACT_NAP; /* the safe stop: no answer, the okupa naps */
    t->decision.mood = OK_FEEL_SLEEPY;
    return s;
}

ok_result ok_think_run(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                       const uint8_t nonce[16], const ok_pet *pet, int hour, int minute, const char *last_action,
                       uint32_t minutes_away, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    static char token[640], headers[800], req[OK_THINK_REQUEST + 1024], path[256];
    ok_zerog_session s;
    ok_http_response r;
    ok_json root, v;
    size_t n, got, body;
    int e;

    if (!t) return OK_ARGUMENT;
    memset(t, 0, sizeof *t);
    stop(t, OK_THINK_SETUP);
    if (!p || !p->host || !p->provider || !device_key || !nonce || !pet || !fetch || !url_safe(p->host, 0) ||
        !url_safe(p->model, 1))
        return OK_ARGUMENT;
    ok_keys_checksum(p->signer, t->signer);

    if (ok_mind_request(p->model, pet, hour, minute, last_action, minutes_away, t->request, sizeof t->request,
                        &t->request_len) != OK_OK)
        return OK_RANGE;
    s.provider = p->provider;
    s.timestamp_ms = now_ms;
    s.expires_ms = now_ms + 3600000u; /* one hour: the token only lives for this decision */
    memcpy(s.nonce, nonce, 16);
    s.generation = p->generation;
    s.token_id = 255; /* ephemeral */
    if (ok_zerog_token(device_key, &s, token, sizeof token, &n) != OK_OK) return OK_CRYPTO;
    snprintf(headers, sizeof headers, "Content-Type: application/json\r\nAuthorization: Bearer %s\r\n", token);
    ok_wipe(token, sizeof token);
    if (ok_http_request("POST", p->host, "/v1/proxy/chat/completions", headers, t->request, t->request_len, req,
                        sizeof req, &n) != OK_OK)
        return OK_RANGE;
    ok_wipe(headers, sizeof headers);

    /* 1. The decision itself. */
    e = fetch(fetch_ctx, p->host, req, n, t->wire, sizeof t->wire - 1, &got);
    ok_wipe(req, sizeof req);
    if (e) {
        t->net_error = e;
        stop(t, OK_THINK_NET);
        return OK_OK;
    }
    if (ok_http_response_read(t->wire, got, &r, &body) != OK_OK) {
        stop(t, OK_THINK_NET);
        return OK_OK;
    }
    t->http_status = r.status;
    t->response = r.body;
    t->response_len = body;
    if (r.status != 200) {
        stop(t, OK_THINK_HTTP);
        return OK_OK;
    }
    if (ok_http_header(&r, "ZG-Res-Key", t->chat_id, sizeof t->chat_id) != OK_OK || !url_safe(t->chat_id, 0)) {
        /* Fall back to the completion id, as the SDK does. */
        if (ok_json_parse(t->response, body, &root) != OK_OK || ok_json_member(&root, "id", &v) != OK_OK ||
            ok_json_string(&v, t->chat_id, sizeof t->chat_id, &n) != OK_OK || !url_safe(t->chat_id, 0)) {
            t->chat_id[0] = 0;
            stop(t, OK_THINK_NO_ID);
            return OK_OK;
        }
    }

    /* The upstream model the response names (it is inside the signed bytes). */
    if (ok_json_parse(t->response, body, &root) == OK_OK && ok_json_member(&root, "model", &v) == OK_OK &&
        (ok_json_string(&v, t->upstream_model, sizeof t->upstream_model, &n) != OK_OK ||
         !url_safe(t->upstream_model, 1)))
        t->upstream_model[0] = 0;

    /* 2. The TEE's signature for this chat id. */
    snprintf(path, sizeof path, "/v1/proxy/signature/%s?model=%s", t->chat_id, p->model);
    if (ok_http_request("GET", p->host, path, NULL, NULL, 0, req, sizeof req, &n) != OK_OK) return OK_RANGE;
    e = fetch(fetch_ctx, p->host, req, n, t->sigwire, sizeof t->sigwire - 1, &got);
    if (e || ok_http_response_read(t->sigwire, got, &r, &body) != OK_OK || r.status != 200) {
        t->net_error = e;
        stop(t, OK_THINK_SIG_NET);
        return OK_OK;
    }
    if (ok_json_parse(r.body, body, &root) != OK_OK || ok_json_member(&root, "text", &v) != OK_OK ||
        ok_json_string(&v, t->text, sizeof t->text, &n) != OK_OK || n != OK_ZEROG_TEXT ||
        ok_json_member(&root, "signature", &v) != OK_OK ||
        ok_json_string(&v, t->signature, sizeof t->signature, &n) != OK_OK) {
        stop(t, OK_THINK_SIG_FORMAT);
        return OK_OK;
    }

    /* 3. Our bytes, their signature, the signer we trust. */
    if (!forward(t, p->model) ||
        ok_zerog_verify((const uint8_t *)t->forwarded, t->forwarded_len, (const uint8_t *)t->response,
                        t->response_len, t->text, strlen(t->text), t->signature, p->signer) != OK_OK) {
        stop(t, OK_THINK_UNVERIFIED);
        return OK_OK;
    }

    /* 4. Only a verified answer is read, and only from the closed sets. */
    if (ok_mind_parse(t->response, t->response_len, &t->decision) != OK_OK) {
        stop(t, OK_THINK_ANSWER);
        return OK_OK;
    }
    t->stage = OK_THINK_DECIDED;
    return OK_OK;
}

/* Appends s as a JSON string literal. */
static int put_str(char *out, size_t cap, size_t *o, const char *s, size_t n)
{
    size_t i;
    if (*o + 2 > cap) return 0;
    out[(*o)++] = '"';
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (*o + 8 > cap) return 0;
        if (c == '"' || c == '\\') {
            out[(*o)++] = '\\';
            out[(*o)++] = (char)c;
        } else if (c < 0x20) {
            *o += (size_t)snprintf(out + *o, cap - *o, "\\u%04x", c);
        } else {
            out[(*o)++] = (char)c;
        }
    }
    out[(*o)++] = '"';
    return 1;
}

static int put(char *out, size_t cap, size_t *o, const char *s)
{
    size_t k = strlen(s);
    if (*o + k + 1 > cap) return 0;
    memcpy(out + *o, s, k);
    *o += k;
    return 1;
}

ok_result ok_think_episode(const ok_think *t, const ok_think_provider *p, uint64_t now_ms, char *out, size_t cap,
                           size_t *len)
{
    char head[256];
    size_t o = 0;
    int decided;
    if (!t || !p || !out || !len) return OK_ARGUMENT;
    decided = t->stage == OK_THINK_DECIDED;
    snprintf(head, sizeof head, "{\"okupa_episode\":1,\"time_ms\":%llu,\"stage\":\"%s\",\"verified\":%s,\"http\":%d,",
             (unsigned long long)now_ms, ok_think_stage_name(t->stage),
             decided || t->stage == OK_THINK_ANSWER ? "true" : "false", t->http_status);
    if (!put(out, cap, &o, head) || !put(out, cap, &o, "\"provider\":") ||
        !put_str(out, cap, &o, p->provider, strlen(p->provider)) || !put(out, cap, &o, ",\"model\":") ||
        !put_str(out, cap, &o, p->model, strlen(p->model)) || !put(out, cap, &o, ",\"tee_signer\":") ||
        !put_str(out, cap, &o, t->signer, strlen(t->signer)) || !put(out, cap, &o, ",\"upstream_model\":") ||
        !put_str(out, cap, &o, t->upstream_model, strlen(t->upstream_model)) || !put(out, cap, &o, ",\"chat_id\":") ||
        !put_str(out, cap, &o, t->chat_id, strlen(t->chat_id)) || !put(out, cap, &o, ",\"decision\":"))
        return OK_RANGE;
    if (decided) {
        if (!put(out, cap, &o, "{\"activity\":") ||
            !put_str(out, cap, &o, ok_mind_activity_name(t->decision.activity),
                     strlen(ok_mind_activity_name(t->decision.activity))) ||
            !put(out, cap, &o, ",\"mood\":") ||
            !put_str(out, cap, &o, ok_mind_mood_name(t->decision.mood), strlen(ok_mind_mood_name(t->decision.mood))) ||
            !put(out, cap, &o, ",\"line\":") || !put_str(out, cap, &o, t->decision.line, strlen(t->decision.line)) ||
            !put(out, cap, &o, "}"))
            return OK_RANGE;
    } else if (!put(out, cap, &o, "{\"activity\":\"nap\",\"safe_stop\":true}")) {
        return OK_RANGE;
    }
    if (!put(out, cap, &o, ",\"signed_text\":") || !put_str(out, cap, &o, t->text, strlen(t->text)) ||
        !put(out, cap, &o, ",\"signature\":") || !put_str(out, cap, &o, t->signature, strlen(t->signature)) ||
        !put(out, cap, &o, ",\"request\":") || !put_str(out, cap, &o, t->request, t->request_len) ||
        !put(out, cap, &o, ",\"response\":") ||
        !put_str(out, cap, &o, t->response ? t->response : "", t->response ? t->response_len : 0) ||
        !put(out, cap, &o, "}"))
        return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
}

static const char *find(const char *s, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(s + i, needle, k)) return s + i;
    return NULL;
}

/* Whether a response is complete, without touching it (chunked bodies are
 * decoded in place later, once). */
static int complete(char *buf, size_t n)
{
    ok_http_response r;
    char value[32];
    const char *end = find(buf, n, "\r\n\r\n");
    size_t want = 0, i;
    if (!end || n < 12) return 0;
    r.headers = buf;
    r.headers_len = (size_t)(end - buf) + 2;
    if (ok_http_header(&r, "Transfer-Encoding", value, sizeof value) == OK_OK)
        return n >= 5 && !memcmp(buf + n - 5, "0\r\n\r\n", 5) && (n == (size_t)(end - buf) + 9 || buf[n - 6] == '\n');
    if (ok_http_header(&r, "Content-Length", value, sizeof value) != OK_OK) return 0;
    for (i = 0; value[i] >= '0' && value[i] <= '9' && i < 9; i++) want = want * 10 + (size_t)(value[i] - '0');
    return n - (size_t)(end - buf) - 4 >= want;
}

int ok_think_https(ok_tls *tls, const char *host, int64_t now, const uint8_t seed[32], ok_tls_read_fn rd,
                   ok_tls_write_fn wr, void *io, const char *req, size_t req_len, char *resp, size_t cap,
                   size_t *got)
{
    size_t n;
    *got = 0;
    ok_tls_open(tls, host, now, seed, 32, rd, wr, io);
    if (ok_tls_write(tls, req, req_len) != OK_OK) return ok_tls_error(tls) ? ok_tls_error(tls) : -1;
    while (*got < cap) {
        if (ok_tls_read(tls, resp + *got, cap - *got, &n) != OK_OK || n == 0) break; /* closed */
        *got += n;
        if (complete(resp, *got)) break;
    }
    if (!*got) return ok_tls_error(tls) ? ok_tls_error(tls) : -1;
    resp[*got] = 0; /* cap leaves room for it */
    return 0;
}
