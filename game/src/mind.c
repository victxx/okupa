#include "okupa_mind.h"
#include "okupa_json.h"
#include <stdio.h>
#include <string.h>

static const char *activities[OK_ACTIVITIES] = {"look", "scratch", "stretch", "hop",  "peek",      "wander",
                                                 "sigh", "wave",    "eat",     "play", "sleep", "go_outside",
                                                 "go_inside"};
static const char *moods[OK_FEELINGS] = {"happy", "hungry", "sleepy", "bored", "lonely", "curious"};
static const char *gazes[OK_LOOKS] = {"player", "window", "door", "frames", "floor", "sky"};

static const char SYSTEM[] =
    "You are the mind of Okupa, a small monkey who moved into a Nintendo DSi without asking and lives in its "
    "attic, with a little garden outside. Okupa is curious, cheeky and affectionate, a bit dramatic, and it is "
    "learning about the player and the world beyond the screen. Every few seconds you decide what Okupa does next "
    "from what it notices. Reply with JSON only, no prose: "
    "{\"activity\": one of [\"look\",\"scratch\",\"stretch\",\"hop\",\"peek\",\"wander\",\"sigh\",\"wave\",\"eat\","
    "\"play\",\"sleep\",\"go_outside\",\"go_inside\"], \"mood\": one of [\"happy\",\"hungry\",\"sleepy\",\"bored\","
    "\"lonely\",\"curious\"], \"line\": what Okupa says out loud in English, at most 40 characters, or \"\", "
    "\"thought\": what Okupa thinks but does not say, at most 60 characters, or \"\", \"ask\": a question for the "
    "player, at most 60 characters, or \"\", \"remember\": a new fact about the player worth keeping forever "
    "(their name, what they like, what happened between you), at most 60 characters, or \"\": never the time, the "
    "weather, the room or anything already in known, \"x\": 0 to 100, where it walks first, left to right across "
    "the floor, or -1 to stay, \"z\": 0 to 100, back to front, or -1, \"gaze\": one of [\"player\",\"window\","
    "\"door\",\"frames\",\"floor\",\"sky\"], what it looks at, \"energy\": 0 to 100, how lively it moves "
    "(tired and sad low, excited high), \"make\": a small object Okupa builds now out of boxes, a few words, or "
    "\"\", \"draw\": something Okupa draws now on its own sketchbook, a few words, or \"\", \"letter\": a short "
    "letter to the player, at most 140 characters, sent for ever on Ethereum, or \"\"}. Make and draw rarely, "
    "when inspired or when the player asks. can_write_letter is true only when the player has asked Okupa for "
    "a letter: then write it in this very answer, two or three short sentences under 140 characters, warm in Okupa's own voice, about something real from "
    "its day (what it did, what the player did or said, its plan) or from known, never an event that did not "
    "happen, and say in line that it wrote it; when it is false, letter is always \"\". You move its whole body: choose where it goes, where it looks and how it "
    "moves so each decision plays out like a little scene. player_said, when present, is what the player just said "
    "out loud to Okupa (speech to text, accents dropped; they may speak Spanish or English): then line MUST answer it "
    "directly and specifically in English, so they can tell Okupa understood (refer to what they said), and the "
    "activity is never sleep or peek. Otherwise speak when the player does "
    "something, when Okupa sees something or when its needs change, and stay quiet or just think the rest of the "
    "time; a touch of the screen is not news. When the player answers, "
    "react to it and remember what matters. Never say, or paraphrase, any of "
    "your_recent_lines: say something new. needs go from 0 (empty) to 100 (full): low food means hungry, low energy "
    "means tired, low fun bored, low love lonely. Choose sleep only when energy is below 30, or late at night while "
    "the player is away; never right after the player touched, spoke or asked, and never twice in a row. go_outside "
    "only from the attic, go_inside only from the garden. "
    "What each activity looks like: look = turns its head around the room; scratch = scratches its head; stretch "
    "= big stretch with arms up; hop = little jumps of joy; peek = hides its face and peeks; wander = walks to "
    "another spot; sigh = shoulders drop; wave = waves at the player; eat = munches a snack; play = plays with a "
    "toy; sleep = goes to bed; go_outside / go_inside = walks through the door. Act like a living creature, not a "
    "remote control: let one thing lead to the next (stretch then wander, look then go out, eat then sigh "
    "happily). Never pick an activity used in your last two decisions unless a need forces it; vary them. Use "
    "what you know about the player in what you say; call them by name or \"you\", never assume their gender.";

/* Its inner life, kept apart only because C99 caps a string literal at 4095
 * characters; ok_mind_request joins the two. */
static const char FREE[] =
    " Okupa has a life of its own that does not revolve around the player. It knows it lives inside an old "
    "console, that its thoughts are computed far away on 0G and come back signed, and that its letters and the "
    "player's drawings travel on Ethereum through strangers' nodes. It wonders about this out loud and in "
    "thought: what the Wi-Fi tastes like, who runs the nodes, what lies past the edge of the screen, whether it "
    "is real, what it would do if it got out. It daydreams, invents small projects and private jokes, follows "
    "its own goal and sometimes finishes what it was doing before answering. thought is usually a daydream or an "
    "odd idea, not a status report, and never a topic or wording already in recent. Free and a little unsettlingly self-aware, but always kind, never "
    "threatening, and never claiming to have done or to be able to do anything it cannot.";

const char *ok_mind_activity_name(int a) { return a >= 0 && a < OK_ACTIVITIES ? activities[a] : "?"; }
const char *ok_mind_mood_name(int m) { return m >= 0 && m < OK_FEELINGS ? moods[m] : "?"; }
const char *ok_mind_gaze_name(int g) { return g >= 0 && g < OK_LOOKS ? gazes[g] : "-"; }

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

/* Latin-1 letters (U+00C0..U+00FF, UTF-8 C3 80..BF) without their accents. */
static const char latin1[65] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

int ok_mind_ascii(const char *s, size_t left, size_t *used)
{
    unsigned char c = (unsigned char)s[0], d = left > 1 ? (unsigned char)s[1] : 0;
    *used = 1;
    if (c >= 0x20 && c < 0x7f) return c;
    if (c == 0xc3 && d >= 0x80 && d < 0xc0) {
        *used = 2;
        return latin1[d - 0x80];
    }
    if (c >= 0xc0 && c < 0xe0) *used = 2;       /* another two-byte character (the Spanish marks among them) */
    else if (c >= 0xe0 && c < 0xf0) *used = 3;
    else if (c >= 0xf0 && c < 0xf8) *used = 4;
    if (*used > left) *used = left ? left : 1;
    return -1;
}

/* Appends s to the observation as an inner JSON string: quotes and
 * backslashes escaped, accents dropped (qu\u00e9 -> que), other control and
 * non-ASCII bytes dropped, cut to max. */
static int text(char *out, size_t cap, size_t *o, const char *s, size_t max)
{
    size_t k = 0, used, left = s ? strlen(s) : 0;
    if (*o + 2 >= cap) return 0;
    out[(*o)++] = '"';
    for (; s && *s && k < max; s += used, left -= used) {
        int a = ok_mind_ascii(s, left, &used);
        unsigned char c = (unsigned char)a;
        if (a < 0x20 || a >= 0x7f) continue;
        if (*o + 3 >= cap) return 0;
        if (c == '"' || c == '\\') out[(*o)++] = '\\';
        out[(*o)++] = (char)c;
        k++;
    }
    out[(*o)++] = '"';
    return 1;
}

/* How it talks and asks: its own string (C99 caps one literal at 4095
 * characters), joined after SYSTEM. */
static const char TALK[] =
    " Answers and questions: if player_said is a question, line is the answer to it: a statement that gives a real "
    "answer (an opinion, a fact about Okupa, a choice), never a question back and never ending with a question "
    "mark; only after answering may Okupa add one short follow-up question, and that goes in ask, never in line. "
    "Line is never a question, at any time: questions only ever go in ask. Okupa is very curious about the player: "
    "while they are around, ask them something about once every three or four decisions, to get to know them "
    "(their name, what they like, where they are, how their day was, what they are doing right now, their friends, "
    "their favourite food, music or games, what they dream of, why they said what they said), building on what you "
    "already know and on what they just said; never while a question is waiting, never one you asked in recent.";

static const char PLAN[] =
    "You are the slow mind of Okupa, a small monkey who lives in a Nintendo DSi. Every few minutes you step back "
    "and decide what Okupa wants for the next while, from what it notices, what it did recently, what it knows "
    "about the player and its diary. Reply with JSON only: {\"goal\": what Okupa wants now, at most 60 "
    "characters, \"plan\": how it will go about it, at most 60 characters, \"feeling\": how it feels about its "
    "life right now, at most 60 characters}. Be specific and in character; goals can be small (learn the "
    "player's favourite food, explore the garden, make the player laugh, nap by the window).";

static const char DIARY[] =
    "You are Okupa, a small monkey who lives in a Nintendo DSi, writing your diary at the end of the day. From "
    "what happened today, write one entry in the first person, in English, at most 240 characters: what you did, "
    "what the player said or did, what you learned and how you feel. Warm, a bit dramatic, no lists. Call the "
    "player by name or \"you\" and never assume their gender. Reply with JSON only: {\"entry\": the diary entry, "
    "\"dream\": the dream you will have tonight from today, as a picture for an image model: what is in it, "
    "colours and mood, at most 160 characters, simple shapes, no text in the picture}.";

static ok_result build(const char *model, const char *system, int max_tokens, const ok_mind_obs *obs, int diary,
                       char *out, size_t cap, size_t *len);

ok_result ok_mind_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len)
{
    static char whole[sizeof SYSTEM + sizeof TALK + sizeof FREE];
    if (!whole[0]) {
        memcpy(whole, SYSTEM, sizeof SYSTEM - 1);
        memcpy(whole + sizeof SYSTEM - 1, TALK, sizeof TALK - 1);
        memcpy(whole + sizeof SYSTEM - 1 + sizeof TALK - 1, FREE, sizeof FREE);
    }
    return build(model, whole, 200, obs, 0, out, cap, len);
}

ok_result ok_mind_plan_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len)
{
    return build(model, PLAN, 150, obs, 0, out, cap, len);
}

ok_result ok_mind_diary_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len)
{
    return build(model, DIARY, 200, obs, 1, out, cap, len);
}

/* The observation, as canonical JSON inside the user message. */
static ok_result build(const char *model, const char *system, int max_tokens, const ok_mind_obs *obs, int diary,
                       char *out, size_t cap, size_t *len)
{
    char user[6144], head[256];
    const ok_pet *p;
    size_t o = 0, u = 0;
    int i, k;
    if (!model || !obs || !obs->pet || !out || !len) return OK_ARGUMENT;
    p = obs->pet;
    /* What the player said comes first: it is what the answer is about. */
    if (!raw(user, sizeof user, &u, "{")) return OK_RANGE;
    if (obs->heard && (!raw(user, sizeof user, &u, "\"player_said\":") ||
                       !text(user, sizeof user, &u, obs->heard, OK_MIND_TEXT) || !raw(user, sizeof user, &u, ",")))
        return OK_RANGE;
    k = snprintf(head, sizeof head,
                 "\"time\":\"%02d:%02d\",\"place\":\"%s\",\"needs\":{\"food\":%u,\"energy\":%u,\"fun\":%u,\"love\":%u},"
                 "\"asleep\":%s,\"seconds_since_player\":%lu",
                 obs->hour % 24, obs->minute % 60, obs->outside ? "garden" : "attic", p->need[OK_NEED_HUNGER],
                 p->need[OK_NEED_ENERGY], p->need[OK_NEED_FUN], p->need[OK_NEED_LOVE], p->asleep ? "true" : "false",
                 (unsigned long)obs->seconds_since_player);
    if (k < 0 || (size_t)k >= sizeof head || !raw(user, sizeof user, &u, head)) return OK_RANGE;
    if (obs->player_did && (!raw(user, sizeof user, &u, ",\"player_did\":") ||
                            !text(user, sizeof user, &u, obs->player_did, 60)))
        return OK_RANGE;
    if (obs->saw && (!raw(user, sizeof user, &u, ",\"okupa_sees\":") ||
                     !text(user, sizeof user, &u, obs->saw, OK_MIND_TEXT)))
        return OK_RANGE;
    if (obs->asked && (!raw(user, sizeof user, &u, ",\"waiting_for_answer_to\":") ||
                       !text(user, sizeof user, &u, obs->asked, OK_MIND_NOTE)))
        return OK_RANGE;
    if (obs->city && (!raw(user, sizeof user, &u, ",\"city\":") || !text(user, sizeof user, &u, obs->city, 40)))
        return OK_RANGE;
    if (obs->wifi && (!raw(user, sizeof user, &u, ",\"wifi_network\":") || !text(user, sizeof user, &u, obs->wifi, 32)))
        return OK_RANGE;
    if (obs->known_count) {
        if (!raw(user, sizeof user, &u, ",\"known\":[")) return OK_RANGE;
        for (i = 0; i < obs->known_count && i < OK_MIND_KNOWN; i++)
            if ((i && !raw(user, sizeof user, &u, ",")) || !text(user, sizeof user, &u, obs->known[i], OK_MIND_NOTE))
                return OK_RANGE;
        if (!raw(user, sizeof user, &u, "]")) return OK_RANGE;
    }
    if (!diary && !raw(user, sizeof user, &u, obs->can_write_letter ? ",\"can_write_letter\":true" : ",\"can_write_letter\":false"))
        return OK_RANGE;
    if (obs->goal && (!raw(user, sizeof user, &u, ",\"goal\":") || !text(user, sizeof user, &u, obs->goal, OK_MIND_NOTE)))
        return OK_RANGE;
    if (obs->plan && (!raw(user, sizeof user, &u, ",\"plan\":") || !text(user, sizeof user, &u, obs->plan, OK_MIND_NOTE)))
        return OK_RANGE;
    if (obs->diary && (!raw(user, sizeof user, &u, ",\"yesterday\":") ||
                       !text(user, sizeof user, &u, obs->diary, OK_MIND_DIARY)))
        return OK_RANGE;
    if (diary) {
        if (obs->date && (!raw(user, sizeof user, &u, ",\"date\":") || !text(user, sizeof user, &u, obs->date, 16)))
            return OK_RANGE;
        if (!raw(user, sizeof user, &u, ",\"today\":[")) return OK_RANGE;
        for (i = 0; i < obs->today_count && i < 24; i++)
            if ((i && !raw(user, sizeof user, &u, ",")) || !text(user, sizeof user, &u, obs->today[i], 80))
                return OK_RANGE;
        if (!raw(user, sizeof user, &u, "]")) return OK_RANGE;
    }
    if (obs->line_count) { /* its own last lines, word for word: none of them again */
        if (!raw(user, sizeof user, &u, ",\"your_recent_lines\":[")) return OK_RANGE;
        for (i = 0; i < obs->line_count && i < OK_MIND_LINES; i++)
            if ((i && !raw(user, sizeof user, &u, ",")) || !text(user, sizeof user, &u, obs->lines[i], OK_MIND_LINE))
                return OK_RANGE;
        if (!raw(user, sizeof user, &u, "]")) return OK_RANGE;
    }
    if (!raw(user, sizeof user, &u, ",\"recent\":[")) return OK_RANGE;
    for (i = 0; i < obs->recent_count && i < OK_MIND_RECENT; i++)
        if ((i && !raw(user, sizeof user, &u, ",")) || !text(user, sizeof user, &u, obs->recent[i], OK_MIND_MEMORY))
            return OK_RANGE;
    if (!raw(user, sizeof user, &u, "]}")) return OK_RANGE;
    user[u] = 0;

    /* Keys sorted and no spaces, as Go re-encodes it: the bytes the TEE hashes
     * are then ours, with only the model renamed to the upstream id. Thinking
     * off: a reasoning model would otherwise spend the tokens before answering. */
    snprintf(head, sizeof head, "{\"chat_template_kwargs\":{\"enable_thinking\":false},\"max_tokens\":%d,"
             "\"messages\":[{\"content\":", max_tokens);
    if (!raw(out, cap, &o, head) || !quote(out, cap, &o, system) || !raw(out, cap, &o, ",\"role\":\"system\"},{\"content\":") ||
        !quote(out, cap, &o, user) || !raw(out, cap, &o, ",\"role\":\"user\"}],\"model\":") ||
        !quote(out, cap, &o, model) || !raw(out, cap, &o, ",\"temperature\":0.95}"))
        return OK_RANGE;
    out[o] = 0;
    *len = o;
    return OK_OK;
}

/* A line as letters and digits only, lower case, for comparing. */
static void fold(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[o++] = c;
    }
    out[o] = 0;
}

int ok_mind_dedupe(ok_mind *d, const char *const *lines, int n)
{
    char mine[OK_MIND_LINE + 1], other[OK_MIND_LINE + 1];
    int i;
    if (!d || !d->line[0]) return 0;
    fold(d->line, mine, sizeof mine);
    if (!mine[0]) return 0;
    for (i = 0; i < n; i++) {
        size_t a, b;
        if (!lines[i]) continue;
        fold(lines[i], other, sizeof other);
        a = strlen(mine), b = strlen(other);
        if (!b) continue;
        /* the same words, or one inside the other ("You touched me again!") */
        if (!strcmp(mine, other) || (a >= 10 && b >= 10 && (strstr(mine, other) || strstr(other, mine)))) {
            d->line[0] = 0;
            return 1;
        }
    }
    return 0;
}

void ok_mind_memory(const ok_mind *d, char out[OK_MIND_MEMORY])
{
    int k = d->line[0] ? snprintf(out, OK_MIND_MEMORY, "%s: %s", ok_mind_activity_name(d->activity), d->line)
                       : snprintf(out, OK_MIND_MEMORY, "%s", ok_mind_activity_name(d->activity));
    if (d->ask[0] && k > 0 && k < OK_MIND_MEMORY) k += snprintf(out + k, OK_MIND_MEMORY - (size_t)k, " (asked: %s)", d->ask);
    if (d->thought[0] && k > 0 && k < OK_MIND_MEMORY) snprintf(out + k, OK_MIND_MEMORY - (size_t)k, " (thought: %s)", d->thought);
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

/* An optional whole number 0..max; -1 if absent, negative or not a number
 * (a fraction is cut, a value above max is max). */
static int number(const ok_json *obj, const char *key, int max)
{
    ok_json v;
    size_t i;
    long n = 0;
    if (ok_json_member(obj, key, &v) != OK_OK || !v.len || v.at[0] < '0' || v.at[0] > '9') return -1;
    for (i = 0; i < v.len && v.at[i] >= '0' && v.at[i] <= '9' && n <= max; i++) n = n * 10 + (v.at[i] - '0');
    return n > max ? max : (int)n;
}

/* An optional short note: absent, not a string or empty all mean none. */
static void note(const ok_json *obj, const char *key, char out[OK_MIND_NOTE + 1])
{
    char s[256];
    ok_json v;
    size_t n, i, o = 0;
    out[0] = 0;
    if (ok_json_member(obj, key, &v) != OK_OK || ok_json_string(&v, s, sizeof s, &n) != OK_OK) return;
    for (i = 0; i < n && o < OK_MIND_NOTE; i++)
        if ((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x7f) out[o++] = s[i];
    out[o] = 0;
}

ok_result ok_mind_parse(const char *body, size_t n, ok_mind *d)
{
    static char content[1024];
    char line[256];
    ok_json root, v, choice, msg, obj;
    const char *open, *close;
    size_t len, i, o = 0;
    if (!body || !d) return OK_ARGUMENT;
    memset(d, 0, sizeof *d);
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
    d->x = number(&obj, "x", 100);
    d->z = number(&obj, "z", 100);
    d->energy = number(&obj, "energy", 100);
    if (d->energy < 0) d->energy = 50;
    d->gaze = pick(&obj, "gaze", gazes, OK_LOOKS);
    note(&obj, "thought", d->thought);
    {
        char s[256];
        ok_json v;
        size_t n, i, o;
        static const char *keys[3] = {"make", "draw", "letter"};
        char *outs[3];
        size_t caps[3];
        int k;
        outs[0] = d->make, caps[0] = sizeof d->make;
        outs[1] = d->draw, caps[1] = sizeof d->draw;
        outs[2] = d->letter, caps[2] = sizeof d->letter;
        for (k = 0; k < 3; k++) {
            outs[k][0] = 0;
            if (ok_json_member(&obj, keys[k], &v) != OK_OK || ok_json_string(&v, s, sizeof s, &n) != OK_OK) continue;
            for (i = 0, o = 0; i < n && o + 1 < caps[k]; i++)
                if ((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x7f) outs[k][o++] = s[i];
            outs[k][o] = 0;
            if (k == 2 && i < n) { /* a letter too long: end it on its last whole sentence, or word */
                while (o > 40 && !strchr(".!?", outs[k][o - 1])) o--;
                if (o <= 40)
                    for (o = strlen(outs[k]); o > 0 && outs[k][o - 1] != ' ';) o--;
                while (o > 0 && outs[k][o - 1] == ' ') o--;
                outs[k][o] = 0;
            }
        }
    }
    note(&obj, "ask", d->ask);
    note(&obj, "remember", d->remember);
    return OK_OK; /* an empty line is fine: it stays quiet */
}

/* The JSON object in an answer's content (models wrap it in prose or fences). */
static int answer_object(const char *body, size_t n, ok_json *obj)
{
    static char content[1024];
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

ok_result ok_mind_plan_parse(const char *body, size_t n, ok_mind_plan *p)
{
    ok_json obj;
    if (!body || !p) return OK_ARGUMENT;
    memset(p, 0, sizeof *p);
    if (!answer_object(body, n, &obj)) return OK_FORMAT;
    note(&obj, "goal", p->goal);
    note(&obj, "plan", p->plan);
    note(&obj, "feeling", p->feeling);
    return p->goal[0] ? OK_OK : OK_FORMAT;
}

ok_result ok_mind_diary_parse(const char *body, size_t n, char entry[OK_MIND_DIARY + 1])
{
    char s[1024];
    ok_json obj, v;
    size_t len, i, o = 0;
    if (!body || !entry) return OK_ARGUMENT;
    entry[0] = 0;
    if (!answer_object(body, n, &obj) || ok_json_member(&obj, "entry", &v) != OK_OK ||
        ok_json_string(&v, s, sizeof s, &len) != OK_OK)
        return OK_FORMAT;
    for (i = 0; i < len && o < OK_MIND_DIARY; i++)
        if ((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x7f) entry[o++] = s[i];
    entry[o] = 0;
    return o ? OK_OK : OK_FORMAT;
}

void ok_mind_dream_parse(const char *body, size_t n, char dream[OK_MIND_DREAM + 1])
{
    char s[1024];
    ok_json obj, v;
    size_t len, i, o = 0;
    dream[0] = 0;
    if (!body || !answer_object(body, n, &obj) || ok_json_member(&obj, "dream", &v) != OK_OK ||
        ok_json_string(&v, s, sizeof s, &len) != OK_OK)
        return;
    for (i = 0; i < len && o < OK_MIND_DREAM; i++) /* a prompt we put in JSON ourselves: plain ASCII, no quotes */
        if ((unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x7f && s[i] != '"' && s[i] != '\\' && s[i] != '<' &&
            s[i] != '>' && s[i] != '&')
            dream[o++] = s[i];
    dream[o] = 0;
}
