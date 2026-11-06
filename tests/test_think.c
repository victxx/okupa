#include "okupa_json.h"
#include "okupa_keys.h"
#include "okupa_think.h"
#include "okupa_zerog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A fake 0G provider: answers the chat with a fixed content, then signs
 * sha256(request):sha256(response) with its TEE key, as the broker does. */
static struct {
    int fail_net, status, tamper, no_id;
    const char *upstream; /* model id the broker forwards, NULL = unchanged */
    const char *content;
    char req_body[8192];
    size_t req_len;
    char resp_body[4096];
    size_t resp_len;
    uint8_t tee[32];
    int calls;
    char last_auth[1024];
} fake;

static int fetch(void *ctx, const char *host, const char *req, size_t n, char *resp, size_t cap, size_t *got)
{
    const char *body = strstr(req, "\r\n\r\n");
    int k;
    (void)ctx;
    assert(!strcmp(host, "tee.example"));
    fake.calls++;
    if (fake.fail_net) return 62;
    if (!strncmp(req, "POST /v1/proxy/chat/completions ", 32)) {
        const char *a = strstr(req, "Authorization: Bearer app-sk-");
        assert(a && body);
        snprintf(fake.last_auth, sizeof fake.last_auth, "%.*s", (int)(strstr(a, "\r\n") - a), a);
        fake.req_len = n - (size_t)(body + 4 - req);
        memcpy(fake.req_body, body + 4, fake.req_len);
        fake.req_body[fake.req_len] = 0;
        if (fake.upstream) { /* the broker renames the model before the TEE hashes it */
            static char tmp[8192];
            char *at = strstr(fake.req_body, ",\"model\":\"0GM-1.0\"");
            assert(at);
            fake.req_len = (size_t)snprintf(tmp, sizeof tmp, "%.*s,\"model\":\"%s\"%s", (int)(at - fake.req_body),
                                            fake.req_body, fake.upstream, at + strlen(",\"model\":\"0GM-1.0\""));
            memcpy(fake.req_body, tmp, fake.req_len + 1);
        }
        fake.resp_len = (size_t)snprintf(fake.resp_body, sizeof fake.resp_body,
                                         "{\"id\":\"chatcmpl-9\",\"choices\":[{\"message\":{\"role\":\"assistant\","
                                         "\"content\":\"%s\"}}],\"model\":\"%s\",\"usage\":{\"prompt_tokens\":9}}",
                                         fake.content, fake.upstream ? fake.upstream : "0GM-1.0");
        k = snprintf(resp, cap, "HTTP/1.1 %d X\r\n%sContent-Length: %lu\r\n\r\n%s", fake.status,
                     fake.no_id ? "" : "ZG-Res-Key: chat-abc_1\r\n", (unsigned long)fake.resp_len, fake.resp_body);
    } else {
        char text[OK_ZEROG_TEXT + 1], sig_hex[131], json[512];
        uint8_t sig[65];
        int i;
        assert(!strncmp(req, "GET /v1/proxy/signature/", 24) && strstr(req, "?model=0GM-1.0 HTTP/1.1\r\n"));
        if (fake.tamper) fake.resp_body[0] = ' ';
        ok_zerog_text((const uint8_t *)fake.req_body, fake.req_len, (const uint8_t *)fake.resp_body, fake.resp_len,
                      text);
        assert(ok_personal_sign(text, OK_ZEROG_TEXT, fake.tee, sig) == OK_OK);
        for (i = 0; i < 65; i++) snprintf(sig_hex + 2 * i, 3, "%02x", sig[i]);
        snprintf(json, sizeof json, "{\"text\":\"%s\",\"signature\":\"0x%s\"}", text, sig_hex);
        k = snprintf(resp, cap, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%x\r\n%s\r\n0\r\n\r\n",
                     (unsigned)strlen(json), json);
    }
    assert(k > 0 && (size_t)k < cap);
    *got = (size_t)k;
    return 0;
}

static ok_think t;
static char episode[65536];

static ok_result run(ok_think_provider *p, const char *content)
{
    static const uint8_t device[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
    static const uint8_t nonce[16] = {1, 2, 3};
    ok_mind_obs obs;
    ok_pet pet;
    ok_pet_init(&pet, 1800000000);
    memset(&obs, 0, sizeof obs);
    obs.pet = &pet;
    obs.hour = 9;
    obs.minute = 5;
    obs.player_did = "said hi";
    fake.content = content;
    fake.calls = 0;
    return ok_think_run(&t, p, device, 1800000000000ull, nonce, &obs, fetch, NULL);
}

int main(void)
{
    static const char good[] = "{\\\"activity\\\":\\\"eat\\\",\\\"mood\\\":\\\"curious\\\",\\\"line\\\":\\\"A new book!\\\"}";
    uint8_t pub[64];
    ok_think_provider p = {"tee.example", "0x1cB95Eb66Cc67C62d4DE0CC96726C5A5d82e7C85", "0GM-1.0", {0}, 0};
    ok_json root, v;
    char s[8192];
    size_t n;

    memset(fake.tee, 0x42, 32);
    assert(ok_keys_public(fake.tee, pub) == OK_OK);
    ok_keys_address(pub, p.signer);
    fake.status = 200;

    /* A verified decision: two round trips, the closed-set answer, our token. */
    run(&p, good);
    assert(t.stage == OK_THINK_DECIDED && fake.calls == 2);
    assert(t.decision.activity == OK_MIND_EAT && t.decision.mood == OK_FEEL_CURIOUS);
    assert(!strcmp(t.decision.line, "A new book!") && !strcmp(t.chat_id, "chat-abc_1"));
    assert(t.request_len == fake.req_len && !memcmp(t.request, fake.req_body, t.request_len));
    assert(!strncmp(t.request, "{\"chat_template_kwargs\":", 24)); /* keys sorted, as Go re-encodes it */
    assert(strlen(fake.last_auth) > 400);

    /* The episode is JSON and carries the exact bytes. */
    assert(ok_think_episode(&t, &p, 1800000000001ull, "ab12", episode, sizeof episode, &n) == OK_OK);
    assert(ok_json_parse(episode, n, &root) == OK_OK);
    assert(ok_json_member(&root, "request", &v) == OK_OK && ok_json_string(&v, s, sizeof s, &n) == OK_OK);
    assert(n == t.request_len && !memcmp(s, t.request, n));
    assert(ok_json_member(&root, "chat_id", &v) == OK_OK && ok_json_string(&v, s, sizeof s, &n) == OK_OK);
    assert(!strcmp(s, "chat-abc_1"));
    assert(strstr(episode, "\"prev_sha256\":\"ab12\""));
    assert(strstr(episode, "\"stage\":\"decided\",\"verified\":true") && strstr(episode, "\"activity\":\"eat\""));

    /* The broker renames the model to the upstream id the response names:
     * still ours byte for byte otherwise, so it verifies. */
    fake.upstream = "0GM-1.0-0427";
    run(&p, good);
    assert(t.stage == OK_THINK_DECIDED && !strcmp(t.upstream_model, "0GM-1.0-0427"));
    assert(strstr(t.forwarded, "\"model\":\"0GM-1.0-0427\"") && t.forwarded_len == t.request_len + 5);
    assert(ok_think_episode(&t, &p, 1, "", episode, sizeof episode, &n) == OK_OK);
    assert(strstr(episode, "\"upstream_model\":\"0GM-1.0-0427\""));
    fake.upstream = NULL;

    /* The wrong signer, or a signature over other bytes: refused, the okupa naps. */
    p.signer[0] ^= 1;
    run(&p, good);
    assert(t.stage == OK_THINK_UNVERIFIED && t.decision.activity == OK_MIND_SLEEP && !t.decision.line[0]);
    p.signer[0] ^= 1;
    fake.tamper = 1;
    run(&p, good);
    assert(t.stage == OK_THINK_UNVERIFIED);
    fake.tamper = 0;
    assert(ok_think_episode(&t, &p, 1, "", episode, sizeof episode, &n) == OK_OK);
    assert(strstr(episode, "\"verified\":false") && strstr(episode, "\"safe_stop\":true"));

    /* A verified answer outside the closed sets is still refused. */
    run(&p, "{\\\"activity\\\":\\\"fly\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"hi\\\"}");
    assert(t.stage == OK_THINK_ANSWER && t.decision.activity == OK_MIND_SLEEP);

    /* No chat id header: the completion id is used. */
    fake.no_id = 1;
    run(&p, good);
    assert(t.stage == OK_THINK_DECIDED && !strcmp(t.chat_id, "chatcmpl-9"));
    fake.no_id = 0;

    /* The provider refuses, or the network is down: one round trip, safe stop. */
    fake.status = 401;
    run(&p, good);
    assert(t.stage == OK_THINK_HTTP && t.http_status == 401 && fake.calls == 1);
    fake.status = 200;
    fake.fail_net = 1;
    run(&p, good);
    assert(t.stage == OK_THINK_NET && t.net_error == 62 && t.decision.activity == OK_MIND_SLEEP);
    fake.fail_net = 0;

    /* Hosts and models that would break the URL are refused up front. */
    p.model = "a b";
    assert(run(&p, good) == OK_ARGUMENT && t.stage == OK_THINK_SETUP && fake.calls == 0);

    /* Keep-alive: the connection stays unless the server says it closes. */
    assert(ok_think_keeps_open("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 38));
    assert(!ok_think_keeps_open("HTTP/1.1 200 OK\r\nconnection: Close\r\n\r\n", 39));
    assert(!ok_think_keeps_open("HTTP/1.0 200 OK\r\n\r\n", 19));

    printf("think: ok\n");
    return 0;
}
