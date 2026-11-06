#include "mind.h"
#include "net.h"
#include "okupa_json.h"
#include "okupa_memory.h"
#include "okupa_png.h"
#include "senses.h"
#include "okupa_store.h"
#include "okupa_facts.h"
#include "okupa_ui.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_sha256.h"
#include "okupa_sntp.h"
#include "bearssl_hash.h"
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
/* One TLS state per provider host, so each keeps its session for the next
 * connection: a resumed handshake skips the certificate chain and the key
 * exchange, the part that would hold the ARM9 for hundreds of ms. */
#define TLS_HOSTS 4 /* chat, ears, dreams, the 0G chain */
static ok_tls tls_by_host[TLS_HOSTS];
static ok_tls *tlsp = &tls_by_host[0];
#define tls (*tlsp)
static char episode[224 * 1024]; /* a hearing keeps its audio in base64 (~110 KB); a batch's upload body */
static char recent[OK_MIND_RECENT][OK_MIND_LINE + 24];
static int recent_count;

/* What it keeps for good (memory.txt), the question it is waiting on, and
 * what its senses brought since the last decision. */
static ok_memory facts;
/* The slow mind's goal and plan, the day so far, and yesterday's entry. */
static ok_mind_plan aim;
static uint32_t aimed_at;
static int aimed;
static char today[24][81];
static int today_count;
static char today_date[11];
static char yesterday[OK_MIND_DIARY + 1];
static int diary_written; /* today's entry is in diary.txt */

/* What it makes, dreams and writes, handed to the game loop once each. */
static char want_make[41], want_draw[41];
static ok_object made;
static uint8_t drew[512];
static char drew_name[21];
static char letter[141];
static volatile int made_new, drew_new, letter_new, dream_new, letters_ok;
static uint16_t dream_px[128 * 128];
static int dream_w, dream_h;
static char dream_about[OK_MIND_DREAM + 1];
static uint8_t dream_png[64 * 1024];      /* a 128x128 PNG is 25..50 KB */
static uint8_t dream_scratch[120 * 1024]; /* the PNG's data plus 128 rows of 385 bytes */
static char asked[OK_MIND_NOTE + 1];
static uint32_t asked_at;
static char heard_text[OK_MIND_TEXT + 1], saw_text[OK_MIND_TEXT + 1];
static const uint8_t *clip;
static size_t clip_len;
static volatile int clip_waiting, look_wanted, ear_state;
static uint32_t looked_at;
static uint8_t photo[64 * 1024]; /* a camera PNG (37 KB) or a drawing (49 KB) */
/* Pictionary: the drawing to guess, and the guess once 0G has made it. */
static uint8_t doodle[512];
static volatile int guess_wanted, guess_state; /* guess_state: 1 new guess, -1 it could not */
static char guess_text[OK_MIND_TEXT + 1];
static char sense_buf[OK_EAR_RATE * OK_EAR_SECONDS + 20 * 1024]; /* a clip's multipart, or a photo in base64 */
static char chain[65]; /* sha256 of the last episode written */

/* Episodes waiting to go to 0G Storage together, one JSON object a line: one
 * submission for many keeps the gas down. */
#define BATCH_MAX (160 * 1024) /* its upload body, 4/3 of it, fits in episode[] */
static char batch[BATCH_MAX];
static size_t batch_len;
static int batch_count;
static uint32_t stored_at;
static int want_store;
static ok_store store; /* its request bodies go in episode[], free while a batch is stored */

static struct {
    char host[96], provider[48], model[64], ip[20];
    uint8_t signer[20];
    long tz_minutes;
    /* Ears: a speech-to-text provider (Whisper), optional. */
    char ear_host[96], ear_provider[48], ear_model[64], ear_ip[20];
    uint8_t ear_signer[20];
    int have_ear;
    /* Dreams: an image model (z-image-turbo), optional. */
    char dream_host[96], dream_provider[48], dream_model[64], dream_ip[20];
    uint8_t dream_signer[20];
    int have_dream;
    /* Eyes: look through the camera every so many seconds while the player
     * is there (0 = only when asked with R). */
    long look_every;
    char city[41];
    /* 0G Storage: store=mainnet turns it on; store_every= minutes between
     * batches (10); store_node= "ip:port:shard" lines override the nodes. */
    int store_on;
    long store_every;
    char store_node[8][32];
    int store_nodes;
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

/* The live feed on the SELECT screens: every real 0G step as it happens. */
#define feed(...) ok_facts_log(ok_dsi_uptime(), 'G', __VA_ARGS__)

void ok_dsi_mind_observe(const ok_pet *p, int outside, uint32_t seconds_since_player, const char *did)
{
    seen_pet = *p;
    have_pet = 1;
    seen_outside = outside;
    seen_idle = seconds_since_player;
    if (did) snprintf(seen_did, sizeof seen_did, "%s", did);
}

void ok_dsi_mind_toggle(void) { paused = !paused; }

int ok_dsi_mind_ear_busy(void) { return clip_waiting; }
int ok_dsi_mind_ear_state(void) { return ear_state; }

void ok_dsi_mind_hear(const uint8_t *wav, size_t len)
{
    if (clip_waiting || !wav) return;
    clip = wav;
    clip_len = len;
    clip_waiting = 1; /* the clip stays put until the mind has sent it */
    ear_state = 2;
}

void ok_dsi_mind_look(void) { look_wanted = 1; }

void ok_dsi_mind_letters(int allowed) { letters_ok = allowed; }

int ok_dsi_mind_take_object(ok_object *o)
{
    if (!made_new) return 0;
    *o = made;
    made_new = 0;
    return 1;
}

int ok_dsi_mind_take_drawing(uint8_t pixels[512], char name[21])
{
    if (!drew_new) return 0;
    memcpy(pixels, drew, 512);
    memcpy(name, drew_name, 21);
    drew_new = 0;
    return 1;
}

int ok_dsi_mind_take_letter(char text[141])
{
    if (!letter_new) return 0;
    memcpy(text, letter, 141);
    letter_new = 0;
    return 1;
}

const uint16_t *ok_dsi_mind_take_dream(int *w, int *h, const char **about)
{
    if (!dream_new) return NULL;
    dream_new = 0;
    *w = dream_w;
    *h = dream_h;
    *about = dream_about;
    return dream_px;
}

void ok_dsi_mind_guess(const uint8_t pixels[512])
{
    if (guess_wanted) return;
    memcpy(doodle, pixels, sizeof doodle);
    guess_wanted = 1;
}

int ok_dsi_mind_take_guess(char *out, size_t cap)
{
    int s = guess_state;
    if (!s) return 0;
    guess_state = 0;
    if (s > 0) snprintf(out, cap, "%s", guess_text);
    return s;
}

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
        /* Even when data is already there, let the game run first: a long
         * 0G reply keeps the socket readable for the whole transfer. */
        cothread_yield();
        if (rc > 0) return (p.revents & events) != 0;
        if (rc < 0) return 0;
        cothread_yield_irq(IRQ_VBLANK);
    }
    return 0;
}

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx, n;
    if (!wait_for(fd, POLLIN, 60000)) return -1; /* yields every time, data or not */ /* inference can take a while */
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
    const char *ip = !strcmp(host, cfg.host) ? cfg.ip : !strcmp(host, cfg.ear_host) ? cfg.ear_ip
                     : !strcmp(host, cfg.dream_host) ? cfg.dream_ip : "";
    if (ip[0] && inet_aton(ip, out)) return 1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ok_dsi_mind_where = "getaddrinfo";
    if (getaddrinfo(host, "443", &hints, &ai) || !ai) return 0;
    ok_dsi_mind_where = "resolved";
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
            feed("clock: %.32s", ok_facts_now.og_clock);
            return 1;
        }
    }
    snprintf(ok_facts_now.og_clock, sizeof ok_facts_now.og_clock, "no NTP: RTC%s", cfg.tz_minutes ? " + tz" : "");
    line(4, "Clock: %.25s", ok_facts_now.og_clock);
    feed("clock: no NTP answer, using the RTC");
    return 0;
}

/* What a failure means, in a few words for the screens. */
static const char *why(int e)
{
    switch (e) {
    case 0: return "";
    case -2: return "DNS: no address";
    case -3: return "TCP: no connection";
    case -5: return "answer too big";
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
static int resumed; /* the open connection offered to resume a session */
static char open_host[96]; /* the host the open connection goes to */
volatile const char *ok_dsi_mind_where = "not started";

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
        feed("DNS: no address for %.22s", host);
        return -2;
    }
    line(2, "TCP %s:443", inet_ntoa(addr.sin_addr));
    feed("TCP %s:443 ...", inet_ntoa(addr.sin_addr));
    ok_dsi_mind_where = "tcp connect";
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -3;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    if ((connect(fd, (struct sockaddr *)&addr, sizeof addr) && errno != EINPROGRESS) || !wait_for(fd, POLLOUT, 8000) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &elen) || error) {
        hang_up();
        line(2, "TCP: no connection");
        feed("TCP: no connection");
        return -3;
    }
    draw(seed, sizeof seed);
    snprintf(open_host, sizeof open_host, "%s", host);
    {
        int k, free_slot = -1;
        for (k = 0; k < TLS_HOSTS && strcmp(tls_by_host[k].host, host); k++)
            if (free_slot < 0 && !tls_by_host[k].host[0]) free_slot = k;
        tlsp = &tls_by_host[k < TLS_HOSTS ? k : free_slot >= 0 ? free_slot : 0];
    }
    resumed = ok_tls_resume(&tls, host, utc_now(), seed, sizeof seed, sock_read, sock_write, &fd);
    ok_wipe(seed, sizeof seed);
    line(2, "TLS 1.2 to %.20s", host);
    return 0;
}

static void tls_name(char *out, size_t cap);

static int fetch(void *ctx, const char *host, const char *req, size_t n, const char *body, size_t body_len,
                 char *resp, size_t cap, size_t *got)
{
    int e, tries;
    uint32_t t0;
    (void)ctx;
    if (fd >= 0 && strcmp(open_host, host)) hang_up(); /* another provider: another connection */
    for (tries = 0; tries < 2; tries++) {
        int reused = fd >= 0;
        if (!reused && (e = dial(host))) return e;
        t0 = cpuGetTiming();
        e = ok_think_exchange(&tls, req, n, body, body_len, resp, cap, got);
        if (!e && !reused) {
            char suite[24];
            tls_name(suite, sizeof suite);
            feed("%.23s %s %lu ms", suite, ok_tls_resumed(&tls) ? "resumed" : resumed ? "renewed" : "new",
                 (unsigned long)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u));
        }
        if (!e) {
            if (!ok_think_keeps_open(resp, *got)) hang_up();
            return 0;
        }
        hang_up();
        if (!reused) {
            line(2, "TLS %d: %.22s", e, why(e));
            feed("TLS failed %d: %.24s", e, why(e));
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

static void trim(char *s)
{
    size_t n;
    s[strcspn(s, "\r\n")] = 0;
    for (n = strlen(s); n && (s[n - 1] == ' ' || s[n - 1] == '\t'); n--) s[n - 1] = 0;
}

static int load_settings(uint8_t key[32])
{
    char path[64], text[160], *v;
    int have_signer = 0, ok;
    FILE *f;
    memset(&cfg, 0, sizeof cfg);
    cfg.look_every = 120;
    cfg.store_every = 10;
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
        else if (!strcmp(text, "ear_host")) snprintf(cfg.ear_host, sizeof cfg.ear_host, "%s", v);
        else if (!strcmp(text, "ear_provider")) snprintf(cfg.ear_provider, sizeof cfg.ear_provider, "%s", v);
        else if (!strcmp(text, "ear_model")) snprintf(cfg.ear_model, sizeof cfg.ear_model, "%s", v);
        else if (!strcmp(text, "ear_ip")) snprintf(cfg.ear_ip, sizeof cfg.ear_ip, "%s", v);
        else if (!strcmp(text, "ear_signer")) cfg.have_ear = unhex(v, cfg.ear_signer, 20);
        else if (!strcmp(text, "look_every")) cfg.look_every = strtol(v, NULL, 10);
        else if (!strcmp(text, "dream_host")) snprintf(cfg.dream_host, sizeof cfg.dream_host, "%.95s", v);
        else if (!strcmp(text, "dream_provider")) snprintf(cfg.dream_provider, sizeof cfg.dream_provider, "%.47s", v);
        else if (!strcmp(text, "dream_model")) snprintf(cfg.dream_model, sizeof cfg.dream_model, "%.63s", v);
        else if (!strcmp(text, "dream_ip")) snprintf(cfg.dream_ip, sizeof cfg.dream_ip, "%.19s", v);
        else if (!strcmp(text, "dream_signer")) cfg.have_dream = unhex(v, cfg.dream_signer, 20);
        else if (!strcmp(text, "city")) snprintf(cfg.city, sizeof cfg.city, "%.40s", v);
        else if (!strcmp(text, "store")) cfg.store_on = !strcmp(v, "mainnet");
        else if (!strcmp(text, "store_every")) cfg.store_every = strtol(v, NULL, 10);
        else if (!strcmp(text, "store_node") && cfg.store_nodes < 8)
            snprintf(cfg.store_node[cfg.store_nodes++], sizeof cfg.store_node[0], "%.31s", v);
    }
    fclose(f);
    if (!cfg.host[0] || !cfg.provider[0] || !cfg.model[0] || !have_signer) return 0;
    cfg.have_ear = cfg.have_ear && cfg.ear_host[0] && cfg.ear_provider[0] && cfg.ear_model[0];
    cfg.have_dream = cfg.have_dream && cfg.dream_host[0] && cfg.dream_provider[0] && cfg.dream_model[0];
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

/* A file written in 4 KB pieces, letting the game draw between them (the SD
 * card is slow, and a big episode in one go would freeze the screen). */
static int write_slowly(FILE *f, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    while (n) {
        size_t k = n < 4096 ? n : 4096;
        if (fwrite(p, 1, k, f) != k) return 0;
        p += k;
        n -= k;
        cothread_yield();
    }
    return 1;
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
        ok = write_slowly(f, episode, n);
        ok = fclose(f) == 0 && ok;
    }
    if (!ok) {
        line(13, "Episode: SD write FAILED");
        return;
    }
    {
        br_sha256_context c; /* in pieces, breathing between them */
        size_t i;
        br_sha256_init(&c);
        for (i = 0; i < n; i += 8192) {
            br_sha256_update(&c, episode + i, n - i < 8192 ? n - i : 8192);
            cothread_yield();
        }
        br_sha256_out(&c, hash);
    }
    /* And into the batch for 0G Storage (one that would not fit waits for
     * the next batch; one bigger than a batch stays on the SD card only). */
    if (cfg.store_on && n + 1 < BATCH_MAX / 2) {
        if (batch_len + n + 1 > BATCH_MAX - 256) want_store = 1;
        else {
            memcpy(batch + batch_len, episode, n);
            batch_len += n;
            batch[batch_len++] = '\n';
            batch_count++;
        }
    }
    hex32(hash, chain);
    snprintf(path, sizeof path, "%sepisodes/chain.txt", dir_path);
    if ((f = fopen(path, "w"))) {
        fprintf(f, "%s\n", chain);
        fclose(f);
    }
    line(13, "Episode %u on SD, chained", count);
    feed("episode %u on SD, chained %.8s", count, chain);
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
    for (j = OK_MIND_RECENT - 1; j > 0; j--) memcpy(recent[j], recent[j - 1], sizeof recent[0]);
    ok_mind_memory(d, recent[0]);
    if (recent_count < OK_MIND_RECENT) recent_count++;
}

/* memory.txt: one fact a line, the ones its mind chose to keep. */
static void load_facts(void)
{
    static char text[OK_MIND_KNOWN * (OK_MIND_NOTE + 2) + 1];
    char path[64];
    size_t n = 0;
    FILE *f;
    snprintf(path, sizeof path, "%smemory.txt", dir_path);
    ok_memory_init(&facts);
    if ((f = fopen(path, "r"))) {
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
    }
    ok_memory_load(&facts, text, n);
}

static void save_facts(void)
{
    static char text[OK_MIND_KNOWN * (OK_MIND_NOTE + 2) + 1];
    char path[64];
    size_t n;
    FILE *f;
    snprintf(path, sizeof path, "%smemory.txt", dir_path);
    if (ok_memory_save(&facts, text, sizeof text, &n) != OK_OK || !(f = fopen(path, "w"))) return;
    fwrite(text, 1, n, f);
    fclose(f);
}

/* What happened today, for the diary (the oldest goes when it is full). */
static void today_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void today_add(const char *fmt, ...)
{
    va_list ap;
    if (today_count == 24) {
        memmove(today[0], today[1], sizeof today[0] * 23);
        today_count--;
    }
    va_start(ap, fmt);
    vsnprintf(today[today_count++], sizeof today[0], fmt, ap);
    va_end(ap);
}

/* diary.txt: "YYYY-MM-DD entry" a line; the last one is yesterday. */
static void load_diary(void)
{
    char path[64], text[OK_MIND_DIARY + 16];
    FILE *f;
    snprintf(path, sizeof path, "%sdiary.txt", dir_path);
    yesterday[0] = 0;
    if (!(f = fopen(path, "r"))) return;
    while (fgets(text, sizeof text, f))
        if (strlen(text) > 11) {
            text[strcspn(text, "\r\n")] = 0;
            snprintf(yesterday, sizeof yesterday, "%.240s", text + 11);
            if (!strncmp(text, today_date, 10)) diary_written = 1;
        }
    fclose(f);
}

static void save_diary(const char *date, const char *entry)
{
    char path[64];
    FILE *f;
    snprintf(path, sizeof path, "%sdiary.txt", dir_path);
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, "%.10s %s\n", date, entry);
    fclose(f);
}

/* A file in a folder of its own on the SD card (objects/, drawings/, dreams/). */
static void save_made(const char *folder, const char *name, const char *ext, const void *data, size_t n)
{
    char path[128], clean[24];
    size_t i, o = 0;
    FILE *f;
    for (i = 0; name[i] && o < sizeof clean - 1; i++) {
        char c = name[i];
        clean[o++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '-';
    }
    clean[o] = 0;
    snprintf(path, sizeof path, "%s%s", dir_path, folder);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s%s/%s-%s.%s", dir_path, folder, today_date, o ? clean : "x", ext);
    if ((f = fopen(path, "wb"))) {
        write_slowly(f, data, n);
        fclose(f);
    }
}

/* Waits, but not when the player has just spoken or asked it to look. */
static void rest_frames(int frames)
{
    while (frames-- > 0 && !clip_waiting && !look_wanted && !guess_wanted && !want_make[0] && !want_draw[0]) wait_frames(1);
}

/* A failed call: say why on both screens. */
static void failed(void)
{
    if (t.stage == OK_THINK_NET || t.stage == OK_THINK_SIG_NET)
        stopped("%s %d: %.16s", ok_think_stage_name(t.stage), t.net_error, why(t.net_error));
    else if (t.stage == OK_THINK_HTTP)
        stopped("HTTP %d: %.20s", t.http_status, t.response ? t.response : "");
    else
        stopped("%s", ok_think_stage_name(t.stage));
    feed("safe stop: %.28s", ok_facts_now.og_error);
    hang_up();
    /* A certificate the clock cannot place: ask NTP again first. */
    if (t.net_error == BR_ERR_X509_EXPIRED || t.net_error == BR_ERR_X509_TIME_UNKNOWN) clock_synced = 0;
}

/* Plain HTTP to a storage node, host "ip:port". */
static int http_fetch(void *ctx, const char *host, const char *head, size_t n, const char *body, size_t len, char *resp,
                      size_t cap, size_t *got)
{
    struct sockaddr_in addr;
    char name[32], *colon;
    int nfd, error = 0, k;
    socklen_t elen = sizeof error;
    size_t sent;
    (void)ctx;
    snprintf(name, sizeof name, "%s", host);
    if (!(colon = strchr(name, ':'))) return -2;
    *colon = 0;
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)atoi(colon + 1));
    if (!inet_aton(name, &addr.sin_addr)) return -2;
    if ((nfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -3;
    fcntl(nfd, F_SETFL, fcntl(nfd, F_GETFL, 0) | O_NONBLOCK);
    if ((connect(nfd, (struct sockaddr *)&addr, sizeof addr) && errno != EINPROGRESS) || !wait_for(nfd, POLLOUT, 8000) ||
        getsockopt(nfd, SOL_SOCKET, SO_ERROR, &error, &elen) || error) {
        close(nfd);
        return -3;
    }
    for (k = 0; k < 2; k++) {
        const char *p = k ? body : head;
        size_t total = k ? len : n;
        for (sent = 0; sent < total;) {
            int w;
            if (!wait_for(nfd, POLLOUT, 10000)) {
                close(nfd);
                return -1;
            }
            w = send(nfd, p + sent, total - sent > 4096 ? 4096 : total - sent, 0);
            if (w <= 0) {
                close(nfd);
                return -1;
            }
            sent += (size_t)w;
        }
    }
    *got = 0;
    while (*got < cap && wait_for(nfd, POLLIN, 20000)) {
        int r = recv(nfd, resp + *got, cap - *got, 0);
        if (r <= 0) break;
        *got += (size_t)r;
        if (ok_think_complete(resp, *got)) break;
    }
    close(nfd);
    resp[*got < cap ? *got : cap - 1] = 0;
    return *got ? 0 : -1;
}

static void wait_ms(int ms) { wait_frames(ms * 60 / 1000 + 1); }
static void store_step(const char *step) { feed("storage: %.29s", step); }

/* The batch so far goes to 0G Storage: a first line says what it is, then
 * one episode a line. */
static void flush_batch(const uint8_t key[32])
{
    static const char *mainnet[4] = {"34.71.110.60:5678:0", "34.66.131.173:5678:0", "34.169.236.186:5678:1",
                                     "34.60.163.4:5678:1"};
    static char node_host[8][24];
    ok_store_net net;
    char head[160], path[64];
    size_t hl;
    FILE *f;
    int i, shards = 0;
    if (!batch_len) return;
    memset(&net, 0, sizeof net);
    net.rpc_host = "evmrpc.0g.ai";
    net.chain_id = 16661;
    unhex("62d4144db0f0a6fbbaeb6296c785c71b3d57c526", net.flow, 20);
    unhex("f574ff1fd8602f0131baab74f9f7cf24993321de", net.market, 20);
    for (i = 0; i < (cfg.store_nodes ? cfg.store_nodes : 4) && i < 8; i++) {
        const char *spec = cfg.store_nodes ? cfg.store_node[i] : mainnet[i];
        const char *last = strrchr(spec, ':');
        if (!last) continue;
        snprintf(node_host[net.node_count], sizeof node_host[0], "%.*s", (int)(last - spec), spec);
        net.node[net.node_count] = node_host[net.node_count];
        net.node_shard[net.node_count] = atoi(last + 1);
        if (net.node_shard[net.node_count] + 1 > shards) shards = net.node_shard[net.node_count] + 1;
        net.node_count++;
    }
    net.shards = shards;
    hl = (size_t)snprintf(head, sizeof head, "{\"okupa_batch\":1,\"episodes\":%d,\"last_sha256\":\"%s\"}\n", batch_count, chain);
    if (hl + batch_len > sizeof batch) return;
    memmove(batch + hl, batch, batch_len);
    memcpy(batch, head, hl);
    feed("storage: %d episodes, %lu KB", batch_count, (unsigned long)((hl + batch_len) / 1024));
    if (ok_store_put(&store, &net, key, (const uint8_t *)batch, hl + batch_len, episode, sizeof episode, fetch,
                     http_fetch, NULL, wait_ms, store_step) == OK_OK &&
        store.stage == OK_STORE_DONE) {
        feed("stored on 0G: %.26s", store.root);
        if (store.tx[0]) feed("tx %.35s", store.tx);
        snprintf(ok_facts_now.og_storage_root, sizeof ok_facts_now.og_storage_root, "%s", store.root);
        snprintf(path, sizeof path, "%sepisodes/stored.txt", dir_path);
        if ((f = fopen(path, "a"))) {
            fprintf(f, "%s %s %llu %d %s\n", store.root, store.tx[0] ? store.tx : "-", (unsigned long long)store.seq,
                    batch_count, chain);
            fclose(f);
        }
        batch_len = 0;
        batch_count = 0;
    } else {
        feed("storage failed: %.24s", store.error[0] ? store.error : "?");
        /* the batch stays (without its first line) for the next try */
        memmove(batch, batch + hl, batch_len);
    }
}

/* A number member of an object, or -1. */
static long number(const ok_json *obj, const char *key)
{
    ok_json v;
    long n = 0;
    size_t i;
    if (ok_json_member(obj, key, &v) != OK_OK || !v.len) return -1;
    for (i = 0; i < v.len && v.at[i] >= '0' && v.at[i] <= '9'; i++) n = n * 10 + (v.at[i] - '0');
    return i ? n : -1;
}

/* After a verified call: who signed, and what it used (from the provider's
 * own usage counts, inside the signed bytes). */
static void feed_verified(uint32_t ms)
{
    ok_json root, usage;
    long in, out;
    feed("TEE sig ok %.6s..%.4s %lu ms", t.recovered, t.recovered + 38, (unsigned long)ms);
    if (t.response && ok_json_parse(t.response, t.response_len, &root) == OK_OK &&
        ok_json_member(&root, "usage", &usage) == OK_OK) {
        in = number(&usage, "prompt_tokens");
        out = number(&usage, "completion_tokens");
        if (in >= 0) feed("paid for %ld tokens in, %ld out", in, out < 0 ? 0 : out);
        else if ((in = number(&usage, "seconds")) >= 0) feed("paid for %ld s of audio", in);
    }
}

static int thinker(void *arg)
{
    uint8_t key[32], nonce[16], mac[6];
    char did[64], why_cam[33];
    ok_think_provider p, ear, dreamer;
    ok_mind_obs obs;
    ok_pet pet;
    time_t rtc;
    struct tm *tm;
    uint64_t now_ms;
    uint32_t t0, ms;
    unsigned count = 0, verified = 0, fails = 0;
    size_t n;
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
    load_facts();
    {
        time_t now = time(NULL);
        strftime(today_date, sizeof today_date, "%Y-%m-%d", localtime(&now));
    }
    load_diary();
    line(1, "%.20s%s", cfg.model, cfg.have_ear ? " + ears" : "");
    feed("mind on: %.22s%s", cfg.model, cfg.have_ear ? " + ears" : "");
    p.host = cfg.host;
    p.provider = cfg.provider;
    p.model = cfg.model;
    memcpy(p.signer, cfg.signer, 20);
    p.generation = 0;
    ear.host = cfg.ear_host;
    ear.provider = cfg.ear_provider;
    ear.model = cfg.ear_model;
    memcpy(ear.signer, cfg.ear_signer, 20);
    ear.generation = 0;
    dreamer.host = cfg.dream_host;
    dreamer.provider = cfg.dream_provider;
    dreamer.model = cfg.dream_model;
    memcpy(dreamer.signer, cfg.dream_signer, 20);
    dreamer.generation = 0;

    for (;;) {
        if ((paused || !wifi_ready()) && guess_wanted) { /* no guess without its mind */
            guess_wanted = 0;
            guess_state = -1;
        }
        if (paused) {
            line(0, "0G mind: paused (Y resumes)");
            hang_up();
            wait_frames(30);
            continue;
        }
        if (!wifi_ready() || !have_pet) {
            line(0, "0G mind: waiting for Wi-Fi");
            if (strcmp(ok_facts_now.og_error, "waiting for Wi-Fi")) feed("waiting for Wi-Fi");
            snprintf(ok_facts_now.og_error, sizeof ok_facts_now.og_error, "waiting for Wi-Fi");
            hang_up();
            wait_frames(60);
            continue;
        }
        Wifi_GetData(WIFIGETDATA_MACADDRESS, sizeof mac, mac);
        ok_keccak_update(&pool, mac, sizeof mac);
        if (!clock_synced || frames_alive - clock_synced_at > 60u * 1800u) clock_sync();

        /* 1. Ears: what the player said, in words, checked. */
        if (clip_waiting) {
            if (cfg.have_ear) {
                now_ms = (uint64_t)utc_now() * 1000u;
                draw(nonce, sizeof nonce);
                line(3, "Listening: %lu bytes to 0G", (unsigned long)clip_len);
                feed("hearing: %lu KB to %.20s", (unsigned long)(clip_len / 1024), cfg.ear_model);
                t0 = cpuGetTiming();
                r = ok_think_hear(&t, &ear, key, now_ms, nonce, clip, clip_len, sense_buf, sizeof sense_buf, fetch,
                                  NULL);
                ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
                if (r == OK_OK && t.stage == OK_THINK_DECIDED) feed_verified(ms);
                if (r == OK_OK && t.stage == OK_THINK_DECIDED && t.heard[0]) {
                    snprintf(heard_text, sizeof heard_text, "%s", t.heard);
                    line(3, "Heard: %.25s", t.heard);
                    feed("heard: \"%.30s\"", t.heard);
                    today_add("heard the player say: %.56s", t.heard);
                } else if (r == OK_OK) {
                    failed();
                }
                save_episode(&ear, now_ms, count);
            } else {
                stopped("no ears: ear_* in mind.txt");
            }
            clip_waiting = 0;
            ear_state = 0;
        }

        /* 2. Eyes: through the camera when asked, or now and then while the
         * player is there. */
        if (look_wanted || (cfg.look_every > 0 && seen_idle < 60 &&
                            frames_alive - looked_at > (uint32_t)cfg.look_every * 60u)) {
            look_wanted = 0;
            looked_at = frames_alive;
            if (ok_dsi_eye_snap(photo, sizeof photo, &n, why_cam)) {
                now_ms = (uint64_t)utc_now() * 1000u;
                draw(nonce, sizeof nonce);
                line(3, "Looking: %lu B photo to 0G", (unsigned long)n);
                feed("looking: %lu KB photo to vision", (unsigned long)(n / 1024));
                t0 = cpuGetTiming();
                r = ok_think_see(&t, &p, key, now_ms, nonce, photo, n, sense_buf, sizeof sense_buf, fetch, NULL);
                ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
                if (r == OK_OK && t.stage == OK_THINK_DECIDED) {
                    feed_verified(ms);
                    snprintf(saw_text, sizeof saw_text, "%s", t.saw);
                    line(3, "Saw: %.27s", t.saw);
                    feed("saw: %.33s", t.saw);
                    today_add("saw: %.75s", t.saw);
                } else if (r == OK_OK) {
                    failed();
                }
                save_episode(&p, now_ms, count);
            } else {
                stopped("%s", why_cam);
                feed("%.38s", why_cam);
            }
        }

        /* Pictionary: the player's drawing, guessed by 0G's vision model. */
        if (guess_wanted) {
            if (ok_png_drawing(doodle, 2, photo, sizeof photo, &n) == OK_OK) {
                now_ms = (uint64_t)utc_now() * 1000u;
                draw(nonce, sizeof nonce);
                feed("guessing your drawing (%lu KB)", (unsigned long)(n / 1024));
                t0 = cpuGetTiming();
                r = ok_think_guess(&t, &p, key, now_ms, nonce, photo, n, sense_buf, sizeof sense_buf, fetch, NULL);
                ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
                if (r == OK_OK && t.stage == OK_THINK_DECIDED) {
                    feed_verified(ms);
                    snprintf(guess_text, sizeof guess_text, "%s", t.saw);
                    feed("guess: %.32s", t.saw);
                    today_add("guessed the player's drawing: %.48s", t.saw);
                    line(3, "Guess: %.25s", t.saw);
                    guess_state = 1;
                } else {
                    if (r == OK_OK) failed();
                    guess_state = -1;
                }
                save_episode(&p, now_ms, count);
            } else {
                guess_state = -1;
            }
            guess_wanted = 0;
        }

        /* 3. What it notices, copied from the game loop, and what it does. */
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
            if (strncmp(did, "touched", 7)) today_add("the player %.66s", did);
        }
        if (aim.goal[0]) obs.goal = aim.goal;
        if (aim.plan[0]) obs.plan = aim.plan;
        if (yesterday[0]) obs.diary = yesterday;
        if (heard_text[0]) obs.heard = heard_text;
        if (saw_text[0]) obs.saw = saw_text;
        if (asked[0] && frames_alive - asked_at > 60u * 180u) asked[0] = 0; /* nobody answered in 3 minutes */
        if (asked[0]) obs.asked = asked;
        if (cfg.city[0]) obs.city = cfg.city;
        obs.can_write_letter = letters_ok;
        ok_memory_tell(&facts, &obs);
        for (j = 0; j < recent_count; j++) obs.recent[j] = recent[j];
        obs.recent_count = recent_count;

        now_ms = (uint64_t)utc_now() * 1000u;
        draw(nonce, sizeof nonce);
        feed("asking %.24s...", cfg.model);
        t0 = cpuGetTiming();
        ok_dsi_mind_where = "thinking (TLS)";
        r = ok_think_run(&t, &p, key, now_ms, nonce, &obs, fetch, NULL);
        ok_dsi_mind_where = "after thinking";
        ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
        count++;
        if (r != OK_OK) {
            line(12, "Setup failed: %s", ok_result_name(r));
            break;
        }
        record_facts(&p, ms);
        line(5, "Request id (0G chat id):");
        line(6, "%.32s", t.chat_id[0] ? t.chat_id : "-");
        line(7, "%.32s", strlen(t.chat_id) > 32 ? t.chat_id + 32 : "");
        line(8, "TEE %.6s..%.4s %s", t.signer, t.signer + 38,
             t.stage == OK_THINK_DECIDED || t.stage == OK_THINK_ANSWER ? "VERIFIED" : "not verified");
        if (t.stage == OK_THINK_DECIDED) {
            verified++;
            fails = 0;
            line(9, "Does: %s, %s", ok_mind_activity_name(t.decision.activity), ok_mind_mood_name(t.decision.mood));
            line(10, "\"%.30s\"", t.decision.line);
            line(11, "%.32s", t.decision.ask[0] ? t.decision.ask : t.decision.thought);
            line(12, "Decided by 0G, checked here");
            line(4, "Clock: %.25s", ok_facts_now.og_clock);
            ok_facts_now.og_error[0] = 0;
            feed_verified(ms);
            feed("chose: %s, %s", ok_mind_activity_name(t.decision.activity), ok_mind_mood_name(t.decision.mood));
            if (t.decision.line[0]) {
                feed("says: %.32s", t.decision.line);
                today_add("%s and said: %.50s", ok_mind_activity_name(t.decision.activity), t.decision.line);
            }
            if (t.decision.thought[0]) feed("thinks: %.30s", t.decision.thought);
            if (t.decision.ask[0]) feed("asks: %.32s", t.decision.ask);
            remember(&t.decision);
            /* What it heard and saw has been told: it moves on. */
            heard_text[0] = saw_text[0] = 0;
            if (obs.heard) asked[0] = 0; /* the player answered (or spoke over it) */
            if (t.decision.ask[0] && !asked[0]) {
                snprintf(asked, sizeof asked, "%s", t.decision.ask);
                asked_at = frames_alive;
            }
            if (t.decision.remember[0] && ok_memory_add(&facts, t.decision.remember)) {
                save_facts();
                line(13, "Keeps: %.25s", t.decision.remember);
                feed("keeps: %.32s", t.decision.remember);
                today_add("learned: %.70s", t.decision.remember);
            }
            if (t.decision.make[0] && !want_make[0]) snprintf(want_make, sizeof want_make, "%s", t.decision.make);
            if (t.decision.draw[0] && !want_draw[0]) snprintf(want_draw, sizeof want_draw, "%s", t.decision.draw);
            if (t.decision.letter[0] && letters_ok && !letter_new) {
                snprintf(letter, sizeof letter, "%s", t.decision.letter);
                letter_new = 1;
                letters_ok = 0; /* the game loop says again when another is allowed */
                feed("letter: %.31s", letter);
                today_add("wrote the player a letter: %.52s", letter);
            }
            mailbox = t.decision;
            mailbox_new = 1;
        } else {
            fails++;
            line(9, "Stopped at: %s", ok_think_stage_name(t.stage));
            line(10, " ");
            line(11, "Safe stop: no decision");
            failed();
        }
        line(3, "#%u: HTTP %d in %lu ms, %d known", count, t.http_status, (unsigned long)ms, facts.count);
        line(0, "0G mind: alive, %u/%u verified", verified, count);
        save_episode(&p, now_ms, count);

        /* What it decided to make: an object, or a drawing of its own. */
        if (want_make[0]) {
            now_ms = (uint64_t)utc_now() * 1000u;
            draw(nonce, sizeof nonce);
            feed("making: %.30s", want_make);
            t0 = cpuGetTiming();
            r = ok_think_make(&t, &p, key, now_ms, nonce, want_make, fetch, NULL);
            ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
            if (r == OK_OK && t.stage == OK_THINK_DECIDED) {
                feed_verified(ms);
                made = t.object;
                if (!made.name[0]) snprintf(made.name, sizeof made.name, "%.20s", want_make);
                feed("made: %.20s, %d boxes", made.name, made.count);
                today_add("made %.60s out of boxes", made.name);
                save_made("objects", made.name, "bin", &made, sizeof made);
                made_new = 1;
            } else if (r == OK_OK) {
                failed();
            }
            save_episode(&p, now_ms, count);
            want_make[0] = 0;
        }
        if (want_draw[0]) {
            now_ms = (uint64_t)utc_now() * 1000u;
            draw(nonce, sizeof nonce);
            feed("drawing: %.29s", want_draw);
            t0 = cpuGetTiming();
            r = ok_think_draw(&t, &p, key, now_ms, nonce, want_draw, fetch, NULL);
            ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
            if (r == OK_OK && t.stage == OK_THINK_DECIDED) {
                feed_verified(ms);
                memcpy(drew, t.drawing, sizeof drew);
                snprintf(drew_name, sizeof drew_name, "%.20s", t.drawing_name[0] ? t.drawing_name : want_draw);
                feed("drew: %.32s", drew_name);
                today_add("drew %.70s", drew_name);
                if (ok_png_drawing(drew, 2, photo, sizeof photo, &n) == OK_OK) save_made("drawings", drew_name, "png", photo, n);
                drew_new = 1;
            } else if (r == OK_OK) {
                failed();
            }
            save_episode(&p, now_ms, count);
            want_draw[0] = 0;
        }

        /* The slow mind: a goal and a plan every five minutes. */
        if (t.stage == OK_THINK_DECIDED && (!aimed || frames_alive - aimed_at > 60u * 300u)) {
            aimed = 1;
            aimed_at = frames_alive;
            now_ms = (uint64_t)utc_now() * 1000u;
            draw(nonce, sizeof nonce);
            feed("slow mind: thinking ahead...");
            t0 = cpuGetTiming();
            r = ok_think_plan(&t, &p, key, now_ms, nonce, &obs, fetch, NULL);
            ms = (uint32_t)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u);
            if (r == OK_OK && t.stage == OK_THINK_DECIDED) {
                feed_verified(ms);
                aim = t.plan;
                feed("goal: %.32s", aim.goal);
                feed("plan: %.32s", aim.plan);
                line(2, "Goal: %.26s", aim.goal);
                today_add("decided: %.70s", aim.goal);
            } else if (r == OK_OK) {
                failed();
            }
            save_episode(&p, now_ms, count);
        }

        /* The diary: once the day is over (a new date) or after 23:00. */
        {
            char date[11];
            time_t now = time(NULL);
            struct tm *lt = localtime(&now);
            strftime(date, sizeof date, "%Y-%m-%d", lt);
            if (strcmp(date, today_date) && !diary_written && today_count >= 3) diary_written = -1; /* yesterday's is owed */
            if (strcmp(date, today_date) && diary_written != -1) {
                snprintf(today_date, sizeof today_date, "%s", date); /* a new day */
                today_count = 0;
                diary_written = 0;
            }
            if (today_count >= 3 && (diary_written == -1 || (!diary_written && lt->tm_hour >= 23))) {
                for (j = 0; j < today_count; j++) obs.today[j] = today[j];
                obs.today_count = today_count;
                obs.date = today_date;
                now_ms = (uint64_t)utc_now() * 1000u;
                draw(nonce, sizeof nonce);
                feed("diary: writing %s...", today_date);
                r = ok_think_diary(&t, &p, key, now_ms, nonce, &obs, fetch, NULL);
                if (r == OK_OK && t.stage == OK_THINK_DECIDED) {
                    feed_verified(0);
                    save_diary(today_date, t.diary);
                    snprintf(yesterday, sizeof yesterday, "%s", t.diary);
                    feed("diary: %.31s", t.diary);
                    diary_written = 1;
                    if (t.dream[0] && cfg.have_dream) { /* and tonight's dream, drawn by the image model */
                        char about[OK_MIND_DREAM + 1];
                        snprintf(about, sizeof about, "%s", t.dream);
                        save_episode(&p, now_ms, count);
                        now_ms = (uint64_t)utc_now() * 1000u;
                        draw(nonce, sizeof nonce);
                        feed("dreaming: %.28s", about);
                        r = ok_think_dream(&t, &dreamer, key, now_ms, nonce, about, dream_png, sizeof dream_png,
                                           sense_buf, sizeof sense_buf, fetch, NULL);
                        if (r == OK_OK && t.stage == OK_THINK_DECIDED &&
                            ok_png_read_rgb15(dream_png, t.image_len, dream_px, 128 * 128, &dream_w, &dream_h, dream_scratch,
                                              sizeof dream_scratch) == OK_OK) {
                            feed_verified(0);
                            feed("dream: %dx%d picture, decoded here", dream_w, dream_h);
                            snprintf(dream_about, sizeof dream_about, "%s", about);
                            save_made("dreams", "dream", "png", dream_png, t.image_len);
                            dream_new = 1;
                        } else if (r == OK_OK) {
                            failed();
                        }
                        if (r == OK_OK) save_episode(&dreamer, now_ms, count);
                    }
                } else if (r == OK_OK) {
                    failed();
                }
                save_episode(&p, now_ms, count);
                if (diary_written == -1 || strcmp(date, today_date)) { /* written or not, the new day starts */
                    snprintf(today_date, sizeof today_date, "%s", date);
                    today_count = 0;
                    diary_written = 0;
                } else if (diary_written != 1) {
                    diary_written = 1; /* tonight's failed: one try a night, the day goes on */
                }
            }
        }

        /* 0G Storage: the batch every store_every minutes, or when it is full. */
        if (cfg.store_on && batch_len &&
            (want_store || frames_alive - stored_at > (uint32_t)cfg.store_every * 60u * 60u)) {
            stored_at = frames_alive;
            want_store = 0;
            flush_batch(key);
        }

        /* Every 12 s while the player is around, every 30 s otherwise, so
         * each thing it does has time to play out; after failures it backs
         * off up to a minute. Speaking or R cuts the wait short. */
        rest = seen_idle < 60 ? 12 : 30;
        if (fails) rest = fails >= 3 ? 60 : 10 << fails; /* each try costs a TLS handshake the game waits for */
        rest = rest * 60 - (int)(ms * 60 / 1000);
        rest_frames(rest > 60 * 3 ? rest : 60 * 3);
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
