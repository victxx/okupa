#include "okupa_think.h"
#include "bearssl_hash.h"
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

static const char *find(const char *s, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(s + i, needle, k)) return s + i;
    return NULL;
}

/* sha256 of what the TEE hashed. A JSON body is forwarded by the broker with
 * Go's encoding/json (we build it in that canonical form already) and its one
 * top-level "model" renamed to the upstream id the response names; anything
 * else (audio) goes through as is. 0 if the model cannot be placed exactly. */
static int forwarded_sha(ok_think *t, const char *model)
{
    br_sha256_context c;
    char from[96], to[96];
    const char *at;
    size_t a;
    br_sha256_init(&c);
    if (!t->body_json || !t->upstream_model[0] || !strcmp(t->upstream_model, model)) {
        br_sha256_update(&c, t->body, t->body_len);
        br_sha256_out(&c, t->req_sha);
        return 1;
    }
    snprintf(from, sizeof from, ",\"model\":\"%s\"", model);
    snprintf(to, sizeof to, ",\"model\":\"%s\"", t->upstream_model);
    a = strlen(from);
    if (!(at = find(t->body, t->body_len, from)) || find(at + 1, t->body_len - (size_t)(at + 1 - t->body), from))
        return 0; /* exactly one */
    br_sha256_update(&c, t->body, (size_t)(at - t->body));
    br_sha256_update(&c, to, strlen(to));
    br_sha256_update(&c, at + a, t->body_len - (size_t)(at - t->body) - a);
    br_sha256_out(&c, t->req_sha);
    return 1;
}

static ok_think_stage stop(ok_think *t, ok_think_stage s)
{
    t->stage = s;
    memset(&t->decision, 0, sizeof t->decision);
    t->decision.activity = OK_MIND_SLEEP; /* the safe stop: no answer, the okupa sleeps */
    t->decision.mood = OK_FEEL_SLEEPY;
    return s;
}

static int unhex_sig(const char *sig, uint8_t out[65])
{
    const char *h = sig + (sig[0] == '0' && sig[1] == 'x' ? 2 : 0);
    int i, k;
    if (strlen(h) != 130) return 0;
    for (i = 0; i < 65; i++) {
        unsigned v = 0;
        for (k = 0; k < 2; k++) {
            char c = h[2 * i + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return 0;
            v = v << 4 | (unsigned)d;
        }
        out[i] = (uint8_t)v;
    }
    return 1;
}

static void hex(const uint8_t *in, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
}

static void begin(ok_think *t, const char *kind)
{
    memset(t, 0, sizeof *t);
    stop(t, OK_THINK_SETUP);
    t->kind = kind;
}

static ok_result call(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                      const uint8_t nonce[16], const char *path, const char *content_type, const char *body,
                      size_t body_len, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    static char token[640], headers[1024], head[1536], spath[256];
    char want[OK_ZEROG_TEXT + 1];
    uint8_t who[20];
    ok_zerog_session s;
    ok_http_response r;
    ok_json root, v;
    size_t n, got, len;
    int e;

    if (!p || !p->host || !p->provider || !device_key || !nonce || !path || !content_type || !body || !fetch ||
        !url_safe(p->host, 0) || !url_safe(p->model, 1) || strpbrk(content_type, "\r\n"))
        return OK_ARGUMENT;
    ok_keys_checksum(p->signer, t->signer);
    t->body = body;
    t->body_len = body_len;
    t->body_json = !strncmp(content_type, "application/json", 16);

    s.provider = p->provider;
    s.timestamp_ms = now_ms;
    s.expires_ms = now_ms + 3600000u; /* one hour: the token only lives for this call */
    memcpy(s.nonce, nonce, 16);
    s.generation = p->generation;
    s.token_id = 255; /* ephemeral */
    if (ok_zerog_token(device_key, &s, token, sizeof token, &n) != OK_OK) return OK_CRYPTO;
    snprintf(headers, sizeof headers, "Content-Type: %s\r\nContent-Length: %lu\r\nAuthorization: Bearer %s\r\n",
             content_type, (unsigned long)body_len, token);
    ok_wipe(token, sizeof token);
    if (ok_http_request_alive("POST", p->host, path, headers, NULL, 0, head, sizeof head, &n) != OK_OK) return OK_RANGE;
    ok_wipe(headers, sizeof headers);

    /* 1. The call itself. */
    e = fetch(fetch_ctx, p->host, head, n, body, body_len, t->wire, sizeof t->wire - 1, &got);
    ok_wipe(head, sizeof head);
    if (e) {
        t->net_error = e;
        stop(t, OK_THINK_NET);
        return OK_OK;
    }
    if (ok_http_response_read(t->wire, got, &r, &len) != OK_OK) {
        stop(t, OK_THINK_NET);
        return OK_OK;
    }
    t->http_status = r.status;
    t->response = r.body;
    t->response_len = len;
    if (r.status != 200) {
        stop(t, OK_THINK_HTTP);
        return OK_OK;
    }
    if (ok_http_header(&r, "ZG-Res-Key", t->chat_id, sizeof t->chat_id) != OK_OK || !url_safe(t->chat_id, 0)) {
        /* Fall back to the completion id, as the SDK does. */
        if (ok_json_parse(t->response, len, &root) != OK_OK || ok_json_member(&root, "id", &v) != OK_OK ||
            ok_json_string(&v, t->chat_id, sizeof t->chat_id, &n) != OK_OK || !url_safe(t->chat_id, 0)) {
            t->chat_id[0] = 0;
            stop(t, OK_THINK_NO_ID);
            return OK_OK;
        }
    }
    /* The upstream model the response names (it is inside the signed bytes). */
    if (ok_json_parse(t->response, len, &root) == OK_OK && ok_json_member(&root, "model", &v) == OK_OK &&
        (ok_json_string(&v, t->upstream_model, sizeof t->upstream_model, &n) != OK_OK ||
         !url_safe(t->upstream_model, 1)))
        t->upstream_model[0] = 0;

    /* 2. The TEE's signature for this chat id. */
    snprintf(spath, sizeof spath, "/v1/proxy/signature/%s?model=%s", t->chat_id, p->model);
    if (ok_http_request_alive("GET", p->host, spath, NULL, NULL, 0, head, sizeof head, &n) != OK_OK) return OK_RANGE;
    e = fetch(fetch_ctx, p->host, head, n, NULL, 0, t->sigwire, sizeof t->sigwire - 1, &got);
    if (e || ok_http_response_read(t->sigwire, got, &r, &len) != OK_OK || r.status != 200) {
        t->net_error = e;
        stop(t, OK_THINK_SIG_NET);
        return OK_OK;
    }
    if (ok_json_parse(r.body, len, &root) != OK_OK || ok_json_member(&root, "text", &v) != OK_OK ||
        ok_json_string(&v, t->text, sizeof t->text, &n) != OK_OK || n != OK_ZEROG_TEXT ||
        ok_json_member(&root, "signature", &v) != OK_OK ||
        ok_json_string(&v, t->signature, sizeof t->signature, &n) != OK_OK || !unhex_sig(t->signature, t->sig)) {
        stop(t, OK_THINK_SIG_FORMAT);
        return OK_OK;
    }
    /* Who signed it, whatever it says (for the screens and the episode). */
    if (ok_personal_recover(t->text, OK_ZEROG_TEXT, t->sig, who) == OK_OK) ok_keys_checksum(who, t->recovered);

    /* 3. Our bytes, their signature, the signer we trust. */
    ok_sha256(t->response, t->response_len, t->resp_sha);
    if (!forwarded_sha(t, p->model)) {
        stop(t, OK_THINK_UNVERIFIED);
        return OK_OK;
    }
    hex(t->req_sha, 32, want);
    want[64] = ':';
    hex(t->resp_sha, 32, want + 65);
    want[OK_ZEROG_TEXT] = 0;
    if (memcmp(want, t->text, OK_ZEROG_TEXT) || !t->recovered[0] || memcmp(who, p->signer, 20)) {
        stop(t, OK_THINK_UNVERIFIED);
        return OK_OK;
    }
    t->stage = OK_THINK_DECIDED;
    return OK_OK;
}

ok_result ok_think_call(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const char *path, const char *content_type, const char *body,
                        size_t body_len, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    if (!t) return OK_ARGUMENT;
    begin(t, "call");
    return call(t, p, device_key, now_ms, nonce, path, content_type, body, body_len, fetch, fetch_ctx);
}

ok_result ok_think_run(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                       const uint8_t nonce[16], const ok_mind_obs *obs, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, "decision");
    if (!p || !p->model || !obs) return OK_ARGUMENT;
    if (ok_mind_request(p->model, obs, t->request, sizeof t->request, &t->request_len) != OK_OK) return OK_RANGE;
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/chat/completions", "application/json", t->request,
                      t->request_len, fetch, fetch_ctx);
    if (r != OK_OK || t->stage != OK_THINK_DECIDED) return r;
    /* 4. Only a verified answer is read, and only from the closed sets. */
    if (ok_mind_parse(t->response, t->response_len, &t->decision) != OK_OK) stop(t, OK_THINK_ANSWER);
    return OK_OK;
}

ok_result ok_think_plan(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const ok_mind_obs *obs, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, "plan");
    if (!p || !p->model || !obs) return OK_ARGUMENT;
    if (ok_mind_plan_request(p->model, obs, t->request, sizeof t->request, &t->request_len) != OK_OK) return OK_RANGE;
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/chat/completions", "application/json", t->request,
             t->request_len, fetch, fetch_ctx);
    if (r == OK_OK && t->stage == OK_THINK_DECIDED && ok_mind_plan_parse(t->response, t->response_len, &t->plan) != OK_OK)
        stop(t, OK_THINK_ANSWER);
    return r;
}

ok_result ok_think_diary(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                         const uint8_t nonce[16], const ok_mind_obs *obs, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, "diary");
    if (!p || !p->model || !obs) return OK_ARGUMENT;
    if (ok_mind_diary_request(p->model, obs, t->request, sizeof t->request, &t->request_len) != OK_OK) return OK_RANGE;
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/chat/completions", "application/json", t->request,
             t->request_len, fetch, fetch_ctx);
    if (r == OK_OK && t->stage == OK_THINK_DECIDED && ok_mind_diary_parse(t->response, t->response_len, t->diary) != OK_OK)
        stop(t, OK_THINK_ANSWER);
    return r;
}

/* Keeps printable ASCII only: what the okupa heard or saw goes back into its
 * next request, and onto a small screen. */
static void plain(const char *in, size_t n, char *out, size_t cap)
{
    size_t i, o = 0;
    for (i = 0; i < n && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 0x20 && c < 0x7f && !(c == ' ' && (o == 0 || out[o - 1] == ' '))) out[o++] = (char)c;
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

ok_result ok_think_hear(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const uint8_t *wav, size_t wav_len, char *buf, size_t cap,
                        ok_think_fetch_fn fetch, void *fetch_ctx)
{
    static const char B[] = "okupa0ear0boundary";
    static char text[1024];
    char type[64];
    ok_json root, v;
    size_t o, n;
    int k;
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, "hearing");
    if (!p || !p->model || !wav || !buf || strpbrk(p->model, "\"\r\n")) return OK_ARGUMENT;
    k = snprintf(buf, cap,
                 "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
                 "--%s\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
                 "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"okupa.wav\"\r\n"
                 "Content-Type: audio/wav\r\n\r\n",
                 B, p->model, B, B);
    if (k < 0 || (size_t)k + wav_len + sizeof B + 8 > cap) return OK_RANGE;
    o = (size_t)k;
    memcpy(buf + o, wav, wav_len);
    o += wav_len;
    o += (size_t)snprintf(buf + o, cap - o, "\r\n--%s--\r\n", B);
    snprintf(type, sizeof type, "multipart/form-data; boundary=%s", B);
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/audio/transcriptions", type, buf, o, fetch,
                      fetch_ctx);
    if (r != OK_OK || t->stage != OK_THINK_DECIDED) return r;
    if (ok_json_parse(t->response, t->response_len, &root) != OK_OK || ok_json_member(&root, "text", &v) != OK_OK ||
        ok_json_string(&v, text, sizeof text, &n) != OK_OK) {
        stop(t, OK_THINK_ANSWER);
        return OK_OK;
    }
    plain(text, n, t->heard, sizeof t->heard);
    return OK_OK;
}

static size_t base64(const uint8_t *in, size_t n, char *out)
{
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = a[v >> 18 & 63];
        out[o++] = a[v >> 12 & 63];
        out[o++] = i + 1 < n ? a[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? a[v & 63] : '=';
    }
    return o;
}

static const char EYES[] =
    "You are the eyes of Okupa, a small monkey living inside a Nintendo DSi, looking through its camera. In one "
    "short sentence of plain English (at most 25 words), say what you see: people, faces and expressions, what "
    "they hold or wear, the room, the light. No preamble.";

static const char GUESS[] =
    "You are playing Pictionary with a child on a Nintendo DSi. The picture is a quick black-on-white doodle drawn "
    "with a stylus on a tiny 64x64 sketchbook. Guess what it shows. Reply with only your guess: one to three plain "
    "English words, lower case, no punctuation, no article.";

/* One picture to a vision model, in a canonical request (keys sorted, no
 * spaces, nothing Go would escape differently), and its one-line answer. */
static ok_result look(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                      const uint8_t nonce[16], const char *kind, const char *system, const char *question,
                      int max_tokens, const uint8_t *png, size_t png_len, char *buf, size_t cap,
                      ok_think_fetch_fn fetch, void *fetch_ctx)
{
    static char content[1024];
    ok_json root, v, choice, msg;
    size_t o, n, need;
    int k;
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, kind);
    if (!p || !p->model || !png || !buf || strpbrk(p->model, "\"\\<>&") || strpbrk(system, "\"\\<>&\n") ||
        strpbrk(question, "\"\\<>&\n"))
        return OK_ARGUMENT;
    need = (png_len + 2) / 3 * 4 + strlen(system) + strlen(question) + 512;
    if (need > cap) return OK_RANGE;
    k = snprintf(buf, cap,
                 "{\"chat_template_kwargs\":{\"enable_thinking\":false},\"max_tokens\":%d,\"messages\":[{\"content\":\"%s\","
                 "\"role\":\"system\"},{\"content\":[{\"image_url\":{\"url\":\"data:image/png;base64,",
                 max_tokens, system);
    if (k < 0 || (size_t)k >= cap) return OK_RANGE;
    o = (size_t)k;
    o += base64(png, png_len, buf + o);
    k = snprintf(buf + o, cap - o,
                 "\"},\"type\":\"image_url\"},{\"text\":\"%s\",\"type\":\"text\"}],\"role\":\"user\"}],"
                 "\"model\":\"%s\",\"temperature\":0.3}",
                 question, p->model);
    if (k < 0 || o + (size_t)k >= cap) return OK_RANGE;
    o += (size_t)k;
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/chat/completions", "application/json", buf, o, fetch,
             fetch_ctx);
    if (r != OK_OK || t->stage != OK_THINK_DECIDED) return r;
    if (ok_json_parse(t->response, t->response_len, &root) != OK_OK || ok_json_member(&root, "choices", &v) != OK_OK ||
        ok_json_index(&v, 0, &choice) != OK_OK || ok_json_member(&choice, "message", &msg) != OK_OK ||
        ok_json_member(&msg, "content", &v) != OK_OK || ok_json_string(&v, content, sizeof content, &n) != OK_OK) {
        stop(t, OK_THINK_ANSWER);
        return OK_OK;
    }
    plain(content, n, t->saw, sizeof t->saw);
    if (!t->saw[0]) stop(t, OK_THINK_ANSWER);
    return OK_OK;
}

ok_result ok_think_see(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                       const uint8_t nonce[16], const uint8_t *png, size_t png_len, char *buf, size_t cap,
                       ok_think_fetch_fn fetch, void *fetch_ctx)
{
    return look(t, p, device_key, now_ms, nonce, "sight", EYES, "What do you see?", 80, png, png_len, buf, cap, fetch,
                fetch_ctx);
}

ok_result ok_think_guess(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                         const uint8_t nonce[16], const uint8_t *png, size_t png_len, char *buf, size_t cap,
                         ok_think_fetch_fn fetch, void *fetch_ctx)
{
    ok_result r = look(t, p, device_key, now_ms, nonce, "guess", GUESS, "What did I draw?", 16, png, png_len, buf, cap,
                       fetch, fetch_ctx);
    size_t i;
    /* One to three words, lower case: anything longer is not a guess. */
    if (r == OK_OK && t->stage == OK_THINK_DECIDED) {
        int words = 1;
        for (i = 0; t->saw[i]; i++) {
            char c = t->saw[i];
            if (c >= 'A' && c <= 'Z') t->saw[i] = (char)(c + 32);
            if (c == '.' || c == '!' || c == '"') t->saw[i] = ' ';
            if (c == ' ') words++;
        }
        while (i && t->saw[i - 1] == ' ') t->saw[--i] = 0;
        if (words > 3 || !t->saw[0]) stop(t, OK_THINK_ANSWER);
    }
    return r;
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
        } else if (c < 0x20 || c >= 0x7f) {
            *o += (size_t)snprintf(out + *o, cap - *o, "\\u%04x", c); /* bytes, not UTF-8: exact either way */
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

static int put_b64(char *out, size_t cap, size_t *o, const char *s, size_t n)
{
    if (*o + (n + 2) / 3 * 4 + 3 > cap) return 0;
    out[(*o)++] = '"';
    *o += base64((const uint8_t *)s, n, out + *o);
    out[(*o)++] = '"';
    return 1;
}

ok_result ok_think_episode(const ok_think *t, const ok_think_provider *p, uint64_t now_ms, const char *prev,
                           char *out, size_t cap, size_t *len)
{
    char head[320];
    size_t o = 0;
    int ok = t && t->stage == OK_THINK_DECIDED;
    const char *kind;
    if (!t || !p || !prev || !out || !len) return OK_ARGUMENT;
    kind = t->kind ? t->kind : "call";
    snprintf(head, sizeof head,
             "{\"okupa_episode\":1,\"kind\":\"%s\",\"time_ms\":%llu,\"stage\":\"%s\",\"verified\":%s,\"http\":%d,"
             "\"net_error\":%d,",
             kind, (unsigned long long)now_ms, ok_think_stage_name(t->stage),
             ok || t->stage == OK_THINK_ANSWER ? "true" : "false", t->http_status, t->net_error);
    if (!put(out, cap, &o, head) || !put(out, cap, &o, "\"provider\":") ||
        !put_str(out, cap, &o, p->provider, strlen(p->provider)) || !put(out, cap, &o, ",\"model\":") ||
        !put_str(out, cap, &o, p->model, strlen(p->model)) || !put(out, cap, &o, ",\"tee_signer\":") ||
        !put_str(out, cap, &o, t->signer, strlen(t->signer)) || !put(out, cap, &o, ",\"recovered_signer\":") ||
        !put_str(out, cap, &o, t->recovered, strlen(t->recovered)) || !put(out, cap, &o, ",\"prev_sha256\":") ||
        !put_str(out, cap, &o, prev, strlen(prev)) || !put(out, cap, &o, ",\"upstream_model\":") ||
        !put_str(out, cap, &o, t->upstream_model, strlen(t->upstream_model)) || !put(out, cap, &o, ",\"chat_id\":") ||
        !put_str(out, cap, &o, t->chat_id, strlen(t->chat_id)))
        return OK_RANGE;
    if (!strcmp(kind, "decision")) {
        if (!put(out, cap, &o, ",\"decision\":")) return OK_RANGE;
        if (ok) {
            if (!put(out, cap, &o, "{\"activity\":") ||
                !put_str(out, cap, &o, ok_mind_activity_name(t->decision.activity),
                         strlen(ok_mind_activity_name(t->decision.activity))) ||
                !put(out, cap, &o, ",\"mood\":") ||
                !put_str(out, cap, &o, ok_mind_mood_name(t->decision.mood), strlen(ok_mind_mood_name(t->decision.mood))) ||
                !put(out, cap, &o, ",\"line\":") || !put_str(out, cap, &o, t->decision.line, strlen(t->decision.line)) ||
                !put(out, cap, &o, ",\"thought\":") ||
                !put_str(out, cap, &o, t->decision.thought, strlen(t->decision.thought)) ||
                !put(out, cap, &o, ",\"ask\":") || !put_str(out, cap, &o, t->decision.ask, strlen(t->decision.ask)) ||
                !put(out, cap, &o, ",\"remember\":") ||
                !put_str(out, cap, &o, t->decision.remember, strlen(t->decision.remember)))
                return OK_RANGE;
            snprintf(head, sizeof head, ",\"x\":%d,\"z\":%d,\"gaze\":\"%s\",\"energy\":%d}", t->decision.x, t->decision.z,
                     ok_mind_gaze_name(t->decision.gaze), t->decision.energy);
            if (!put(out, cap, &o, head))
                return OK_RANGE;
        } else if (!put(out, cap, &o, "{\"activity\":\"sleep\",\"safe_stop\":true}")) {
            return OK_RANGE;
        }
    } else if (!strcmp(kind, "hearing")) {
        if (!put(out, cap, &o, ",\"heard\":") || !put_str(out, cap, &o, t->heard, strlen(t->heard))) return OK_RANGE;
    } else if (!strcmp(kind, "plan")) {
        if (!put(out, cap, &o, ",\"goal\":") || !put_str(out, cap, &o, t->plan.goal, strlen(t->plan.goal)) ||
            !put(out, cap, &o, ",\"plan\":") || !put_str(out, cap, &o, t->plan.plan, strlen(t->plan.plan)) ||
            !put(out, cap, &o, ",\"feeling\":") || !put_str(out, cap, &o, t->plan.feeling, strlen(t->plan.feeling)))
            return OK_RANGE;
    } else if (!strcmp(kind, "diary")) {
        if (!put(out, cap, &o, ",\"entry\":") || !put_str(out, cap, &o, t->diary, strlen(t->diary))) return OK_RANGE;
    } else if (!strcmp(kind, "sight") || !strcmp(kind, "guess")) {
        if (!put(out, cap, &o, !strcmp(kind, "guess") ? ",\"guess\":" : ",\"saw\":") ||
            !put_str(out, cap, &o, t->saw, strlen(t->saw)))
            return OK_RANGE;
    }
    if (!put(out, cap, &o, ",\"signed_text\":") || !put_str(out, cap, &o, t->text, strlen(t->text)) ||
        !put(out, cap, &o, ",\"signature\":") || !put_str(out, cap, &o, t->signature, strlen(t->signature)))
        return OK_RANGE;
    if (t->body_json) {
        if (!put(out, cap, &o, ",\"request\":") || !put_str(out, cap, &o, t->body ? t->body : "", t->body_len))
            return OK_RANGE;
    } else if (!put(out, cap, &o, ",\"request_b64\":") || !put_b64(out, cap, &o, t->body ? t->body : "", t->body_len)) {
        return OK_RANGE;
    }
    if (!put(out, cap, &o, ",\"response\":") ||
        !put_str(out, cap, &o, t->response ? t->response : "", t->response ? t->response_len : 0) ||
        !put(out, cap, &o, "}"))
        return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
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

int ok_think_exchange(ok_tls *tls, const char *head, size_t head_len, const char *body, size_t body_len, char *resp,
                      size_t cap, size_t *got)
{
    size_t n;
    *got = 0;
    if (ok_tls_write(tls, head, head_len) != OK_OK || (body && body_len && ok_tls_write(tls, body, body_len) != OK_OK))
        return ok_tls_error(tls) ? ok_tls_error(tls) : -1;
    while (*got < cap) {
        if (ok_tls_read(tls, resp + *got, cap - *got, &n) != OK_OK || n == 0) break; /* closed */
        *got += n;
        if (complete(resp, *got)) break;
    }
    if (!*got) return ok_tls_error(tls) ? ok_tls_error(tls) : -1;
    resp[*got] = 0; /* cap leaves room for it */
    return 0;
}

int ok_think_https(ok_tls *tls, const char *host, int64_t now, const uint8_t seed[32], ok_tls_read_fn rd,
                   ok_tls_write_fn wr, void *io, const char *head, size_t head_len, const char *body,
                   size_t body_len, char *resp, size_t cap, size_t *got)
{
    ok_tls_open(tls, host, now, seed, 32, rd, wr, io);
    return ok_think_exchange(tls, head, head_len, body, body_len, resp, cap, got);
}

int ok_think_keeps_open(const char *resp, size_t n)
{
    ok_http_response r;
    char value[32];
    const char *end = find(resp, n, "\r\n\r\n");
    size_t i;
    if (!end || n < 12 || memcmp(resp, "HTTP/1.1", 8)) return 0;
    r.headers = resp;
    r.headers_len = (size_t)(end - resp) + 2;
    if (ok_http_header(&r, "Connection", value, sizeof value) != OK_OK) return 1;
    for (i = 0; value[i]; i++) value[i] = (char)(value[i] >= 'A' && value[i] <= 'Z' ? value[i] + 32 : value[i]);
    return strcmp(value, "close") != 0;
}
