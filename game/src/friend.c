#include "okupa_friend.h"
#include <stdio.h>
#include <string.h>

void ok_friend_start(ok_friend *f, uint16_t code)
{
    memset(f, 0, sizeof *f);
    f->mode = OK_FRIEND_WAITING;
    f->code = (uint16_t)(1000 + code % 9000);
    snprintf(f->name, sizeof f->name, "Friend");
}

void ok_friend_stop(ok_friend *f) { f->mode = OK_FRIEND_OFF; }

void ok_friend_round(ok_friend *f, int mode)
{
    if (f->mode == OK_FRIEND_OFF) return;
    f->mode = mode == OK_FRIEND_THEY_DRAW ? OK_FRIEND_THEY_DRAW : OK_FRIEND_DS_DRAWS;
    f->round++;
    memset(f->ds_drawing, 0, sizeof f->ds_drawing);
    memset(f->their_drawing, 0, sizeof f->their_drawing);
    f->guess_count = 0;
    f->ds_guess[0] = 0;
    f->ds_guess_state = OK_GUESS_NONE;
}

void ok_friend_right(ok_friend *f, int k)
{
    if (f->mode != OK_FRIEND_DS_DRAWS || k < 0 || k >= f->guess_count) return;
    f->ds_points++;
    f->friend_points++;
    ok_friend_round(f, OK_FRIEND_THEY_DRAW); /* their turn to draw */
}

/* Printable ASCII only, trimmed, cut to cap - 1. */
static void clean(char *out, size_t cap, const char *in, size_t n)
{
    size_t o = 0, i;
    for (i = 0; i < n && o + 1 < cap; i++) {
        char c = in[i];
        if (c == '+') c = ' '; /* form encoding */
        if (c >= 0x20 && c < 0x7f && !(o == 0 && c == ' ')) out[o++] = c;
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

void ok_friend_guess(ok_friend *f, const char *text)
{
    if (f->mode != OK_FRIEND_THEY_DRAW || !text) return;
    clean(f->ds_guess, sizeof f->ds_guess, text, strlen(text));
    f->ds_guess_state = f->ds_guess[0] ? OK_GUESS_ASKED : OK_GUESS_NONE;
}

int ok_friend_here(const ok_friend *f, uint32_t now)
{
    return f->mode != OK_FRIEND_OFF && f->seen_at && now - f->seen_at < 10;
}

/* ---- HTTP ------------------------------------------------------------------ */

static size_t reply(char *out, size_t cap, int status, const char *type, const char *body, size_t len)
{
    const char *why = status == 200 ? "OK" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : "Bad Request";
    int h = snprintf(out, cap,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     status, why, type, (unsigned long)len);
    if (h < 0 || (size_t)h + len > cap) return 0;
    memcpy(out + h, body, len);
    return (size_t)h + len;
}

static size_t text(char *out, size_t cap, int status, const char *body)
{
    return reply(out, cap, status, "text/plain", body, strlen(body));
}

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* A drawing sent as 1024 hex characters. */
static int read_drawing(const char *body, size_t n, uint8_t out[512])
{
    size_t i;
    while (n && (body[n - 1] == '\n' || body[n - 1] == '\r')) n--;
    if (n != 1024) return 0;
    for (i = 0; i < 512; i++) {
        int a = hexval(body[2 * i]), b = hexval(body[2 * i + 1]);
        if (a < 0 || b < 0) return 0;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return 1;
}

static void put_hex(char *out, const uint8_t *b, size_t n)
{
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) out[2 * i] = d[b[i] >> 4], out[2 * i + 1] = d[b[i] & 15];
    out[2 * n] = 0;
}

/* A JSON string with " and \ escaped (the texts are printable ASCII). */
static int put_str(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    if (cap < 3) return 0;
    out[o++] = '"';
    for (; *s; s++) {
        if (o + 3 >= cap) return 0;
        if (*s == '"' || *s == '\\') out[o++] = '\\';
        out[o++] = *s;
    }
    out[o++] = '"';
    out[o] = 0;
    return (int)o;
}

/* The query value of key (?key=value&...), copied; "" if absent. */
static void query(const char *target, size_t n, const char *key, char *out, size_t cap)
{
    size_t k = strlen(key), i;
    out[0] = 0;
    for (i = 0; i + k + 1 < n; i++)
        if ((target[i] == '?' || target[i] == '&') && !strncmp(target + i + 1, key, k) && target[i + 1 + k] == '=') {
            size_t j = i + 2 + k, o = 0;
            while (j < n && target[j] != '&' && o + 1 < cap) out[o++] = target[j++];
            out[o] = 0;
            return;
        }
}

static size_t state(ok_friend *f, char *out, size_t cap)
{
    static char body[2600];
    char hexd[1025], s[OK_FRIEND_TEXT * 2 + 4];
    int o, k;
    o = snprintf(body, sizeof body, "{\"mode\":%d,\"round\":%lu,\"ds\":%d,\"you\":%d,\"name\":", f->mode,
                 (unsigned long)f->round, f->ds_points, f->friend_points);
    o += put_str(body + o, sizeof body - (size_t)o, f->name);
    if (f->mode == OK_FRIEND_DS_DRAWS) {
        put_hex(hexd, f->ds_drawing, 512);
        o += snprintf(body + o, sizeof body - (size_t)o, ",\"drawing\":\"%s\",\"guesses\":[", hexd);
        for (k = 0; k < f->guess_count; k++) {
            if (put_str(s, sizeof s, f->guesses[k]))
                o += snprintf(body + o, sizeof body - (size_t)o, "%s%s", k ? "," : "", s);
        }
        o += snprintf(body + o, sizeof body - (size_t)o, "]");
    } else if (f->mode == OK_FRIEND_THEY_DRAW) {
        put_str(s, sizeof s, f->ds_guess);
        o += snprintf(body + o, sizeof body - (size_t)o, ",\"guess\":%s,\"judged\":%d", s, f->ds_guess_state);
    }
    o += snprintf(body + o, sizeof body - (size_t)o, "}");
    return reply(out, cap, 200, "application/json", body, (size_t)o);
}

size_t ok_friend_http(ok_friend *f, const char *req, size_t n, uint32_t now, char *out, size_t cap)
{
    const char *end = NULL, *sp1, *sp2, *body;
    char code[8], value[8];
    size_t i, body_len = 0, have, target_len;
    int post;
    for (i = 0; i + 3 < n; i++)
        if (!memcmp(req + i, "\r\n\r\n", 4)) {
            end = req + i + 4;
            break;
        }
    if (!end) return n > 4096 ? text(out, cap, 400, "too long") : 0;
    post = n >= 5 && !memcmp(req, "POST ", 5);
    if (!post && (n < 4 || memcmp(req, "GET ", 4))) return text(out, cap, 400, "no");
    sp1 = memchr(req, ' ', (size_t)(end - req));
    sp2 = sp1 ? memchr(sp1 + 1, ' ', (size_t)(end - sp1 - 1)) : NULL;
    if (!sp1 || !sp2) return text(out, cap, 400, "no");
    target_len = (size_t)(sp2 - sp1 - 1);
    for (i = 0; i + 16 < (size_t)(end - req); i++) /* Content-Length, any case */
        if ((req[i] == '\n') && (!strncmp(req + i + 1, "Content-Length:", 15) || !strncmp(req + i + 1, "content-length:", 15))) {
            const char *p = req + i + 16;
            while (*p == ' ') p++;
            while (*p >= '0' && *p <= '9') body_len = body_len * 10 + (size_t)(*p++ - '0');
            break;
        }
    if (body_len > 2048) return text(out, cap, 400, "too long");
    have = n - (size_t)(end - req);
    if (have < body_len) return 0; /* the rest of the body is still coming */
    body = end;
    if (!post && target_len == 1 && sp1[1] == '/') { /* the page */
        size_t o, k, h;
        const char *const *part;
        for (k = 0, part = ok_friend_page; *part; part++) k += strlen(*part);
        h = (size_t)snprintf(out, cap,
                             "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %lu\r\n"
                             "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                             (unsigned long)k);
        if (h + k > cap) return text(out, cap, 400, "page too big");
        for (o = h, part = ok_friend_page; *part; part++) {
            memcpy(out + o, *part, strlen(*part));
            o += strlen(*part);
        }
        return o;
    }
    if (target_len < 2 || sp1[1] != '/') return text(out, cap, 404, "no such thing");
    if (f->mode == OK_FRIEND_OFF) return text(out, cap, 403, "the okupa is not playing");
    query(sp1 + 1, target_len, "c", code, sizeof code);
    snprintf(value, sizeof value, "%u", (unsigned)f->code);
    if (strcmp(code, value)) return text(out, cap, 403, "wrong code");
    f->seen_at = now ? now : 1;
    switch (sp1[2]) {
    case 's': /* the state, polled */
        return state(f, out, cap);
    case 'n': /* their name */
        if (!post) break;
        clean(f->name, sizeof f->name, body, body_len);
        if (!f->name[0]) snprintf(f->name, sizeof f->name, "Friend");
        if (f->mode == OK_FRIEND_WAITING) ok_friend_round(f, OK_FRIEND_DS_DRAWS);
        return text(out, cap, 200, "hi");
    case 'g': /* a guess at the DS drawing */
        if (!post || f->mode != OK_FRIEND_DS_DRAWS) break;
        {
            char g[OK_FRIEND_TEXT + 1];
            clean(g, sizeof g, body, body_len);
            if (!g[0]) break;
            memmove(f->guesses[1], f->guesses[0], sizeof f->guesses[0] * (OK_FRIEND_GUESSES - 1));
            snprintf(f->guesses[0], sizeof f->guesses[0], "%s", g);
            if (f->guess_count < OK_FRIEND_GUESSES) f->guess_count++;
            f->guesses_new = 1;
        }
        return text(out, cap, 200, "guessed");
    case 'd': /* their drawing, as it grows */
        if (!post || f->mode != OK_FRIEND_THEY_DRAW || !read_drawing(body, body_len, f->their_drawing)) break;
        f->their_new = 1;
        return text(out, cap, 200, "drawn");
    case 'j': /* their verdict on the DS guess */
        if (!post || f->mode != OK_FRIEND_THEY_DRAW || f->ds_guess_state != OK_GUESS_ASKED) break;
        query(sp1 + 1, target_len, "ok", value, sizeof value);
        f->ds_guess_state = value[0] == '1' ? OK_GUESS_RIGHT : OK_GUESS_WRONG;
        if (f->ds_guess_state == OK_GUESS_RIGHT) f->ds_points++, f->friend_points++;
        f->judged_new = 1;
        return text(out, cap, 200, "judged");
    case 'h': /* a drawing given to the okupa */
        if (!post || !read_drawing(body, body_len, f->gift)) break;
        f->gift_new = 1;
        return text(out, cap, 200, "given");
    default:
        return text(out, cap, 404, "no such thing");
    }
    return text(out, cap, 400, "not now");
}
