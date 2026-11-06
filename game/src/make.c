#include "okupa_make.h"
#include "okupa_act.h"
#include "okupa_json.h"
#include <stdio.h>
#include <string.h>

static const char *colours[OK_MAKE_COLOURS] = {"red",    "orange", "yellow", "lime", "green", "teal",  "cyan", "blue",
                                               "navy",   "purple", "pink",   "brown", "tan",  "white", "grey", "black"};

const char *ok_make_colour_name(int c) { return c >= 0 && c < OK_MAKE_COLOURS ? colours[c] : "?"; }

static const char MAKER[] =
    "You build tiny 3D objects for Okupa, a small monkey who lives in a Nintendo DSi; it leaves them on the floor "
    "of its attic. Use at most 16 axis-aligned boxes on a 16x16x16 grid: x and z go across the floor, y goes up "
    "from the floor at 0. Each box is [x, y, z, w, h, d, colour] with whole numbers, x+w, y+h and z+d at most 16, "
    "and colour one of red, orange, yellow, lime, green, teal, cyan, blue, navy, purple, pink, brown, tan, white, "
    "grey, black. Make it recognisable: big simple parts first, then one or two details. Reply with JSON only: "
    "{\"name\": a short name, \"boxes\": [[x, y, z, w, h, d, colour], ...]}.";

static const char ARTIST[] =
    "You are Okupa, a small monkey who lives in a Nintendo DSi, drawing on its 64x64 black-and-white sketchbook "
    "with a stylus. Draw the subject as a simple, charming line drawing: at most 40 straight lines [x1, y1, x2, y2] "
    "and at most 8 circles [cx, cy, r], whole numbers from 0 to 63, x to the right and y down, filling most of the "
    "page. Reply with JSON only: {\"name\": a short title, \"lines\": [[x1, y1, x2, y2], ...], \"circles\": [[cx, cy, "
    "r], ...]}.";

/* The JSON string literal of s, escaped as Go's encoding/json does (ASCII). */
static int quote(char *out, size_t cap, size_t *o, const char *s)
{
    if (*o + 1 >= cap) return 0;
    out[(*o)++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (*o + 7 >= cap) return 0;
        if (c == '"' || c == '\\') {
            out[(*o)++] = '\\';
            out[(*o)++] = (char)c;
        } else if (c == '<' || c == '>' || c == '&') {
            *o += (size_t)snprintf(out + *o, cap - *o, "\\u%04x", c);
        } else if (c >= 0x20 && c < 0x7f) {
            out[(*o)++] = (char)c;
        } /* anything else is dropped */
    }
    out[(*o)++] = '"';
    return 1;
}

static int raw(char *out, size_t cap, size_t *o, const char *s)
{
    size_t k = strlen(s);
    if (*o + k >= cap) return 0;
    memcpy(out + *o, s, k);
    *o += k;
    return 1;
}

/* Canonical (keys sorted, no spaces), so the bytes the TEE hashes are ours
 * with only the model renamed. */
static ok_result request(const char *model, const char *system, const char *user, int max_tokens, char *out,
                         size_t cap, size_t *len)
{
    char head[128];
    size_t o = 0;
    if (!model || !user || !*user || !out || !len) return OK_ARGUMENT;
    snprintf(head, sizeof head,
             "{\"chat_template_kwargs\":{\"enable_thinking\":false},\"max_tokens\":%d,\"messages\":[{\"content\":",
             max_tokens);
    if (!raw(out, cap, &o, head) || !quote(out, cap, &o, system) || !raw(out, cap, &o, ",\"role\":\"system\"},{\"content\":") ||
        !quote(out, cap, &o, user) || !raw(out, cap, &o, ",\"role\":\"user\"}],\"model\":") || !quote(out, cap, &o, model) ||
        !raw(out, cap, &o, ",\"temperature\":0.7}"))
        return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
}

ok_result ok_make_object_request(const char *model, const char *what, char *out, size_t cap, size_t *len)
{
    char user[96];
    if (!what) return OK_ARGUMENT;
    snprintf(user, sizeof user, "Make %.60s.", what);
    return request(model, MAKER, user, 700, out, cap, len);
}

ok_result ok_make_drawing_request(const char *model, const char *subject, char *out, size_t cap, size_t *len)
{
    char user[96];
    if (!subject) return OK_ARGUMENT;
    snprintf(user, sizeof user, "Draw %.60s.", subject);
    return request(model, ARTIST, user, 900, out, cap, len);
}

/* The JSON object in an answer's content (models wrap it in prose or fences). */
static int answer(const char *body, size_t n, ok_json *obj)
{
    static char content[8192];
    ok_json root, v, choice, msg;
    const char *open, *close;
    size_t len;
    if (ok_json_parse(body, n, &root) != OK_OK || ok_json_member(&root, "choices", &v) != OK_OK ||
        ok_json_index(&v, 0, &choice) != OK_OK || ok_json_member(&choice, "message", &msg) != OK_OK ||
        ok_json_member(&msg, "content", &v) != OK_OK || ok_json_string(&v, content, sizeof content, &len) != OK_OK)
        return 0;
    open = memchr(content, '{', len);
    for (close = content + len; close > content && close[-1] != '}'; close--) {}
    return open && close > open && ok_json_parse(open, (size_t)(close - open), obj) == OK_OK && obj->at[0] == '{';
}

/* A whole number in a JSON span; -1 if it is not one. */
static int whole(const ok_json *v)
{
    size_t i;
    int n = 0;
    if (!v->len || v->at[0] < '0' || v->at[0] > '9') return -1;
    for (i = 0; i < v->len && v->at[i] >= '0' && v->at[i] <= '9' && n < 10000; i++) n = n * 10 + (v->at[i] - '0');
    return n;
}

static void title(const ok_json *obj, char name[21])
{
    ok_json v;
    char s[128];
    size_t n, i, o = 0;
    name[0] = 0;
    if (ok_json_member(obj, "name", &v) != OK_OK || ok_json_string(&v, s, sizeof s, &n) != OK_OK) return;
    for (i = 0; i < n && o < 20; i++)
        if ((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x7f) name[o++] = s[i];
    name[o] = 0;
}

ok_result ok_make_object_parse(const char *body, size_t n, ok_object *o)
{
    ok_json obj, boxes, box, v;
    size_t i;
    int k, c;
    if (!body || !o) return OK_ARGUMENT;
    memset(o, 0, sizeof *o);
    if (!answer(body, n, &obj) || ok_json_member(&obj, "boxes", &boxes) != OK_OK) return OK_FORMAT;
    title(&obj, o->name);
    for (i = 0; o->count < OK_MAKE_BOXES && ok_json_index(&boxes, i, &box) == OK_OK; i++) {
        int b[6], ok = 1;
        char name[16];
        size_t len;
        for (k = 0; k < 6 && ok; k++) ok = ok_json_index(&box, (size_t)k, &v) == OK_OK && (b[k] = whole(&v)) >= 0;
        if (!ok || ok_json_index(&box, 6, &v) != OK_OK || ok_json_string(&v, name, sizeof name, &len) != OK_OK) continue;
        for (c = 0; c < OK_MAKE_COLOURS && strcmp(name, colours[c]); c++) {}
        if (c == OK_MAKE_COLOURS) continue; /* not a colour we know */
        for (k = 0; k < 3; k++) {
            if (b[k] > 15) b[k] = 15;
            if (b[k + 3] < 1) b[k + 3] = 1;
            if (b[k] + b[k + 3] > 16) b[k + 3] = 16 - b[k]; /* inside the grid */
        }
        for (k = 0; k < 6; k++) o->box[o->count][k] = (uint8_t)b[k];
        o->box[o->count][6] = (uint8_t)c;
        o->count++;
    }
    return o->count ? OK_OK : OK_FORMAT;
}

static void dot(uint8_t px[512], int x, int y)
{
    if (x >= 0 && x < 64 && y >= 0 && y < 64) ok_drawing_set(px, (unsigned)x, (unsigned)y, 1);
}

static void line(uint8_t px[512], int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, guard = 0;
    for (;;) {
        dot(px, x0, y0);
        if ((x0 == x1 && y0 == y1) || ++guard > 200) return;
        if (2 * err >= dy) {
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void circle(uint8_t px[512], int cx, int cy, int r)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        dot(px, cx + x, cy + y), dot(px, cx + y, cy + x), dot(px, cx - y, cy + x), dot(px, cx - x, cy + y);
        dot(px, cx - x, cy - y), dot(px, cx - y, cy - x), dot(px, cx + y, cy - x), dot(px, cx + x, cy - y);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

ok_result ok_make_drawing_parse(const char *body, size_t n, uint8_t pixels[512], char name[21])
{
    ok_json obj, list, item, v;
    size_t i;
    int k, strokes = 0;
    if (!body || !pixels || !name) return OK_ARGUMENT;
    memset(pixels, 0, 512);
    if (!answer(body, n, &obj)) return OK_FORMAT;
    title(&obj, name);
    if (ok_json_member(&obj, "lines", &list) == OK_OK)
        for (i = 0; i < 40 && ok_json_index(&list, i, &item) == OK_OK; i++) {
            int c[4], ok = 1;
            for (k = 0; k < 4 && ok; k++) ok = ok_json_index(&item, (size_t)k, &v) == OK_OK && (c[k] = whole(&v)) >= 0 && c[k] < 64;
            if (ok) line(pixels, c[0], c[1], c[2], c[3]), strokes++;
        }
    if (ok_json_member(&obj, "circles", &list) == OK_OK)
        for (i = 0; i < 8 && ok_json_index(&list, i, &item) == OK_OK; i++) {
            int c[3], ok = 1;
            for (k = 0; k < 3 && ok; k++) ok = ok_json_index(&item, (size_t)k, &v) == OK_OK && (c[k] = whole(&v)) >= 0 && c[k] < 64;
            if (ok && c[2] > 0 && c[2] <= 32) circle(pixels, c[0], c[1], c[2]), strokes++;
        }
    return strokes ? OK_OK : OK_FORMAT;
}
