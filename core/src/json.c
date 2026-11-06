#include "okupa_json.h"
#include <string.h>

#define DEPTH 16

static size_t ws(const char *s, size_t n, size_t i)
{
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    return i;
}

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Skips one value starting at s[i]; returns the index after it, or 0 on error. */
static size_t skip(const char *s, size_t n, size_t i, int depth)
{
    if (i >= n || depth > DEPTH) return 0;
    if (s[i] == '"') {
        for (i++; i < n; i++) {
            if ((unsigned char)s[i] < 0x20) return 0;
            if (s[i] == '\\') {
                if (++i >= n) return 0;
                if (s[i] == 'u') {
                    int k;
                    for (k = 1; k <= 4; k++)
                        if (i + k >= n || hexval(s[i + k]) < 0) return 0;
                    i += 4;
                } else if (!strchr("\"\\/bfnrt", s[i])) {
                    return 0;
                }
            } else if (s[i] == '"') {
                return i + 1;
            }
        }
        return 0;
    }
    if (s[i] == '{' || s[i] == '[') {
        char close = s[i] == '{' ? '}' : ']';
        int object = s[i] == '{';
        i = ws(s, n, i + 1);
        if (i < n && s[i] == close) return i + 1;
        for (;;) {
            if (object) {
                if (i >= n || s[i] != '"' || !(i = skip(s, n, i, depth + 1))) return 0;
                i = ws(s, n, i);
                if (i >= n || s[i] != ':') return 0;
                i = ws(s, n, i + 1);
            }
            if (!(i = skip(s, n, i, depth + 1))) return 0;
            i = ws(s, n, i);
            if (i < n && s[i] == ',') {
                i = ws(s, n, i + 1);
                continue;
            }
            return i < n && s[i] == close ? i + 1 : 0;
        }
    }
    if (n - i >= 4 && (!memcmp(s + i, "true", 4) || !memcmp(s + i, "null", 4))) return i + 4;
    if (n - i >= 5 && !memcmp(s + i, "false", 5)) return i + 5;
    if (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')) {
        size_t start = i;
        if (s[i] == '-') i++;
        if (i >= n || s[i] < '0' || s[i] > '9') return 0;
        if (s[i] == '0') i++;
        else
            while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        if (i < n && s[i] == '.') {
            if (++i >= n || s[i] < '0' || s[i] > '9') return 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        }
        if (i < n && (s[i] == 'e' || s[i] == 'E')) {
            i++;
            if (i < n && (s[i] == '+' || s[i] == '-')) i++;
            if (i >= n || s[i] < '0' || s[i] > '9') return 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        }
        return i > start ? i : 0;
    }
    return 0;
}

ok_result ok_json_parse(const char *text, size_t n, ok_json *value)
{
    size_t start, end;
    if (!text || !value) return OK_ARGUMENT;
    start = ws(text, n, 0);
    if (!(end = skip(text, n, start, 0)) || ws(text, n, end) != n) return OK_FORMAT;
    value->at = text + start;
    value->len = end - start;
    return OK_OK;
}

ok_result ok_json_member(const ok_json *object, const char *key, ok_json *value)
{
    const char *s;
    size_t n, i, k, klen;
    if (!object || !key || !value) return OK_ARGUMENT;
    s = object->at;
    n = object->len;
    if (n < 2 || s[0] != '{') return OK_FORMAT;
    klen = strlen(key);
    i = ws(s, n, 1);
    if (s[i] == '}') return OK_RANGE;
    for (;;) {
        size_t kend = skip(s, n, i, 1), vstart, vend;
        if (!kend) return OK_FORMAT;
        k = ws(s, n, kend);
        vstart = ws(s, n, k + 1);
        if (!(vend = skip(s, n, vstart, 1))) return OK_FORMAT;
        if (kend - i - 2 == klen && !memcmp(s + i + 1, key, klen)) {
            value->at = s + vstart;
            value->len = vend - vstart;
            return OK_OK;
        }
        i = ws(s, n, vend);
        if (s[i] != ',') return OK_RANGE;
        i = ws(s, n, i + 1);
    }
}

ok_result ok_json_index(const ok_json *array, size_t index, ok_json *value)
{
    const char *s;
    size_t n, i, k = 0;
    if (!array || !value) return OK_ARGUMENT;
    s = array->at;
    n = array->len;
    if (n < 2 || s[0] != '[') return OK_FORMAT;
    i = ws(s, n, 1);
    if (s[i] == ']') return OK_RANGE;
    for (;;) {
        size_t end = skip(s, n, i, 1);
        if (!end) return OK_FORMAT;
        if (k++ == index) {
            value->at = s + i;
            value->len = end - i;
            return OK_OK;
        }
        i = ws(s, n, end);
        if (s[i] != ',') return OK_RANGE;
        i = ws(s, n, i + 1);
    }
}

static int put(char *out, size_t cap, size_t *o, unsigned c)
{
    uint8_t b[3];
    size_t k, len = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
    if (len == 1) b[0] = (uint8_t)c;
    else if (len == 2) { b[0] = (uint8_t)(0xc0 | c >> 6); b[1] = (uint8_t)(0x80 | (c & 63)); }
    else { b[0] = (uint8_t)(0xe0 | c >> 12); b[1] = (uint8_t)(0x80 | (c >> 6 & 63)); b[2] = (uint8_t)(0x80 | (c & 63)); }
    if (*o + len >= cap) return 0;
    for (k = 0; k < len; k++) out[(*o)++] = (char)b[k];
    return 1;
}

ok_result ok_json_string(const ok_json *value, char *out, size_t cap, size_t *len)
{
    const char *s;
    size_t i, o = 0, n;
    if (!value || !out || !len || cap == 0) return OK_ARGUMENT;
    s = value->at;
    n = value->len;
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') return OK_FORMAT;
    for (i = 1; i < n - 1; i++) {
        unsigned c = (unsigned char)s[i];
        if (c == '\\') {
            char e = s[++i];
            if (e == 'u') {
                c = (unsigned)(hexval(s[i + 1]) << 12 | hexval(s[i + 2]) << 8 | hexval(s[i + 3]) << 4 | hexval(s[i + 4]));
                i += 4;
                if (c >= 0xd800 && c <= 0xdfff) c = '?'; /* no surrogate pairs: an unknown character */
            } else {
                c = e == 'b' ? '\b' : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : (unsigned char)e;
            }
            if (!put(out, cap, &o, c)) return OK_RANGE;
        } else {
            if (o + 1 >= cap) return OK_RANGE;
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
    *len = o;
    return OK_OK;
}
