#include "mind.h"
#include "okupa_facts.h"
#include "okupa_ui.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_sha256.h"
#include "okupa_sntp.h"
#include "okupa_think.h"
#include <arpa/inet.h>
#include <dswifi9.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <nds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* One decision runs in a cothread. Cothreads are cooperative, so the shared
 * lines and flags below are only touched between yields. */
#define LINES OK_UI_DEV_LINES

static char lines[LINES][33];
static char dir_path[40];
static int started, paused;

/* What the game loop tells the mind (copied, so the thread never reads the
 * game's live state while it changes). */
static ok_pet seen_pet;
static int seen_outside, have_pet;
static uint32_t seen_idle;
static char seen_did[64];

/* The newest verified decision, handed out once. */
static ok_mind mailbox;
static int mailbox_new;

static ok_think t;
static ok_tls tls;
static char episode[OK_THINK_RESPONSE + OK_THINK_REQUEST * 2 + 4096];
static char memory[OK_MIND_RECENT][OK_MIND_LINE + 24];
static int memory_count;
static char chain[65]; /* sha256 of the last episode written */

static struct {
    char host[96], provider[48], model[64], ip[20];
    uint8_t signer[20];
    long tz_minutes;
} cfg;

static void line(int i, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void line(int i, const char *fmt, ...)
{
    va_list ap;
    if (i < 0 || i >= LINES) return;
    va_start(ap, fmt);
    vsnprintf(lines[i], 33, fmt, ap);
    va_end(ap);
}

const char (*ok_dsi_mind_lines(void))[33] { return (const char (*)[33])lines; }

void ok_dsi_mind_observe(const ok_pet *p, int outside, uint32_t seconds_since_player, const char *did)
{
    seen_pet = *p;
    have_pet = 1;
    seen_outside = outside;
    seen_idle = seconds_since_player;
    if (did) snprintf(seen_did, sizeof seen_did, "%s", did);
}

void ok_dsi_mind_toggle(void) { paused = !paused; }

int ok_dsi_mind_take(ok_mind *d)
{
    if (!mailbox_new) return 0;
    *d = mailbox;
    mailbox_new = 0;
    return 1;
}

/* ---- Entropy: the DSi has none, so timer jitter around network events,
 * the clock and the MAC go through Keccak (as for the handshake keys). Used
 * for the TLS seed and the token nonce only, never for a wallet key. */
static ok_keccak pool;
static uint32_t pool_counter;

static void stir(void)
{
    uint32_t c = cpuGetTiming();
    time_t now = time(NULL);
    ok_keccak_update(&pool, &c, sizeof c);
    ok_keccak_update(&pool, &now, sizeof now);
}

static void draw(uint8_t *out, size_t n)
{
    while (n) {
        ok_keccak k = pool;
        uint8_t h[32];
        size_t m = n < 32 ? n : 32;
        pool_counter++;
        stir();
        ok_keccak_update(&k, &pool_counter, sizeof pool_counter);
        ok_keccak_final(&k, h);
        memcpy(out, h, m);
        out += m;
        n -= m;
    }
}

/* ---- Sockets: never trust a blocking call; ask once per frame. */
static int wait_for(int fd, short events, int ms)
{
    int frames;
    for (frames = 0; frames <= ms * 60 / 1000; frames++) {
        struct pollfd p = {fd, events, 0};
        int rc = poll(&p, 1, 0);
        stir();
        if (rc > 0) return (p.revents & events) != 0;
        if (rc < 0) return 0;
        cothread_yield_irq(IRQ_VBLANK);
    }
    return 0;
}

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx, n;
    if (!wait_for(fd, POLLIN, 60000)) return -1; /* inference can take a while */
    n = recv(fd, buf, len, 0);
    return n > 0 ? n : -1;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx, n;
    if (!wait_for(fd, POLLOUT, 10000)) return -1;
    n = send(fd, buf, len, 0);
    return n > 0 ? n : -1;
}

static int resolve(const char *host, struct in_addr *out)
{
    struct addrinfo hints, *ai = NULL;
    if (cfg.ip[0] && inet_aton(cfg.ip, out)) return 1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "443", &hints, &ai) || !ai) return 0;
    *out = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
    freeaddrinfo(ai);
    return 1;
}

/* ---- The clock. The DSi's RTC can be days off (and is set to local time),
 * but TLS certificates and 0G tokens need real UTC: a fresh certificate is
 * "not valid yet" to a console that is four days slow. So UTC comes from an
 * NTP server (unauthenticated, see okupa_sntp.h), never earlier than a floor
 * this build knows has passed; without NTP, the RTC with tz= from mind.txt. */
#define CLOCK_FLOOR 1793980000 /* 2026-11-06 16:26 UTC: before this build existed */

static int64_t clock_offset; /* add to time(NULL) for UTC */
static int clock_synced;
static uint32_t clock_synced_at; /* frames, for a resync every half hour */
static uint32_t frames_alive;

static int64_t utc_now(void)
{
    int64_t now = (int64_t)time(NULL) + (clock_synced ? clock_offset : -(int64_t)cfg.tz_minutes * 60);
    return now < CLOCK_FLOOR ? CLOCK_FLOOR : now;
}

static void describe_offset(char *out, size_t cap, int64_t off)
{
    int64_t a = off < 0 ? -off : off;
    const char *way = off < 0 ? "fast" : "slow";
    if (a < 120)
        snprintf(out, cap, "NTP, RTC right");
    else if (a < 86400)
        snprintf(out, cap, "NTP, RTC %ldh%02ldm %s", (long)(a / 3600), (long)(a / 60 % 60), way);
    else
        snprintf(out, cap, "NTP, RTC %ldd %ldh %s", (long)(a / 86400), (long)(a / 3600 % 24), way);
}

/* One SNTP question to each server in turn until one answers. */
static int clock_sync(void)
{
    static const char *const servers[] = {"162.159.200.1", "216.239.35.0", "pool.ntp.org"};
    struct sockaddr_in addr;
    uint8_t q[OK_SNTP_SIZE], r[64], stamp[8];
    unsigned i;
    int64_t utc;
    for (i = 0; i < sizeof servers / sizeof servers[0]; i++) {
        int fd, n;
        memset(&addr, 0, sizeof addr);
        addr.sin_len = sizeof addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(123);
        if (!inet_aton(servers[i], &addr.sin_addr)) {
            struct addrinfo hints, *ai = NULL;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(servers[i], "123", &hints, &ai) || !ai) continue;
            addr.sin_addr = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
            freeaddrinfo(ai);
        }
        if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        draw(stamp, sizeof stamp);
        ok_sntp_request(stamp, q);
        sendto(fd, q, sizeof q, 0, (struct sockaddr *)&addr, sizeof addr);
        n = wait_for(fd, POLLIN, 3000) ? recv(fd, r, sizeof r, 0) : -1;
        close(fd);
        if (n > 0 && ok_sntp_read(r, (size_t)n, stamp, &utc) == OK_OK && utc >= CLOCK_FLOOR) {
            clock_offset = utc - (int64_t)time(NULL);
            clock_synced = 1;
            clock_synced_at = frames_alive;
            describe_offset(ok_facts_now.og_clock, sizeof ok_facts_now.og_clock, clock_offset);
            line(4, "Clock: %.25s", ok_facts_now.og_clock);
            return 1;
        }
    }
    snprintf(ok_facts_now.og_clock, sizeof ok_facts_now.og_clock, "no NTP: RTC%s", cfg.tz_minutes ? " + tz" : "");
    line(4, "Clock: %.25s", ok_facts_now.og_clock);
    return 0;
}

/* What a failure means, in a few words for the screens. */
static const char *why(int e)
{
    switch (e) {
    case 0: return "";
    case -2: return "DNS: no address";
    case -3: return "TCP: no connection";
    case BR_ERR_X509_EXPIRED: return "cert dates: clock wrong?";
    case BR_ERR_X509_TIME_UNKNOWN: return "cert: no clock";
    case BR_ERR_X509_NOT_TRUSTED: return "cert: unknown root";
    case BR_ERR_X509_BAD_SERVER_NAME: return "cert: wrong host name";
    case BR_ERR_X509_BAD_SIGNATURE: return "cert: bad signature";
    case BR_ERR_BAD_CIPHER_SUITE: return "TLS: no common cipher";
    case BR_ERR_UNSUPPORTED_VERSION: return "TLS: version (1.3 only?)";
    case BR_ERR_IO: return "TLS: connection lost";
    default: return e >= BR_ERR_SEND_FATAL_ALERT ? "TLS: we sent an alert"
                    : e >= BR_ERR_RECV_FATAL_ALERT ? "TLS: server alert" : "TLS error";
    }
}

static void stopped(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void stopped(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ok_facts_now.og_error, sizeof ok_facts_now.og_error, fmt, ap);
    va_end(ap);
    line(12, "Stop: %.26s", ok_facts_now.og_error);
}

/* One HTTPS round trip for the shared core, on the connection the last one
 * left open when the server keeps it; a stale one is reopened once. */
static int fd = -1;

static void hang_up(void)
{
    if (fd >= 0) close(fd);
    fd = -1;
}

static int dial(const char *host)
{
    struct sockaddr_in addr;
    uint8_t seed[32];
    int error = 0;
    socklen_t elen = sizeof error;
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    if (!resolve(host, &addr.sin_addr)) {
        line(2, "DNS: no address for host");
        return -2;
    }
    line(2, "TCP %s:443", inet_ntoa(addr.sin_addr));
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -3;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    if ((connect(fd, (struct sockaddr *)&addr, sizeof addr) && errno != EINPROGRESS) || !wait_for(fd, POLLOUT, 8000) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &elen) || error) {
        hang_up();
        line(2, "TCP: no connection");
        return -3;
    }
    draw(seed, sizeof seed);
    ok_tls_open(&tls, host, utc_now(), seed, sizeof seed, sock_read, sock_write, &fd);
    ok_wipe(seed, sizeof seed);
    line(2, "TLS 1.2 to %.20s", host);
    return 0;
}

static int fetch(void *ctx, const char *host, const char *req, size_t n, const char *body, size_t body_len,
                 char *resp, size_t cap, size_t *got)
{
    int e, tries;
    (void)ctx;
    for (tries = 0; tries < 2; tries++) {
        int reused = fd >= 0;
        if (!reused && (e = dial(host))) return e;
        e = ok_think_exchange(&tls, req, n, body, body_len, resp, cap, got);
        if (!e) {
            if (!ok_think_keeps_open(resp, *got)) hang_up();
            return 0;
        }
        hang_up();
        if (!reused) {
            line(2, "TLS %d: %.22s", e, why(e));
            return e;
        }
        /* the server dropped the idle connection: once more, fresh */
    }
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

static void trim(char *s) { s[strcspn(s, "\r\n \t")] = 0; }

static int load_settings(uint8_t key[32])
{
    char path[64], text[160], *v;
    int have_signer = 0, ok;
    FILE *f;
    memset(&cfg, 0, sizeof cfg);
    snprintf(path, sizeof path, "%smind.txt", dir_path);
    if (!(f = fopen(path, "r"))) return 0;
    while (fgets(text, sizeof text, f)) {
        if (!(v = strchr(text, '='))) continue;
        *v++ = 0;
        trim(v);
        if (!strcmp(text, "host")) snprintf(cfg.host, sizeof cfg.host, "%s", v);
        else if (!strcmp(text, "provider")) snprintf(cfg.provider, sizeof cfg.provider, "%s", v);
        else if (!strcmp(text, "model")) snprintf(cfg.model, sizeof cfg.model, "%s", v);
        else if (!strcmp(text, "ip")) snprintf(cfg.ip, sizeof cfg.ip, "%s", v);
        else if (!strcmp(text, "tz")) cfg.tz_minutes = strtol(v, NULL, 10);
        else if (!strcmp(text, "signer")) have_signer = unhex(v, cfg.signer, 20);
    }
    fclose(f);
    if (!cfg.host[0] || !cfg.provider[0] || !cfg.model[0] || !have_signer) return 0;
    snprintf(path, sizeof path, "%smind.key", dir_path);
    if (!(f = fopen(path, "r"))) return 0;
    ok = fgets(text, sizeof text, f) != NULL;
    fclose(f);
    trim(text);
    ok = ok && unhex(text, key, 32);
    ok_wipe(text, sizeof text);
    return ok;
}

static void hex32(const uint8_t h[32], char out[65])
{
    int i;
    for (i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

/* The episode goes to the SD card as the next link of the chain. */
static void save_episode(const ok_think_provider *p, uint64_t now_ms, unsigned count)
{
    char path[160];
    uint8_t hash[32];
    size_t n;
    FILE *f;
    int ok;
    if (ok_think_episode(&t, p, now_ms, chain, episode, sizeof episode, &n) != OK_OK) {
        line(13, "Episode: too big, not saved");
        return;
    }
    if (t.chat_id[0])
        snprintf(path, sizeof path, "%sepisodes/%.80s.json", dir_path, t.chat_id);
    else
        snprintf(path, sizeof path, "%sepisodes/stop-%llu.json", dir_path, (unsigned long long)now_ms);
    ok = (f = fopen(path, "wb")) != NULL;
    if (ok) {
        ok = fwrite(episode, 1, n, f) == n;
        ok = fclose(f) == 0 && ok;
    }
    if (!ok) {
        line(13, "Episode: SD write FAILED");
        return;
    }
    ok_sha256(episode, n, hash);
    hex32(hash, chain);
    snprintf(path, sizeof path, "%sepisodes/chain.txt", dir_path);
    if ((f = fopen(path, "w"))) {
        fprintf(f, "%s\n", chain);
        fclose(f);
    }
    line(13, "Episode %u on SD, chained", count);
}

static void load_chain(void)
{
    char path[64];
    FILE *f;
    snprintf(path, sizeof path, "%sepisodes", dir_path);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%sepisodes/chain.txt", dir_path);
    chain[0] = 0;
    if ((f = fopen(path, "r"))) {
        if (!fgets(chain, sizeof chain, f) || strlen(chain) != 64) chain[0] = 0;
        fclose(f);
    }
}

/* The TLS version and suite this connection agreed, short enough for the
 * screen ("TLS1.2 ECDSA AES128-GCM"). */
static void tls_name(char *out, size_t cap)
{
    br_ssl_session_parameters sp;
    const char *kx = "?", *cipher = "?";
    br_ssl_engine_get_session_parameters(&tls.client.eng, &sp);
    switch (sp.cipher_suite) {
    case BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256: kx = "ECDSA", cipher = "AES128-GCM"; break;
    case BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384: kx = "ECDSA", cipher = "AES256-GCM"; break;
    case BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256: kx = "ECDSA", cipher = "CHACHA20"; break;
    case BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256: kx = "RSA", cipher = "AES128-GCM"; break;
    case BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384: kx = "RSA", cipher = "AES256-GCM"; break;
    case BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256: kx = "RSA", cipher = "CHACHA20"; break;
    default: break;
    }
    if (!sp.cipher_suite) {
        out[0] = 0;
        return;
    }
    if (*kx == '?')
        snprintf(out, cap, "TLS%u.%u suite %04x", (sp.version >> 8) - 2, (sp.version & 0xff) - 1, sp.cipher_suite);
    else
        snprintf(out, cap, "TLS%u.%u %s %s", (sp.version >> 8) - 2, (sp.version & 0xff) - 1, kx, cipher);
}

/* The 0G page of the technical screens: only what this decision really did.
 * The request hash is of the bytes the TEE hashed: ours, with only the model
 * renamed to the upstream id the signed response names. */
static void record_facts(const ok_think_provider *p, uint32_t ms)
{
    ok_facts *f = &ok_facts_now;
    snprintf(f->og_host, sizeof f->og_host, "%.47s", p->host);
    snprintf(f->og_model, sizeof f->og_model, "%.47s", p->model);
    snprintf(f->og_provider, sizeof f->og_provider, "%.42s", p->provider);
    snprintf(f->og_signer, sizeof f->og_signer, "%.42s", t.recovered);
    f->og_verified = t.stage == OK_THINK_DECIDED || t.stage == OK_THINK_ANSWER;
    tls_name(f->og_tls, sizeof f->og_tls);
    memcpy(f->og_req_sha, t.req_sha, 32);
    memcpy(f->og_resp_sha, t.resp_sha, 32);
    if (t.recovered[0]) memcpy(f->og_sig, t.sig, 65);
    else memset(f->og_sig, 0, 65);
    snprintf(f->og_request_id, sizeof f->og_request_id, "%.47s", t.chat_id);
    f->og_ms = ms;
    if (t.stage == OK_THINK_DECIDED)
        snprintf(f->og_decision, sizeof f->og_decision, "%.39s", ok_mind_activity_name(t.decision.activity));
    else
        snprintf(f->og_decision, sizeof f->og_decision, "sleep (safe stop: %.20s)", ok_think_stage_name(t.stage));
}

static void wait_frames(int frames)
{
    while (frames-- > 0) {
        cothread_yield_irq(IRQ_VBLANK);
        frames_alive++;
    }
}

static int wifi_ready(void)
{
    return Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED && Wifi_GetIP() != 0 && Wifi_GetIP() != 0xffffffffu;
}

static void remember(const ok_mind *d)
{
    int j;
    for (j = OK_MIND_RECENT - 1; j > 0; j--) memcpy(memory[j], memory[j - 1], sizeof memory[0]);
    ok_mind_memory(d, memory[0]);
    if (memory_count < OK_MIND_RECENT) memory_count++;
}

static int thinker(void *arg)
{
    uint8_t key[32], nonce[16], mac[6];
    char did[64];
    ok_think_provider p;
    ok_mind_obs obs;
    ok_pet pet;
    time_t rtc;
    struct tm *tm;
    uint64_t now_ms;
    uint32_t t0, ms;
    unsigned count = 0, verified = 0, fails = 0;
    int j, rest;
    ok_result r;
    (void)arg;

    memset(lines, 0, sizeof lines);
    line(0, "0G mind: starting");
    line(13, "Idle moves are local, not 0G");
    ok_keccak_init(&pool);
    stir();
    if (!dir_path[0] || !load_settings(key)) {
        line(1, "No mind.txt / mind.key on SD");
        stopped("no mind.txt / mind.key");
        return 0;
    }
    load_chain();
    line(1, "%.32s", cfg.model);
    p.host = cfg.host;
    p.provider = cfg.provider;
    p.model = cfg.model;
    memcpy(p.signer, cfg.signer, 20);
    p.generation = 0;

    for (;;) {
        if (paused) {
            line(0, "0G mind: paused (Y resumes)");
            hang_up();
            wait_frames(30);
            continue;
        }
        if (!wifi_ready() || !have_pet) {
            line(0, "0G mind: waiting for Wi-Fi");
            snprintf(ok_facts_now.og_error, sizeof ok_facts_now.og_error, "waiting for Wi-Fi");
            hang_up();
            wait_frames(60);
            continue;
        }
        Wifi_GetData(WIFIGETDATA_MACADDRESS, sizeof mac, mac);
        ok_keccak_update(&pool, mac, sizeof mac);
        if (!clock_synced || frames_alive - clock_synced_at > 60u * 1800u) clock_sync();

        /* What it notices, copied from the game loop. */
        pet = seen_pet;
        memset(&obs, 0, sizeof obs);
        rtc = time(NULL); /* the RTC is local time: it gives the hour the okupa lives in */
        tm = localtime(&rtc);
        obs.pet = &pet;
        obs.hour = tm->tm_hour;
        obs.minute = tm->tm_min;
        obs.outside = seen_outside;
        obs.seconds_since_player = seen_idle;
        if (seen_did[0]) {
            memcpy(did, seen_did, sizeof did);
            seen_did[0] = 0;
            obs.player_did = did;
        }
        for (j = 0; j < memory_count; j++) obs.recent[j] = memory[j];
        obs.recent_count = memory_count;

        now_ms = (uint64_t)utc_now() * 1000u;
        draw(nonce, sizeof nonce);
        t0 = cpuGetTiming();
        r = ok_think_run(&t, &p, key, now_ms, nonce, &obs, fetch, NULL);
        ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
        count++;
        if (r != OK_OK) {
            line(12, "Setup failed: %s", ok_result_name(r));
            break;
        }
        record_facts(&p, ms);
        line(3, "#%u: HTTP %d in %lu ms", count, t.http_status, (unsigned long)ms);
        line(5, "Request id (0G chat id):");
        line(6, "%.32s", t.chat_id[0] ? t.chat_id : "-");
        line(7, "%.32s", strlen(t.chat_id) > 32 ? t.chat_id + 32 : "");
        line(8, "TEE %.6s..%.4s %s", t.signer, t.signer + 38,
             t.stage == OK_THINK_DECIDED || t.stage == OK_THINK_ANSWER ? "VERIFIED" : "not verified");
        if (t.stage == OK_THINK_DECIDED) {
            verified++;
            fails = 0;
            line(9, "Does: %s", ok_mind_activity_name(t.decision.activity));
            line(10, "Mood: %s", ok_mind_mood_name(t.decision.mood));
            line(11, "\"%.30s\"", t.decision.line);
            line(12, "Decided by 0G, checked here");
            line(4, "Clock: %.25s", ok_facts_now.og_clock);
            ok_facts_now.og_error[0] = 0;
            remember(&t.decision);
            mailbox = t.decision;
            mailbox_new = 1;
        } else {
            fails++;
            line(9, "Stopped at: %s", ok_think_stage_name(t.stage));
            line(10, " ");
            line(11, "Safe stop: no decision");
            if (t.stage == OK_THINK_NET || t.stage == OK_THINK_SIG_NET)
                stopped("%s %d: %.16s", ok_think_stage_name(t.stage), t.net_error, why(t.net_error));
            else if (t.stage == OK_THINK_HTTP)
                stopped("HTTP %d: %.20s", t.http_status, t.response ? t.response : "");
            else
                stopped("%s", ok_think_stage_name(t.stage));
            hang_up();
            /* A certificate the clock cannot place: ask NTP again first. */
            if (t.net_error == BR_ERR_X509_EXPIRED || t.net_error == BR_ERR_X509_TIME_UNKNOWN) clock_synced = 0;
        }
        line(0, "0G mind: alive, %u/%u verified", verified, count);
        save_episode(&p, now_ms, count);

        /* Every 5 s while the player is around, every 20 s otherwise; after
         * failures it backs off up to a minute. 0G allows 30 requests a
         * minute and a decision is two. */
        rest = seen_idle < 60 ? 5 : 20;
        if (fails) rest = fails >= 3 ? 60 : 10 << fails; /* each try costs a TLS handshake the game waits for */
        rest = rest * 60 - (int)(ms * 60 / 1000);
        wait_frames(rest > 60 * 3 ? rest : 60 * 3);
    }
    ok_wipe(key, sizeof key);
    return 0;
}

void ok_dsi_mind_start(const char *dir)
{
    if (started) return;
    started = 1;
    snprintf(dir_path, sizeof dir_path, "%s", dir ? dir : "");
    if (cothread_create(thinker, NULL, 64 * 1024, COTHREAD_DETACHED) < 0) line(0, "0G mind: no thread");
}
