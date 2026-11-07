#ifndef OKUPA_THINK_H
#define OKUPA_THINK_H

#include "okupa.h"
#include "okupa_make.h"
#include "okupa_mind.h"
#include "okupa_pet.h"
#include "okupa_tls.h"
#include "okupa_zerog.h"

/* One decision of the okupa's mind on 0G Compute, the same on the host and
 * on the DSi:
 *   1. the request is built from the pet's state (okupa_mind.h);
 *   2. it goes to the provider through 0G Direct with a bearer token signed by
 *      the console's own key;
 *   3. the chat id comes back in ZG-Res-Key; the provider's TEE signature of
 *      sha256(request):sha256(response) is fetched for it;
 *   4. the signature is checked against the TEE signer we were given, over
 *      the bytes we sent and received ourselves;
 *   5. only then is the answer read, and only from the closed sets.
 * Any failure is a safe stop: stage says where, and nothing is decided. Nothing
 * here opens sockets: the platform gives one HTTPS round trip per call. */

#define OK_THINK_REQUEST 12288  /* the decision's own request (the prompt, the observation, its last lines) */
#define OK_THINK_RESPONSE 98304 /* a dream picture (a 128x128 PNG) comes back as base64 */
#define OK_THINK_SIGNATURE 4096
#define OK_THINK_CHAT_ID 96

typedef enum {
    OK_THINK_DECIDED = 0, /* verified (and, for a decision, read from the closed sets) */
    OK_THINK_SETUP,       /* bad provider settings or device key */
    OK_THINK_NET,         /* the round trip to the provider failed (TCP/TLS) */
    OK_THINK_HTTP,        /* the provider answered, but not 200 */
    OK_THINK_NO_ID,       /* no chat id in the answer */
    OK_THINK_SIG_NET,     /* could not fetch the signature */
    OK_THINK_SIG_FORMAT,  /* the signature reply is not what 0G returns */
    OK_THINK_UNVERIFIED,  /* the text is not our exchange, or the wrong signer */
    OK_THINK_ANSWER       /* verified, but not an answer we can use */
} ok_think_stage;

typedef struct {
    const char *host;     /* provider host, e.g. compute-network-20.integratenetwork.work */
    const char *provider; /* provider address as the contract lists it (checksummed) */
    const char *model;
    uint8_t signer[20];   /* teeSignerAddress from the serving contract */
    uint32_t generation;  /* sub-account generation (0 unless tokens were revoked) */
} ok_think_provider;

/* One HTTPS round trip: send head then body (body may be NULL) to host:443
 * and read the whole response into resp (up to cap bytes). Returns 0 on
 * success; any other value is the error the platform wants logged (e.g. a
 * BearSSL code). */
typedef int (*ok_think_fetch_fn)(void *ctx, const char *host, const char *head, size_t head_len, const char *body,
                                 size_t body_len, char *resp, size_t cap, size_t *got);

typedef struct {
    ok_think_stage stage;
    int http_status, net_error;
    char chat_id[OK_THINK_CHAT_ID];
    ok_mind decision; /* a decision: valid only when stage == OK_THINK_DECIDED */
    char heard[OK_MIND_TEXT + 1]; /* ok_think_hear: the transcript */
    char saw[OK_MIND_TEXT + 1];   /* ok_think_see: what the camera shows */
    ok_mind_plan plan;            /* ok_think_plan: its goal for the next while */
    char diary[OK_MIND_DIARY + 1]; /* ok_think_diary: today's entry */
    char dream[OK_MIND_DREAM + 1]; /* ok_think_diary: tonight's dream, as a picture to make */
    ok_object object;              /* ok_think_make: the object it built */
    uint8_t drawing[512];          /* ok_think_draw: its own drawing */
    char drawing_name[21];
    /* ok_think_dream: the picture, decoded from the answer; the TEE signs
     * sha256(request):sha256(these bytes). */
    uint8_t *image;
    size_t image_cap, image_len;
    char signer[43];  /* checksummed expected signer, for the episode */
    const char *kind; /* "decision", "hearing", "sight" */
    /* The exact bytes, kept for the episode and for anyone re-checking it:
     * body points to what was sent (request below, or the caller's audio or
     * image request), JSON or not. */
    char request[OK_THINK_REQUEST];
    size_t request_len;
    const char *body;
    size_t body_len;
    int body_json;
    char *response; /* inside wire, decoded in place */
    size_t response_len;
    /* What the TEE hashed: our body with only the model renamed to the
     * upstream id that the signed response names (JSON bodies). */
    char upstream_model[64];
    uint8_t req_sha[32], resp_sha[32];
    char text[OK_ZEROG_TEXT + 1];
    char signature[133]; /* "0x" + 130 hex */
    uint8_t sig[65];     /* the same, as bytes */
    char recovered[43];  /* who really signed the text, recovered here ("" if unreadable) */
    /* Scratch: the wire buffers of both round trips. */
    char wire[OK_THINK_RESPONSE];
    char sigwire[OK_THINK_SIGNATURE];
} ok_think;

/* One verified 0G Direct call: POST body to path, fetch the TEE signature
 * for its chat id, check it over our own bytes. stage == OK_THINK_DECIDED
 * means verified; t->response is then the provider's answer. now_ms: unix
 * milliseconds (real UTC). nonce: 16 fresh random bytes for the token. */
ok_result ok_think_call(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const char *path, const char *content_type, const char *body,
                        size_t body_len, ok_think_fetch_fn fetch, void *fetch_ctx);

/* What the okupa does next: the decision request from what it notices. */
ok_result ok_think_run(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                       const uint8_t nonce[16], const ok_mind_obs *obs, ok_think_fetch_fn fetch, void *fetch_ctx);

/* What the player said: a 16-bit mono WAV to Whisper. buf (cap bytes, at
 * least wav_len + 512) holds the multipart body; t->heard the transcript. */
ok_result ok_think_hear(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const uint8_t *wav, size_t wav_len, char *buf, size_t cap,
                        ok_think_fetch_fn fetch, void *fetch_ctx);

/* What the okupa sees: a PNG to a vision model. buf (cap bytes, at least
 * 4/3 png_len + 2048) holds the request; t->saw one sentence. */
ok_result ok_think_see(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                       const uint8_t nonce[16], const uint8_t *png, size_t png_len, char *buf, size_t cap,
                       ok_think_fetch_fn fetch, void *fetch_ctx);

/* The slow mind (a goal and a plan) and the diary at the end of the day,
 * from the same observation (okupa_mind.h). */
ok_result ok_think_plan(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const ok_mind_obs *obs, ok_think_fetch_fn fetch, void *fetch_ctx);
ok_result ok_think_diary(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                         const uint8_t nonce[16], const ok_mind_obs *obs, ok_think_fetch_fn fetch, void *fetch_ctx);

/* Things it makes: an object out of boxes, or its own drawing. */
ok_result ok_think_make(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const char *what, ok_think_fetch_fn fetch, void *fetch_ctx);
ok_result ok_think_draw(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                        const uint8_t nonce[16], const char *subject, ok_think_fetch_fn fetch, void *fetch_ctx);

/* A dream: the image model draws prompt as a 128x128 PNG into image (cap
 * bytes). buf holds the request. */
ok_result ok_think_dream(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                         const uint8_t nonce[16], const char *prompt, uint8_t *image, size_t cap, char *buf,
                         size_t buf_cap, ok_think_fetch_fn fetch, void *fetch_ctx);

/* The okupa's reaction to a drawing the player sent it: t->saw is one line
 * from the vision model, decoration only. */
ok_result ok_think_comment(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                           const uint8_t nonce[16], const uint8_t *png, size_t png_len, char *buf, size_t cap,
                           ok_think_fetch_fn fetch, void *fetch_ctx);

/* Pictionary: the player's drawing as a PNG; t->saw is the vision model's
 * guess, one to three lower-case words (anything else is OK_THINK_ANSWER). */
ok_result ok_think_guess(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                         const uint8_t nonce[16], const uint8_t *png, size_t png_len, char *buf, size_t cap,
                         ok_think_fetch_fn fetch, void *fetch_ctx);

const char *ok_think_stage_name(ok_think_stage s);

/* The episode of one call as JSON (for 0G Storage): kind, stage, decision or
 * transcript or sight, chat id, signer, signed text and signature, and the
 * exact request (a JSON string, or base64 for audio) and response, so anyone
 * can rebuild and re-check the hashes. prev: sha256 hex of the episode before
 * it ("" for the first), so the okupa's life is a chain anyone can walk. */
ok_result ok_think_episode(const ok_think *t, const ok_think_provider *p, uint64_t now_ms, const char *prev,
                           char *out, size_t cap, size_t *len);

/* An HTTPS round trip over the core's TLS for platforms that only have a
 * connected socket: rd/wr move raw bytes on it. ok_think_https opens the TLS
 * session first; ok_think_exchange reuses one already open (keep-alive). */
int ok_think_https(ok_tls *tls, const char *host, int64_t now, const uint8_t seed[32], ok_tls_read_fn rd,
                   ok_tls_write_fn wr, void *io, const char *head, size_t head_len, const char *body,
                   size_t body_len, char *resp, size_t cap, size_t *got);
int ok_think_exchange(ok_tls *tls, const char *head, size_t head_len, const char *body, size_t body_len, char *resp,
                      size_t cap, size_t *got);
/* 1 once buf holds a whole HTTP response (Content-Length or chunked). */
int ok_think_complete(char *buf, size_t n);
/* 0 if the server said it closes the connection after this response. */
int ok_think_keeps_open(const char *resp, size_t n);

#endif
