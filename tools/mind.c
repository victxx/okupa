/* One decision of the okupa's mind on 0G Compute with the core alone: the
 * request from a pet state, a 0G Direct token signed by the given key, HTTPS
 * with our TLS, the TEE signature fetched and checked, the closed-set answer.
 * Only the socket, the clock and the entropy live here.
 *   build/okupa-mind <host> <provider 0x..> <model> <tee signer 0x..> <key file> [out dir] [decisions]
 *   build/okupa-mind <host> <provider> <model> <signer> <key file> <out dir> hear=<16-bit mono wav>
 *   build/okupa-mind <host> <provider> <model> <signer> <key file> <out dir> see=<png>
 * With several decisions it lives for a while: each one remembers the last
 * few, one TLS connection is kept open between them, and the episodes are
 * chained by hash (episode-N.json). The key must own the 0G ledger that pays
 * the provider. Exit 0 only if every decision was verified; anything else is
 * a safe stop (the okupa sleeps). */
#define _POSIX_C_SOURCE 200809L /* getaddrinfo with -std=c99 */
#include "okupa_sha256.h"
#include "okupa_think.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
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

/* One HTTPS round trip, on the connection left open by the last one when
 * the server keeps it (a stale one is reopened once). */
static int fd = -1;
static char open_host[128];

static void hang_up(void)
{
    if (fd >= 0) close(fd);
    fd = -1;
}

static int dial(const char *host)
{
    struct addrinfo hints, *ai;
    struct timeval tv = {60, 0};
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "443", &hints, &ai)) return -2;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        freeaddrinfo(ai);
        hang_up();
        return -3;
    }
    freeaddrinfo(ai);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    snprintf(open_host, sizeof open_host, "%s", host);
    return 0;
}

static int fetch(void *ctx, const char *host, const char *req, size_t n, const char *body, size_t body_len,
                 char *resp, size_t cap, size_t *got)
{
    static ok_tls tls;
    uint8_t seed[32];
    int e, reused = fd >= 0 && !strcmp(open_host, host);
    (void)ctx;
    if (reused) {
        e = ok_think_exchange(&tls, req, n, body, body_len, resp, cap, got);
        if (!e && ok_think_keeps_open(resp, *got)) return 0;
        if (!e) {
            hang_up();
            return 0;
        }
        hang_up(); /* the server dropped the idle connection: once more, fresh */
    }
    hang_up();
    if ((e = dial(host))) return e;
    if (!entropy(seed, sizeof seed)) return -4;
    e = ok_think_https(&tls, host, (int64_t)time(NULL), seed, sock_read, sock_write, &fd, req, n, body, body_len, resp,
                       cap, got);
    if (e || !ok_think_keeps_open(resp, *got)) hang_up();
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

static double seconds(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double)now.tv_sec + (double)now.tv_usec / 1e6;
}

int main(int argc, char **argv)
{
    static ok_think t;
    static char episode[1200000], memory[OK_MIND_RECENT][OK_MIND_LINE + 24];
    char keyhex[80], prev[65] = "", name[48];
    uint8_t key[32], nonce[16], hash[32];
    ok_think_provider p;
    ok_mind_obs obs;
    ok_pet pet;
    struct tm local;
    time_t sec;
    size_t n;
    FILE *f;
    ok_result r;
    int count = argc > 7 ? atoi(argv[7]) : 1, i, j, verified = 0;
    const char *sense = argc > 7 && (!strncmp(argv[7], "hear=", 5) || !strncmp(argv[7], "see=", 4) ||
                                     !strncmp(argv[7], "guess=", 6))
                            ? argv[7]
                            : NULL;
    double t0;

    if (sense) count = 1;
    if (argc < 6 || count < 1 || count > 500) {
        fprintf(stderr, "usage: okupa-mind <host> <provider> <model> <tee signer> <key file> [out dir] [decisions]\n");
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
    if (!unhex(keyhex, key, 32)) return 2;
    ok_wipe(keyhex, sizeof keyhex);

    if (sense) { /* one verified sense: what it hears or sees */
        static uint8_t media[400000];
        static char buf[600000];
        int hear = sense[0] == 'h', guess = sense[0] == 'g';
        size_t m;
        if (!(f = fopen(strchr(sense, '=') + 1, "rb"))) return 2;
        m = fread(media, 1, sizeof media, f);
        fclose(f);
        if (!entropy(nonce, sizeof nonce)) return 2;
        t0 = seconds();
        r = hear    ? ok_think_hear(&t, &p, key, (uint64_t)(seconds() * 1000.0), nonce, media, m, buf, sizeof buf, fetch, NULL)
            : guess ? ok_think_guess(&t, &p, key, (uint64_t)(seconds() * 1000.0), nonce, media, m, buf, sizeof buf, fetch, NULL)
                    : ok_think_see(&t, &p, key, (uint64_t)(seconds() * 1000.0), nonce, media, m, buf, sizeof buf, fetch, NULL);
        ok_wipe(key, sizeof key);
        hang_up();
        if (r != OK_OK) {
            fprintf(stderr, "okupa-mind: setup failed (%s)\n", ok_result_name(r));
            return 1;
        }
        printf("%s %.2fs %s %s signer %s\n", t.kind, seconds() - t0, ok_think_stage_name(t.stage),
               t.chat_id[0] ? t.chat_id : "-", t.recovered[0] ? t.recovered : "-");
        if (t.stage == OK_THINK_DECIDED)
            printf("VERIFIED %s: \"%s\"\n", hear ? "heard" : guess ? "guessed" : "saw", hear ? t.heard : t.saw);
        else if (t.response_len) printf("    %.*s\n", (int)(t.response_len > 300 ? 300 : t.response_len), t.response);
        if (ok_think_episode(&t, &p, (uint64_t)(seconds() * 1000.0), prev, episode, sizeof episode, &n) == OK_OK)
            save(argv[6], "episode.json", episode, n);
        return t.stage == OK_THINK_DECIDED ? 0 : 1;
    }

    sec = time(NULL);
    ok_pet_init(&pet, (int64_t)sec - 4 * 3600); /* a few hours since the last visit */
    memset(&obs, 0, sizeof obs);
    obs.pet = &pet;
    obs.player_did = "opened the lid";
    obs.city = getenv("OKUPA_CITY"); /* where the console is, if the player said */
    for (i = 0; i < count; i++) {
        sec = time(NULL);
        localtime_r(&sec, &local);
        ok_pet_update(&pet, (int64_t)sec);
        obs.hour = local.tm_hour;
        obs.minute = local.tm_min;
        obs.seconds_since_player = (uint32_t)(i * 5);
        if (i) obs.player_did = NULL;
        if (!entropy(nonce, sizeof nonce)) return 2;
        t0 = seconds();
        r = ok_think_run(&t, &p, key, (uint64_t)(seconds() * 1000.0), nonce, &obs, fetch, NULL);
        if (r != OK_OK) {
            fprintf(stderr, "okupa-mind: setup failed (%s); the okupa sleeps\n", ok_result_name(r));
            ok_wipe(key, sizeof key);
            return 1;
        }
        printf("%3d %5.2fs %-10s %-36s ", i + 1, seconds() - t0, ok_think_stage_name(t.stage),
               t.chat_id[0] ? t.chat_id : "-");
        if (t.stage == OK_THINK_DECIDED) {
            verified++;
            printf("VERIFIED %s / %s \"%s\"%s%s%s%s%s%s\n", ok_mind_activity_name(t.decision.activity),
                   ok_mind_mood_name(t.decision.mood), t.decision.line, t.decision.thought[0] ? " thinks: " : "",
                   t.decision.thought, t.decision.ask[0] ? " asks: " : "", t.decision.ask,
                   t.decision.remember[0] ? " keeps: " : "", t.decision.remember);
            /* It remembers what it just did, newest first. */
            for (j = OK_MIND_RECENT - 1; j > 0; j--) memcpy(memory[j], memory[j - 1], sizeof memory[0]);
            ok_mind_memory(&t.decision, memory[0]);
            if (obs.recent_count < OK_MIND_RECENT) obs.recent_count++;
            for (j = 0; j < obs.recent_count; j++) obs.recent[j] = memory[j];
        } else {
            printf("safe stop, the okupa sleeps (http %d, net %d)\n", t.http_status, t.net_error);
            if (t.response_len) printf("    %.*s\n", (int)(t.response_len > 300 ? 300 : t.response_len), t.response);
        }
        if (argc > 6 && ok_think_episode(&t, &p, (uint64_t)(seconds() * 1000.0), prev, episode, sizeof episode, &n) ==
                            OK_OK) {
            if (count == 1) {
                save(argv[6], "request.json", t.request, t.request_len);
                if (t.response) save(argv[6], "response.json", t.response, t.response_len);
                if (t.signature[0]) save(argv[6], "signature.hex", t.signature, strlen(t.signature));
                if (t.text[0]) save(argv[6], "signed_text.txt", t.text, strlen(t.text));
                save(argv[6], "episode.json", episode, n);
            } else {
                snprintf(name, sizeof name, "episode-%03d.json", i + 1);
                save(argv[6], name, episode, n);
            }
            ok_sha256(episode, n, hash);
            for (j = 0; j < 32; j++) snprintf(prev + 2 * j, 3, "%02x", hash[j]);
        }
    }
    ok_wipe(key, sizeof key);
    hang_up();
    printf("tee signer %s; %d of %d verified\n", t.signer, verified, count);
    return verified == count ? 0 : 1;
}
