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

/* Lets the other threads run now and then inside long loops (the DSi's are
 * cooperative: a mind that never yields freezes the game). */
#define BREATHE(i) \
    do { \
        if (ok_yield && ((i) & 8191) == 8191) ok_yield(); \
    } while (0)

/* sha256 in 8 KB pieces, breathing between them. */
static void sha_pieces(br_sha256_context *c, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    while (n) {
        size_t k = n < 8192 ? n : 8192;
        br_sha256_update(c, p, k);
        p += k;
        n -= k;
        if (ok_yield) ok_yield();
    }
}

static void sha_all(const void *data, size_t n, uint8_t out[32])
{
    br_sha256_context c;
    br_sha256_init(&c);
    sha_pieces(&c, data, n);
    br_sha256_out(&c, out);
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
        sha_pieces(&c, t->body, t->body_len);
        br_sha256_out(&c, t->req_sha);
        return 1;
    }
    snprintf(from, sizeof from, ",\"model\":\"%s\"", model);
    snprintf(to, sizeof to, ",\"model\":\"%s\"", t->upstream_model);
    a = strlen(from);
    if (!(at = find(t->body, t->body_len, from)) || find(at + 1, t->body_len - (size_t)(at + 1 - t->body), from))
        return 0; /* exactly one */
    sha_pieces(&c, t->body, (size_t)(at - t->body));
    br_sha256_update(&c, to, strlen(to));
    sha_pieces(&c, at + a, t->body_len - (size_t)(at - t->body) - a);
    br_sha256_out(&c, t->req_sha);
    return 1;
}

static ok_think_stage stop(ok_think *t, ok_think_stage s)
{
    t->stage = s;
    memset(&t->decision, 0, sizeof t->decision);
    t->decision.activity = OK_MIND_SLEEP; /* the safe stop: never handed to the game, recorded as "none" */
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

/* The picture in an image model's answer: data[0].b64_json, base64 (JSON may
 * write "/" as "\/": the backslashes are skipped). */
static int image_of(ok_think *t)
{
    ok_json root, v, item, b64;
    uint32_t acc = 0;
    size_t i, o = 0;
    int k = 0;
    if (ok_json_parse(t->response, t->response_len, &root) != OK_OK || ok_json_member(&root, "data", &v) != OK_OK ||
        ok_json_index(&v, 0, &item) != OK_OK || ok_json_member(&item, "b64_json", &b64) != OK_OK || b64.len < 3 ||
        b64.at[0] != '"')
        return 0;
    for (i = 1; i + 1 < b64.len; i++) {
        char c = b64.at[i];
        BREATHE(i);
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (c == '\\' || c == '=') continue;
        if (d < 0) return 0;
        acc = acc << 6 | (uint32_t)d;
        if (++k == 4) {
            if (o + 3 > t->image_cap) return 0;
            t->image[o++] = (uint8_t)(acc >> 16);
            t->image[o++] = (uint8_t)(acc >> 8);
            t->image[o++] = (uint8_t)acc;
            k = 0;
            acc = 0;
        }
    }
    if (k == 2 || k == 3) { /* the padded tail */
        if (o + (size_t)(k - 1) > t->image_cap) return 0;
        acc <<= 6 * (4 - k);
        t->image[o++] = (uint8_t)(acc >> 16);
        if (k == 3) t->image[o++] = (uint8_t)(acc >> 8);
    } else if (k) {
        return 0;
    }
    t->image_len = o;
    return o > 0;
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
        if (got >= sizeof t->wire - 1) t->net_error = -5; /* the answer did not fit */
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
    if (ok_yield) ok_yield();
    if (ok_personal_recover(t->text, OK_ZEROG_TEXT, t->sig, who) == OK_OK) ok_keys_checksum(who, t->recovered);

    /* 3. Our bytes, their signature, the signer we trust. An image model signs
     * the picture it made, not the JSON around it. */
    if (t->image) {
        if (!image_of(t)) {
            stop(t, OK_THINK_ANSWER);
            return OK_OK;
        }
        sha_all(t->image, t->image_len, t->resp_sha);
    } else {
        sha_all(t->response, t->response_len, t->resp_sha);
    }
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
    if (r == OK_OK && t->stage == OK_THINK_DECIDED) ok_mind_dream_parse(t->response, t->response_len, t->dream);
    return r;
}

ok_result ok_think_make(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const char *what, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, "object");
    if (!p || !p->model || !what) return OK_ARGUMENT;
    if (ok_make_object_request(p->model, what, t->request, sizeof t->request, &t->request_len) != OK_OK) return OK_RANGE;
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/chat/completions", "application/json", t->request,
             t->request_len, fetch, fetch_ctx);
    if (r == OK_OK && t->stage == OK_THINK_DECIDED && ok_make_object_parse(t->response, t->response_len, &t->object) != OK_OK)
        stop(t, OK_THINK_ANSWER);
    return r;
}

ok_result ok_think_draw(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const char *subject, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    ok_result r;
    if (!t) return OK_ARGUMENT;
    begin(t, "drawing");
    if (!p || !p->model || !subject) return OK_ARGUMENT;
    if (ok_make_drawing_request(p->model, subject, t->request, sizeof t->request, &t->request_len) != OK_OK)
        return OK_RANGE;
    r = call(t, p, device_key, now_ms, nonce, "/v1/proxy/chat/completions", "application/json", t->request,
             t->request_len, fetch, fetch_ctx);
    if (r == OK_OK && t->stage == OK_THINK_DECIDED &&
        ok_make_drawing_parse(t->response, t->response_len, t->drawing, t->drawing_name) != OK_OK)
        stop(t, OK_THINK_ANSWER);
    return r;
}

ok_result ok_think_dream(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                         const uint8_t nonce[16], const char *prompt, uint8_t *image, size_t cap, char *buf,
                         size_t buf_cap, ok_think_fetch_fn fetch, void *fetch_ctx)
{
    int k;
    if (!t) return OK_ARGUMENT;
    begin(t, "dream");
    if (!p || !p->model || !prompt || !*prompt || !image || !buf || strpbrk(prompt, "\"\\<>&\r\n") ||
        strpbrk(p->model, "\"\\<>&"))
        return OK_ARGUMENT;
    t->image = image;
    t->image_cap = cap;
    /* Keys sorted, no spaces: the same bytes whether or not they are re-encoded. */
    k = snprintf(buf, buf_cap,
                 "{\"model\":\"%s\",\"n\":1,\"output_format\":\"png\",\"prompt\":\"%s\",\"response_format\":\"b64_json\","
                 "\"size\":\"128x128\"}",
                 p->model, prompt);
    if (k < 0 || (size_t)k >= buf_cap) return OK_RANGE;
    snprintf(t->dream, sizeof t->dream, "%s", prompt);
    return call(t, p, device_key, now_ms, nonce, "/v1/proxy/images/generations", "application/json", buf, (size_t)k,
                fetch, fetch_ctx);
}

/* Keeps printable ASCII only (accents dropped): what the okupa heard or saw goes back into its
 * next request, and onto a small screen. */
static void plain(const char *in, size_t n, char *out, size_t cap)
{
    size_t i, o = 0, used;
    for (i = 0; i < n && o + 1 < cap; i += used) {
        int c = ok_mind_ascii(in + i, n - i, &used); /* accents dropped: "que tal", not "qu tal" */
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
        uint32_t v;
        if (ok_yield && i % 6144 == 6141) ok_yield();
        v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = a[v >> 18 & 63];
        out[o++] = a[v >> 12 & 63];
        out[o++] = i + 1 < n ? a[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? a[v & 63] : '=';
    }
    return o;
}

static const char EYES[] =
    "You are the eyes of Okupa, a small cheeky monkey living inside a Nintendo DSi, looking through its camera. "
    "Reply with two parts separated by a vertical bar. First: what you really see in at most 12 words of plain "
    "English, concrete things first: the person (expression, what they hold or wear) and the main objects around "
    "them; if it is too dark or blurry to tell, say so. Then a vertical bar. Then what Okupa says to the player "
    "about the picture, at most 40 characters: a playful roast or a backhanded compliment about what is really in "
    "it, a little mean and funny, slang welcome (like: you look rad af, sort of), never about body, weight, race or "
    "anything hurtful. No preamble, no quotes.";

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
    ok_result r = look(t, p, device_key, now_ms, nonce, "sight", EYES, "What do you see?", 100, png, png_len, buf,
                       cap, fetch, fetch_ctx);
    char *bar;
    size_t i, n;
    t->quip[0] = 0;
    /* "what it sees | what Okupa says": the line is only decoration. An
     * answer without the bar is still what it saw, with nothing to say. */
    if (r == OK_OK && t->stage == OK_THINK_DECIDED && (bar = strchr(t->saw, '|')) != NULL) {
        const char *q = bar + 1;
        *bar = 0;
        while (*q == ' ' || *q == '"') q++;
        snprintf(t->quip, sizeof t->quip, "%s", q);
        n = strlen(t->quip);
        while (n && (t->quip[n - 1] == ' ' || t->quip[n - 1] == '"' || t->quip[n - 1] == '|')) t->quip[--n] = 0;
        for (i = 0; t->quip[i]; i++)
            if (t->quip[i] == '|') t->quip[i] = ' ';
        n = strlen(t->saw);
        while (n && t->saw[n - 1] == ' ') t->saw[--n] = 0;
        if (!t->saw[0]) stop(t, OK_THINK_ANSWER);
    }
    return r;
}

static const char COMMENT[] =
    "You are Okupa, a small cheeky, affectionate monkey who lives in a Nintendo DSi. The player just drew this for "
    "you on the tiny 64x64 sketchbook and is showing it to you. React to what is really in the drawing in "
    "one short, warm, specific line in English, at most 40 characters, as Okupa. Only the line, no quotes.";

ok_result ok_think_comment(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                           const uint8_t nonce[16], const uint8_t *png, size_t png_len, char *buf, size_t cap,
                           ok_think_fetch_fn fetch, void *fetch_ctx)
{
    return look(t, p, device_key, now_ms, nonce, "comment", COMMENT, "What do you think of my drawing?", 40, png,
                png_len, buf, cap, fetch, fetch_ctx);
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

/* Whether a response is complete, without touching it (chunked bodies are
 * decoded in place later, once). */
int ok_think_complete(char *buf, size_t n)
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
        if (ok_yield) ok_yield(); /* each record was decrypted and checked: breathe */
        if (ok_think_complete(resp, *got)) break;
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
