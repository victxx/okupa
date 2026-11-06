#include "okupa_mind.h"
#include "okupa_json.h"
#include <stdio.h>
#include <string.h>

static const char *activities[OK_ACTIVITIES] = {"eat", "nap", "play", "tidy", "read", "wave", "stare_out_window"};
static const char *moods[OK_FEELINGS] = {"happy", "sleepy", "bored", "hungry", "lonely", "curious"};

static const char SYSTEM[] =
    "You are the mind of Okupa, a tiny person who moved into a Nintendo DSi. Decide what Okupa does next. "
    "Reply with JSON only, no prose: {\"activity\": one of [\"eat\",\"nap\",\"play\",\"tidy\",\"read\",\"wave\","
    "\"stare_out_window\"], \"mood\": one of [\"happy\",\"sleepy\",\"bored\",\"hungry\",\"lonely\",\"curious\"], "
    "\"line\": a short English sentence for the player, at most 40 characters}";

const char *ok_mind_activity_name(int a) { return a >= 0 && a < OK_ACTIVITIES ? activities[a] : "?"; }
const char *ok_mind_mood_name(int m) { return m >= 0 && m < OK_FEELINGS ? moods[m] : "?"; }

/* A JSON string literal; 0 if it does not fit. */
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
        } else if (c < 0x20) {
            *o += (size_t)snprintf(out + *o, cap - *o, "\\u%04x", c);
        } else {
            out[(*o)++] = (char)c;
        }
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

ok_result ok_mind_request(const char *model, const ok_pet *p, int hour, int minute, const char *last_action,
                          uint32_t minutes_away, char *out, size_t cap, size_t *len)
{
    char user[256];
    size_t o = 0;
    if (!model || !p || !out || !len) return OK_ARGUMENT;
    if (last_action && strpbrk(last_action, "\"\\")) return OK_ARGUMENT;
    snprintf(user, sizeof user,
             "{\"time\":\"%02d:%02d\",\"needs\":{\"food\":%u,\"sleep\":%u,\"fun\":%u,\"love\":%u},"
             "\"asleep\":%s,\"last_action\":\"%.60s\",\"minutes_since_visit\":%lu}",
             hour % 24, minute % 60, p->need[OK_NEED_HUNGER], p->need[OK_NEED_ENERGY], p->need[OK_NEED_FUN],
             p->need[OK_NEED_LOVE], p->asleep ? "true" : "false", last_action ? last_action : "none",
             (unsigned long)minutes_away);
    if (!raw(out, cap, &o, "{\"model\":") || !quote(out, cap, &o, model) ||
        !raw(out, cap, &o, ",\"messages\":[{\"role\":\"system\",\"content\":") || !quote(out, cap, &o, SYSTEM) ||
        !raw(out, cap, &o, "},{\"role\":\"user\",\"content\":") || !quote(out, cap, &o, user) ||
        !raw(out, cap, &o, "}],\"temperature\":0.7,\"max_tokens\":200}"))
        return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
}

static int pick(const ok_json *obj, const char *key, const char *const *set, int count)
{
    ok_json v;
    char s[32];
    size_t n;
    int i;
    if (ok_json_member(obj, key, &v) != OK_OK || ok_json_string(&v, s, sizeof s, &n) != OK_OK) return -1;
    for (i = 0; i < count; i++)
        if (!strcmp(s, set[i])) return i;
    return -1;
}

ok_result ok_mind_parse(const char *body, size_t n, ok_mind *d)
{
    static char content[1024];
    char line[256];
    ok_json root, v, choice, msg, obj;
    const char *open, *close;
    size_t len, i, o = 0;
    if (!body || !d) return OK_ARGUMENT;
    if (ok_json_parse(body, n, &root) != OK_OK || ok_json_member(&root, "choices", &v) != OK_OK ||
        ok_json_index(&v, 0, &choice) != OK_OK || ok_json_member(&choice, "message", &msg) != OK_OK ||
        ok_json_member(&msg, "content", &v) != OK_OK || ok_json_string(&v, content, sizeof content, &len) != OK_OK)
        return OK_FORMAT;
    /* Models like to wrap JSON in prose or ``` fences: take the outermost object. */
    open = memchr(content, '{', len);
    for (close = content + len; close > content && close[-1] != '}'; close--) {}
    if (!open || close <= open || ok_json_parse(open, (size_t)(close - open), &obj) != OK_OK || obj.at[0] != '{')
        return OK_FORMAT;
    d->activity = pick(&obj, "activity", activities, OK_ACTIVITIES);
    d->mood = pick(&obj, "mood", moods, OK_FEELINGS);
    if (d->activity < 0 || d->mood < 0) return OK_FORMAT;
    if (ok_json_member(&obj, "line", &v) != OK_OK || ok_json_string(&v, line, sizeof line, &len) != OK_OK)
        return OK_FORMAT;
    for (i = 0; i < len && o < OK_MIND_LINE; i++)
        if ((unsigned char)line[i] >= 0x20 && (unsigned char)line[i] < 0x7f) d->line[o++] = line[i];
    d->line[o] = 0;
    return o ? OK_OK : OK_FORMAT;
}
