#ifndef OKUPA_THINK_H
#define OKUPA_THINK_H

#include "okupa.h"
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
 * Any failure is a safe stop: stage says where, and the okupa naps. Nothing
 * here opens sockets: the platform gives one HTTPS round trip per call. */

#define OK_THINK_REQUEST 4096
#define OK_THINK_RESPONSE 32768
#define OK_THINK_SIGNATURE 4096
#define OK_THINK_CHAT_ID 96

typedef enum {
    OK_THINK_DECIDED = 0, /* verified, parsed decision */
    OK_THINK_SETUP,       /* bad provider settings or device key */
    OK_THINK_NET,         /* the round trip to the provider failed (TCP/TLS) */
    OK_THINK_HTTP,        /* the provider answered, but not 200 */
    OK_THINK_NO_ID,       /* no chat id in the answer */
    OK_THINK_SIG_NET,     /* could not fetch the signature */
    OK_THINK_SIG_FORMAT,  /* the signature reply is not what 0G returns */
    OK_THINK_UNVERIFIED,  /* the text is not our exchange, or the wrong signer */
    OK_THINK_ANSWER       /* verified, but not a closed-set decision */
} ok_think_stage;

typedef struct {
    const char *host;     /* provider host, e.g. compute-network-20.integratenetwork.work */
    const char *provider; /* provider address as the contract lists it (checksummed) */
    const char *model;
    uint8_t signer[20];   /* teeSignerAddress from the serving contract */
    uint32_t generation;  /* sub-account generation (0 unless tokens were revoked) */
} ok_think_provider;

/* One HTTPS round trip: send req to host:443 and read the whole response into
 * resp (up to cap bytes). Returns 0 on success; any other value is the error
 * the platform wants logged (e.g. a BearSSL code). */
typedef int (*ok_think_fetch_fn)(void *ctx, const char *host, const char *req, size_t req_len, char *resp,
                                 size_t cap, size_t *got);

typedef struct {
    ok_think_stage stage;
    int http_status, net_error;
    char chat_id[OK_THINK_CHAT_ID];
    ok_mind decision; /* valid only when stage == OK_THINK_DECIDED */
    char signer[43];  /* checksummed expected signer, for the episode */
    /* The exact bytes, kept for the episode and for anyone re-checking it. */
    char request[OK_THINK_REQUEST];
    size_t request_len;
    char *response; /* inside wire, decoded in place */
    size_t response_len;
    /* What the broker forwards to the TEE: our request with only the model
     * renamed to the upstream id that the signed response names. */
    char upstream_model[64];
    char forwarded[OK_THINK_REQUEST + 64];
    size_t forwarded_len;
    char text[OK_ZEROG_TEXT + 1];
    char signature[133]; /* "0x" + 130 hex */
    /* Scratch: the wire buffer for both round trips. */
    char wire[OK_THINK_RESPONSE];
    char sigwire[OK_THINK_SIGNATURE];
} ok_think;

/* now_ms: unix milliseconds. nonce: 16 fresh random bytes for the token. */
ok_result ok_think_run(ok_think *t, const ok_think_provider *p, const uint8_t device_key[32], uint64_t now_ms,
                       const uint8_t nonce[16], const ok_pet *pet, int hour, int minute, const char *last_action,
                       uint32_t minutes_away, ok_think_fetch_fn fetch, void *fetch_ctx);

const char *ok_think_stage_name(ok_think_stage s);

/* The episode of one decision as JSON (for 0G Storage): the stage, decision,
 * chat id, signer, signed text and signature, and the exact request and
 * response as JSON strings, so anyone can rebuild and re-check the hashes. */
ok_result ok_think_episode(const ok_think *t, const ok_think_provider *p, uint64_t now_ms, char *out, size_t cap,
                           size_t *len);

/* An HTTPS round trip over the core's TLS for platforms that only have a
 * connected socket: rd/wr move raw bytes on it. */
int ok_think_https(ok_tls *tls, const char *host, int64_t now, const uint8_t seed[32], ok_tls_read_fn rd,
                   ok_tls_write_fn wr, void *io, const char *req, size_t req_len, char *resp, size_t cap,
                   size_t *got);

#endif
