#include "okupa_json.h"
#include "okupa_keys.h"
#include "okupa_sha256.h"
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
    char req_body[200000];
    size_t req_len;
    char resp_body[4096];
    size_t resp_len;
    uint8_t tee[32];
    int calls;
    char last_auth[1024];
    uint8_t image[64]; /* the picture an image model made, when it did */
    size_t image_len;
} fake;

static int fetch(void *ctx, const char *host, const char *head, size_t head_len, const char *payload,
                 size_t payload_len, char *resp, size_t cap, size_t *got)
{
    static char req[200000];
    const char *body;
    size_t n = head_len + payload_len;
    int k;
    (void)ctx;
    assert(n < sizeof req);
    memcpy(req, head, head_len);
    if (payload_len) memcpy(req + head_len, payload, payload_len);
    req[n] = 0;
    body = strstr(req, "\r\n\r\n");
    assert(!strcmp(host, "tee.example"));
    fake.calls++;
    if (fake.fail_net) return 62;
    if (!strncmp(req, "POST /v1/proxy/images/generations ", 34)) {
        /* An image model: the picture is base64 in the JSON (with an escaped
         * slash), and it is the picture's bytes the TEE hashes. */
        assert(body);
        fake.req_len = n - (size_t)(body + 4 - req);
        memcpy(fake.req_body, body + 4, fake.req_len);
        fake.resp_len = (size_t)snprintf(fake.resp_body, sizeof fake.resp_body,
                                         "{\"data\":[{\"b64_json\":\"iVBORw0KGgo\\/AA==\"}],\"usage\":{}}");
        memcpy(fake.image, "\x89PNG\r\n\x1a\n\x3f\x00", 10);
        fake.image_len = 10;
        k = snprintf(resp, cap, "HTTP/1.1 200 OK\r\nZG-Res-Key: chat-abc_1\r\nContent-Length: %lu\r\n\r\n%s",
                     (unsigned long)fake.resp_len, fake.resp_body);
    } else if (!strncmp(req, "POST /v1/proxy/audio/transcriptions ", 36)) {
        /* Whisper: the multipart body is hashed as is, and the answer has no model. */
        assert(body && strstr(req, "Content-Type: multipart/form-data; boundary="));
        fake.req_len = n - (size_t)(body + 4 - req);
        memcpy(fake.req_body, body + 4, fake.req_len);
        fake.resp_len = (size_t)snprintf(fake.resp_body, sizeof fake.resp_body,
                                         "{\"text\":\" Hola, me llamo V\xc3\xad" "ctor.\",\"usage\":{\"seconds\":2}}");
        k = snprintf(resp, cap, "HTTP/1.1 200 OK\r\nZG-Res-Key: chat-abc_1\r\nContent-Length: %lu\r\n\r\n%s",
                     (unsigned long)fake.resp_len, fake.resp_body);
    } else if (!strncmp(req, "POST /v1/proxy/chat/completions ", 32)) {
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
        assert(!payload_len);
        if (fake.tamper) fake.resp_body[0] = ' ';
        if (fake.image_len)
            ok_zerog_text((const uint8_t *)fake.req_body, fake.req_len, fake.image, fake.image_len, text);
        else
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
    {
        char want[43];
        ok_keys_checksum(p.signer, want);
        assert(!strcmp(t.recovered, want)); /* the signer recovered here, not just compared */
    }

    assert(!strcmp(t.chat_id, "chat-abc_1"));

    /* The broker renames the model to the upstream id the response names:
     * still ours byte for byte otherwise, so it verifies. */
    fake.upstream = "0GM-1.0-0427";
    run(&p, good);
    assert(t.stage == OK_THINK_DECIDED && !strcmp(t.upstream_model, "0GM-1.0-0427"));
    {
        uint8_t h[32];
        ok_sha256(fake.req_body, fake.req_len, h); /* the renamed request, as the TEE hashed it */
        assert(!memcmp(h, t.req_sha, 32) && fake.req_len == t.request_len + 5);
    }
    fake.upstream = NULL;

    /* The wrong signer, or a signature over other bytes: refused, the okupa naps. */
    p.signer[0] ^= 1;
    run(&p, good);
    assert(t.recovered[0] && strcmp(t.recovered, t.signer)); /* the real signer, which is not the one we trust */
    assert(t.stage == OK_THINK_UNVERIFIED && t.decision.activity == OK_MIND_SLEEP && !t.decision.line[0]);
    p.signer[0] ^= 1;
    fake.tamper = 1;
    run(&p, good);
    assert(t.stage == OK_THINK_UNVERIFIED);
    fake.tamper = 0;

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

    /* Hearing: Whisper's transcript, verified over our exact multipart bytes;
     * accents dropped for the small screen. */
    p.model = "0GM-1.0";
    {
        static char buf[4096];
        static const uint8_t wav[44] = {'R', 'I', 'F', 'F'};
        static const uint8_t device[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                           7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
        static const uint8_t nonce[16] = {4};
        assert(ok_think_hear(&t, &p, device, 1800000000000ull, nonce, wav, sizeof wav, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strcmp(t.heard, "Hola, me llamo Victor.") && !t.body_json);
        assert(ok_think_hear(&t, &p, device, 1, nonce, wav, sizeof wav, buf, 100, fetch, NULL) == OK_RANGE);

        /* Sight: one sentence from a vision answer, the image inside a
         * canonical request. */
        fake.content = "A person with glasses is smiling at the camera.";
        assert(ok_think_see(&t, &p, device, 1800000000000ull, nonce, wav, sizeof wav, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strcmp(t.saw, "A person with glasses is smiling at the camera."));
        assert(!t.quip[0]); /* no bar: what it saw, nothing to say */
        assert(strstr(fake.req_body, "\"url\":\"data:image/png;base64,UklGR"));
        assert(!strncmp(fake.req_body, "{\"chat_template_kwargs\":", 24));

        /* "what it sees | what Okupa says": split, quotes and spaces trimmed. */
        fake.content = "A man with dark hair, dim light | \\\"You look rad af. Kinda.\\\" "; /* JSON-escaped quotes */
        assert(ok_think_see(&t, &p, device, 1800000000000ull, nonce, wav, sizeof wav, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strcmp(t.saw, "A man with dark hair, dim light"));
        assert(!strcmp(t.quip, "You look rad af. Kinda."));
        fake.content = " | nothing seen";
        assert(ok_think_see(&t, &p, device, 1800000000000ull, nonce, wav, sizeof wav, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_ANSWER); /* a line with nothing seen is not an answer */
    }

    /* Pictionary: the guess is one to three words, lower case. */
    {
        static char buf[4096];
        static const uint8_t png[8] = {0x89, 'P', 'N', 'G'};
        static const uint8_t device[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                           7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
        static const uint8_t nonce[16] = {5};
        fake.content = "A House.";
        assert(ok_think_guess(&t, &p, device, 1800000000000ull, nonce, png, sizeof png, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strcmp(t.saw, "a house"));
        assert(strstr(fake.req_body, "Pictionary") && strstr(fake.req_body, "\"max_tokens\":16"));
        fake.content = "A cosy little house! I want to live there!";
        assert(ok_think_comment(&t, &p, device, 1800000000000ull, nonce, png, sizeof png, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strcmp(t.saw, "A cosy little house! I want to live there!"));
        assert(strstr(fake.req_body, "is showing it to you"));
        fake.content = "I think this might be a small cottage";
        assert(ok_think_guess(&t, &p, device, 1800000000000ull, nonce, png, sizeof png, buf, sizeof buf, fetch, NULL) ==
               OK_OK);
        assert(t.stage == OK_THINK_ANSWER); /* not a guess: a sentence */
    }

    /* The slow mind and the diary: verified like any call, read from JSON. */
    p.model = "0GM-1.0";
    fake.content = "{\\\"goal\\\":\\\"Learn the player's name\\\",\\\"plan\\\":\\\"Wave, then ask\\\",\\\"feeling\\\":\\\"Hopeful\\\"}";
    {
        static const uint8_t device[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                           7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
        static const uint8_t nonce[16] = {6};
        ok_mind_obs obs;
        ok_pet pet;
        ok_pet_init(&pet, 1800000000);
        memset(&obs, 0, sizeof obs);
        obs.pet = &pet;
        obs.goal = "Explore the garden";
        assert(ok_think_plan(&t, &p, device, 1800000000000ull, nonce, &obs, fetch, NULL) == OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strcmp(t.plan.goal, "Learn the player's name"));
        assert(!strcmp(t.plan.plan, "Wave, then ask") && strstr(fake.req_body, "slow mind"));
        fake.content = "{\\\"entry\\\":\\\"Today a giant eye said hello. I waved. I think it likes me.\\\"}";
        obs.today[0] = "said: Hi!";
        obs.today[1] = "heard: hello okupa";
        obs.today_count = 2;
        obs.date = "2026-11-07";
        assert(ok_think_diary(&t, &p, device, 1800000000000ull, nonce, &obs, fetch, NULL) == OK_OK);
        assert(t.stage == OK_THINK_DECIDED && !strncmp(t.diary, "Today a giant eye", 17));
        assert(strstr(fake.req_body, "\\\"today\\\":[\\\"said: Hi!\\\",\\\"heard: hello okupa\\\"]"));
        assert(strstr(fake.req_body, "\\\"date\\\":\\\"2026-11-07\\\""));
        fake.content = "no json here";
        assert(ok_think_plan(&t, &p, device, 1800000000000ull, nonce, &obs, fetch, NULL) == OK_OK && t.stage == OK_THINK_ANSWER);
    }

    /* A dream: the image model's picture, verified over its own bytes. */
    {
        static char buf[2048];
        static uint8_t image[64];
        static const uint8_t device[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                           7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
        static const uint8_t nonce[16] = {8};
        assert(ok_think_dream(&t, &p, device, 1800000000000ull, nonce, "a monkey flying over Mumbai", image, sizeof image,
                              buf, sizeof buf, fetch, NULL) == OK_OK);
        assert(t.stage == OK_THINK_DECIDED && t.image_len == 10 && !memcmp(image, "\x89PNG\r\n\x1a\n\x3f\x00", 10));
        assert(!strncmp(fake.req_body, "{\"model\":\"0GM-1.0\",\"n\":1,\"output_format\":\"png\"", 46));
        assert(ok_think_dream(&t, &p, device, 1, nonce, "a \"quoted\" dream", image, sizeof image, buf, sizeof buf, fetch,
                              NULL) == OK_ARGUMENT);
        fake.image_len = 0;
    }

    /* Keep-alive: the connection stays unless the server says it closes. */
    assert(ok_think_keeps_open("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 38));
    assert(!ok_think_keeps_open("HTTP/1.1 200 OK\r\nconnection: Close\r\n\r\n", 39));
    assert(!ok_think_keeps_open("HTTP/1.0 200 OK\r\n\r\n", 19));

    printf("think: ok\n");
    return 0;
}
