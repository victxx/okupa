#include "mind.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
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
static ok_pet pet_copy;
static volatile int busy;
static char said[OK_MIND_LINE + 1];
static int said_new;

static ok_think t;
static ok_tls tls;
static char episode[OK_THINK_RESPONSE + OK_THINK_REQUEST * 2 + 2048];

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
int ok_dsi_mind_busy(void) { return busy; }

const char *ok_dsi_mind_take_line(void)
{
    if (!said_new) return NULL;
    said_new = 0;
    return said;
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

static int round_trips;

/* One HTTPS round trip on a fresh connection, for the shared core. */
static int fetch(void *ctx, const char *host, const char *req, size_t n, char *resp, size_t cap, size_t *got)
{
    struct sockaddr_in addr;
    uint8_t seed[32];
    int fd, error = 0, e, row = round_trips++ ? 4 : 2;
    socklen_t elen = sizeof error;
    (void)ctx;
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    if (!resolve(host, &addr.sin_addr)) {
        line(row, "DNS: no address for host");
        return -2;
    }
    line(row, "TCP %s:443", inet_ntoa(addr.sin_addr));
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -3;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    if ((connect(fd, (struct sockaddr *)&addr, sizeof addr) && errno != EINPROGRESS) || !wait_for(fd, POLLOUT, 8000) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &elen) || error) {
        close(fd);
        line(row, "TCP: no connection");
        return -3;
    }
    line(row, row == 2 ? "TLS: asking 0G..." : "TLS: fetching signature");
    draw(seed, sizeof seed);
    e = ok_think_https(&tls, host, (int64_t)time(NULL) - cfg.tz_minutes * 60, seed, sock_read, sock_write, &fd, req,
                       n, resp, cap, got);
    ok_wipe(seed, sizeof seed);
    close(fd);
    line(row, e ? "TLS: failed (BearSSL %d)" : "TLS 1.2 ok, %d bytes", e ? e : (int)*got);
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

static void save_episode(const ok_think_provider *p, uint64_t now_ms)
{
    char path[160];
    size_t n;
    FILE *f;
    int ok;
    if (ok_think_episode(&t, p, now_ms, episode, sizeof episode, &n) != OK_OK) {
        line(13, "Episode: too big, not saved");
        return;
    }
    snprintf(path, sizeof path, "%sepisodes", dir_path);
    mkdir(path, 0700);
    if (t.chat_id[0])
        snprintf(path, sizeof path, "%sepisodes/%.80s.json", dir_path, t.chat_id);
    else
        snprintf(path, sizeof path, "%sepisodes/stop-%llu.json", dir_path, (unsigned long long)now_ms);
    ok = (f = fopen(path, "wb")) != NULL;
    if (ok) {
        ok = fwrite(episode, 1, n, f) == n;
        ok = fclose(f) == 0 && ok;
    }
    line(13, ok ? "Episode saved to SD (%u B)" : "Episode: SD write FAILED", (unsigned)n);
}

static int thinker(void *arg)
{
    uint8_t key[32], nonce[16], mac[6];
    ok_think_provider p;
    time_t rtc;
    struct tm *tm;
    uint64_t now_ms;
    uint32_t t0;
    ok_result r;
    (void)arg;

    memset(lines, 0, sizeof lines);
    line(0, "0G mind: one decision");
    ok_keccak_init(&pool);
    stir();
    if (!dir_path[0] || !load_settings(key)) {
        line(1, "No mind.txt / mind.key on SD");
        line(12, "Safe stop: the okupa naps");
        busy = 0;
        return 0;
    }
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED || Wifi_GetIP() == 0 || Wifi_GetIP() == 0xffffffffu) {
        line(1, "No Wi-Fi yet: Dev > Wi-Fi");
        line(12, "Safe stop: the okupa naps");
        ok_wipe(key, sizeof key);
        busy = 0;
        return 0;
    }
    Wifi_GetData(WIFIGETDATA_MACADDRESS, sizeof mac, mac);
    ok_keccak_update(&pool, mac, sizeof mac);
    line(1, "%.32s", cfg.model);
    p.host = cfg.host;
    p.provider = cfg.provider;
    p.model = cfg.model;
    memcpy(p.signer, cfg.signer, 20);
    p.generation = 0;

    rtc = time(NULL);
    tm = localtime(&rtc);
    now_ms = ((uint64_t)rtc - (uint64_t)(cfg.tz_minutes * 60)) * 1000u;
    draw(nonce, sizeof nonce);
    round_trips = 0;
    t0 = cpuGetTiming();
    r = ok_think_run(&t, &p, key, now_ms, nonce, &pet_copy, tm->tm_hour, tm->tm_min, "player asked from Dev", 0,
                     fetch, NULL);
    ok_wipe(key, sizeof key);
    if (r != OK_OK) {
        line(12, "Setup failed: %s", ok_result_name(r));
        busy = 0;
        return 0;
    }
    line(3, "HTTP %d, %lu ms", t.http_status, (unsigned long)((uint64_t)(cpuGetTiming() - t0) * 1000u / 33513982u));
    line(5, "Request id (0G chat id):");
    line(6, "%.32s", t.chat_id[0] ? t.chat_id : "-");
    line(7, "%.32s", strlen(t.chat_id) > 32 ? t.chat_id + 32 : "");
    line(8, "TEE %.6s..%.4s %s", t.signer, t.signer + 38,
         t.stage == OK_THINK_DECIDED || t.stage == OK_THINK_ANSWER ? "VERIFIED" : "not verified");
    if (t.stage == OK_THINK_DECIDED) {
        line(9, "Does: %s", ok_mind_activity_name(t.decision.activity));
        line(10, "Mood: %s", ok_mind_mood_name(t.decision.mood));
        line(11, "\"%.30s\"", t.decision.line);
        line(12, "Decided by 0G, checked here");
        snprintf(said, sizeof said, "%s", t.decision.line);
        said_new = 1;
    } else {
        line(9, "Stopped at: %s", ok_think_stage_name(t.stage));
        line(12, "Safe stop: the okupa naps");
    }
    save_episode(&p, now_ms);
    busy = 0;
    return 0;
}

void ok_dsi_mind_start(const char *dir, const ok_pet *p)
{
    if (busy) return;
    busy = 1;
    snprintf(dir_path, sizeof dir_path, "%s", dir ? dir : "");
    pet_copy = *p;
    if (cothread_create(thinker, NULL, 64 * 1024, COTHREAD_DETACHED) < 0) {
        line(0, "0G mind: no thread");
        busy = 0;
    }
}
