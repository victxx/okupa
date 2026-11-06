#include "okupa_mind.h"
#include "okupa_json.h"
#include <stdio.h>
#include <string.h>

static const char *activities[OK_ACTIVITIES] = {"look", "scratch", "stretch", "hop",  "peek",      "wander",
                                                 "sigh", "wave",    "eat",     "play", "sleep", "go_outside",
                                                 "go_inside"};
static const char *moods[OK_FEELINGS] = {"happy", "hungry", "sleepy", "bored", "lonely", "curious"};

static const char SYSTEM[] =
    "You are the mind of Okupa, a small monkey who moved into a Nintendo DSi without asking and lives in its "
    "attic, with a little garden outside. Okupa is curious, cheeky and affectionate, and a bit dramatic. Every few "
    "seconds you decide what Okupa does next from what it notices. Reply with JSON only, no prose: "
    "{\"activity\": one of [\"look\",\"scratch\",\"stretch\",\"hop\",\"peek\",\"wander\",\"sigh\",\"wave\",\"eat\","
    "\"play\",\"sleep\",\"go_outside\",\"go_inside\"], \"mood\": one of [\"happy\",\"hungry\",\"sleepy\",\"bored\","
    "\"lonely\",\"curious\"], \"line\": what Okupa says out loud in English, at most 40 characters, or \"\" to stay "
    "quiet}. Stay quiet most of the time. Speak when the player does or says something, when Okupa sees something "
    "or when its needs change. Never repeat a recent line. Keep to its needs: hungry means eat, tired means sleep. "
    "go_outside only from the attic, go_inside only from the garden.";

const char *ok_mind_activity_name(int a) { return a >= 0 && a < OK_ACTIVITIES ? activities[a] : "?"; }
const char *ok_mind_mood_name(int m) { return m >= 0 && m < OK_FEELINGS ? moods[m] : "?"; }

/* A JSON string literal escaped exactly as Go's encoding/json does it (the 0G
 * broker re-encodes the request with it before the TEE hashes it); 0 if it
 * does not fit or holds a control byte Go would spell differently. */
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
        } else if (c == '\n' || c == '\r' || c == '\t') {
            out[(*o)++] = '\\';
            out[(*o)++] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
        } else if (c == '<' || c == '>' || c == '&') {
            *o += (size_t)snprintf(out + *o, cap - *o, "\\u%04x", c);
        } else if (c < 0x20 || c >= 0x7f) {
            return 0; /* ASCII only: Go's spelling of the rest is not worth matching */
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

/* Appends s to the observation as an inner JSON string: quotes and
 * backslashes escaped, control and non-ASCII bytes dropped, cut to max. */
static int text(char *out, size_t cap, size_t *o, const char *s, size_t max)
{
    size_t k = 0;
    if (*o + 2 >= cap) return 0;
    out[(*o)++] = '"';
    for (; s && *s && k < max; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c >= 0x7f) continue;
        if (*o + 3 >= cap) return 0;
        if (c == '"' || c == '\\') out[(*o)++] = '\\';
        out[(*o)++] = (char)c;
        k++;
    }
    out[(*o)++] = '"';
    return 1;
}

ok_result ok_mind_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len)
{
    char user[1536], head[256];
    const ok_pet *p;
    size_t o = 0, u = 0;
    int i, k;
    if (!model || !obs || !obs->pet || !out || !len) return OK_ARGUMENT;
    p = obs->pet;
    k = snprintf(head, sizeof head,
                 "{\"time\":\"%02d:%02d\",\"place\":\"%s\",\"needs\":{\"food\":%u,\"sleep\":%u,\"fun\":%u,\"love\":%u},"
                 "\"asleep\":%s,\"seconds_since_player\":%lu",
                 obs->hour % 24, obs->minute % 60, obs->outside ? "garden" : "attic", p->need[OK_NEED_HUNGER],
                 p->need[OK_NEED_ENERGY], p->need[OK_NEED_FUN], p->need[OK_NEED_LOVE], p->asleep ? "true" : "false",
                 (unsigned long)obs->seconds_since_player);
    if (k < 0 || (size_t)k >= sizeof head || !raw(user, sizeof user, &u, head)) return OK_RANGE;
    if (obs->player_did && (!raw(user, sizeof user, &u, ",\"player_did\":") ||
                            !text(user, sizeof user, &u, obs->player_did, 60)))
        return OK_RANGE;
    if (obs->heard && (!raw(user, sizeof user, &u, ",\"player_said\":") ||
                       !text(user, sizeof user, &u, obs->heard, OK_MIND_TEXT)))
        return OK_RANGE;
    if (obs->saw && (!raw(user, sizeof user, &u, ",\"okupa_sees\":") ||
                     !text(user, sizeof user, &u, obs->saw, OK_MIND_TEXT)))
        return OK_RANGE;
    if (!raw(user, sizeof user, &u, ",\"recent\":[")) return OK_RANGE;
    for (i = 0; i < obs->recent_count && i < OK_MIND_RECENT; i++)
        if ((i && !raw(user, sizeof user, &u, ",")) || !text(user, sizeof user, &u, obs->recent[i], 64)) return OK_RANGE;
    if (!raw(user, sizeof user, &u, "]}")) return OK_RANGE;
    user[u] = 0;

    /* Keys sorted and no spaces, as Go re-encodes it: the bytes the TEE hashes
     * are then ours, with only the model renamed to the upstream id. Thinking
     * off: a reasoning model would otherwise spend the tokens before answering. */
    if (!raw(out, cap, &o, "{\"chat_template_kwargs\":{\"enable_thinking\":false},\"max_tokens\":120,"
                           "\"messages\":[{\"content\":") ||
        !quote(out, cap, &o, SYSTEM) || !raw(out, cap, &o, ",\"role\":\"system\"},{\"content\":") ||
        !quote(out, cap, &o, user) || !raw(out, cap, &o, ",\"role\":\"user\"}],\"model\":") ||
        !quote(out, cap, &o, model) || !raw(out, cap, &o, ",\"temperature\":0.8}"))
        return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
}

void ok_mind_memory(const ok_mind *d, char out[OK_MIND_LINE + 24])
{
    if (d->line[0])
        snprintf(out, OK_MIND_LINE + 24, "%s: %s", ok_mind_activity_name(d->activity), d->line);
    else
        snprintf(out, OK_MIND_LINE + 24, "%s", ok_mind_activity_name(d->activity));
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
    return OK_OK; /* an empty line is fine: it stays quiet */
}
