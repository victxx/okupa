/* One decision of the okupa's mind on 0G Compute with the core alone: the
 * request from a pet state, a 0G Direct token signed by the given key, HTTPS
 * with our TLS, the TEE signature fetched and checked, the closed-set answer.
 * Only the socket, the clock and the entropy live here.
 *   build/okupa-mind <host> <provider 0x..> <model> <tee signer 0x..> <key file> [out dir]
 * The key must own the 0G ledger that pays the provider. Exit 0 only for a
 * verified decision; anything else is a safe stop (the okupa naps). */
#define _POSIX_C_SOURCE 200809L /* getaddrinfo with -std=c99 */
#include "okupa_think.h"
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    ssize_t n = read(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    ssize_t n = write(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

static int entropy(uint8_t *out, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    int ok = f && fread(out, 1, n, f) == n;
    if (f) fclose(f);
    return ok;
}

/* One HTTPS round trip on a fresh connection. */
static int fetch(void *ctx, const char *host, const char *req, size_t n, char *resp, size_t cap, size_t *got)
{
    static ok_tls tls;
    struct addrinfo hints, *ai;
    struct timeval tv = {60, 0};
    uint8_t seed[32];
    int fd, e;
    (void)ctx;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "443", &hints, &ai)) return -2;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (fd < 0 || connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        freeaddrinfo(ai);
        if (fd >= 0) close(fd);
        return -3;
    }
    freeaddrinfo(ai);
    if (!entropy(seed, sizeof seed)) return -4;
    e = ok_think_https(&tls, host, (int64_t)time(NULL), seed, sock_read, sock_write, &fd, req, n, resp, cap, got);
    close(fd);
    return e;
}

static int unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (strlen(s) != 2 * n) return 0;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

static void save(const char *dir, const char *name, const char *data, size_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if ((f = fopen(path, "wb"))) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    static ok_think t;
    static char episode[65536];
    char keyhex[80], sig[160];
    uint8_t key[32], nonce[16];
    ok_think_provider p;
    ok_pet pet;
    struct timeval now;
    struct tm local;
    time_t sec;
    size_t n;
    FILE *f;
    ok_result r;

    if (argc < 6) {
        fprintf(stderr, "usage: okupa-mind <host> <provider> <model> <tee signer> <key file> [out dir]\n");
        return 2;
    }
    p.host = argv[1];
    p.provider = argv[2];
    p.model = argv[3];
    p.generation = 0;
    if (!unhex(argv[4], p.signer, 20)) {
        fprintf(stderr, "okupa-mind: bad signer address\n");
        return 2;
    }
    if (!(f = fopen(argv[5], "r")) || !fgets(keyhex, sizeof keyhex, f)) {
        fprintf(stderr, "okupa-mind: cannot read the key\n");
        return 2;
    }
    fclose(f);
    keyhex[strcspn(keyhex, "\r\n")] = 0;
    if (!unhex(keyhex, key, 32) || !entropy(nonce, sizeof nonce)) return 2;
    ok_wipe(keyhex, sizeof keyhex);

    gettimeofday(&now, NULL);
    sec = now.tv_sec;
    localtime_r(&sec, &local);
    ok_pet_init(&pet, (int64_t)sec - 4 * 3600); /* a few hours since the last visit */
    ok_pet_update(&pet, (int64_t)sec);
    r = ok_think_run(&t, &p, key, (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_usec / 1000u, nonce, &pet,
                     local.tm_hour, local.tm_min, "player opened the lid", 240, fetch, NULL);
    ok_wipe(key, sizeof key);
    if (r != OK_OK) {
        fprintf(stderr, "okupa-mind: setup failed (%s); the okupa naps\n", ok_result_name(r));
        return 1;
    }
    printf("stage %s, http %d, net error %d\n", ok_think_stage_name(t.stage), t.http_status, t.net_error);
    printf("chat id (request id) %s\n", t.chat_id[0] ? t.chat_id : "-");
    printf("tee signer %s\n", t.signer);
    if (t.stage == OK_THINK_DECIDED)
        printf("VERIFIED decision: activity %s, mood %s, line \"%s\"\n", ok_mind_activity_name(t.decision.activity),
               ok_mind_mood_name(t.decision.mood), t.decision.line);
    else
        printf("safe stop: the okupa naps (no verified answer)\n");
    if (t.response_len) printf("response: %.*s\n", (int)(t.response_len > 400 ? 400 : t.response_len), t.response);

    if (argc > 6) {
        save(argv[6], "request.json", t.request, t.request_len);
        if (t.response) save(argv[6], "response.json", t.response, t.response_len);
        n = (size_t)snprintf(sig, sizeof sig, "%s", t.signature);
        if (n) save(argv[6], "signature.hex", sig, n);
        if (t.text[0]) save(argv[6], "signed_text.txt", t.text, strlen(t.text));
        if (ok_think_episode(&t, &p, (uint64_t)time(NULL) * 1000u, episode, sizeof episode, &n) == OK_OK)
            save(argv[6], "episode.json", episode, n);
        printf("saved to %s\n", argv[6]);
    }
    return t.stage == OK_THINK_DECIDED ? 0 : 1;
}
