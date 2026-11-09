#ifndef OKUPA_MIND_H
#define OKUPA_MIND_H

#include "okupa.h"
#include "okupa_pet.h"

/* The okupa's mind on 0G: what it notices goes out as an OpenAI-style chat
 * request every few seconds; what comes back must pick from closed sets.
 * Anything else is refused, and the caller falls back to the safe stop: the
 * okupa sleeps and nothing is made up.
 *
 * The activities are the okupa's own moves (the 3D body plays them), plus
 * going out to the garden and back in. */
enum {
    OK_MIND_LOOK, OK_MIND_SCRATCH, OK_MIND_STRETCH, OK_MIND_HOP, OK_MIND_PEEK, OK_MIND_WANDER, OK_MIND_SIGH,
    OK_MIND_WAVE, OK_MIND_EAT, OK_MIND_PLAY, OK_MIND_SLEEP, OK_MIND_GO_OUTSIDE, OK_MIND_GO_INSIDE, OK_ACTIVITIES
};
/* Where it looks while it does it (its body follows its gaze). */
enum { OK_LOOK_PLAYER, OK_LOOK_WINDOW, OK_LOOK_DOOR, OK_LOOK_FRAMES, OK_LOOK_FLOOR, OK_LOOK_SKY, OK_LOOKS };
enum { OK_FEEL_HAPPY, OK_FEEL_HUNGRY, OK_FEEL_SLEEPY, OK_FEEL_BORED, OK_FEEL_LONELY, OK_FEEL_CURIOUS, OK_FEELINGS };
#define OK_MIND_LINE 40
#define OK_MIND_RECENT 6 /* its own last decisions, fed back as memory */
#define OK_MIND_MEMORY 128 /* one of them: "activity: line (asked: ...)" */
#define OK_MIND_TEXT 160 /* what it heard or saw, cut to this */
#define OK_MIND_NOTE 60  /* a thought, a question, a fact to keep */
#define OK_MIND_KNOWN 16 /* facts it keeps about its life and the player */
#define OK_MIND_LINES 12 /* its own last lines, word for word, so it says something new */

typedef struct {
    int activity, mood;
    char line[OK_MIND_LINE + 1];     /* said out loud; printable ASCII; empty = it says nothing */
    char thought[OK_MIND_NOTE + 1];  /* thought, not said (a thought bubble); may be empty */
    char ask[OK_MIND_NOTE + 1];      /* a question for the player, answered by voice; may be empty */
    char remember[OK_MIND_NOTE + 1]; /* a fact it wants to keep for good; may be empty */
    /* The body, chosen by 0G too; the skeleton and its idle life are the
     * okupa's instinct. -1 = not given. */
    int x, z;   /* where it walks first, 0..100 across the floor (left to right, back to front) */
    int gaze;   /* OK_LOOK_* */
    int energy; /* 0 sluggish .. 100 bouncy; 50 when not given */
    /* Things it decides to make, rarely; empty = none. */
    char make[41];   /* an object to build out of boxes */
    char draw[41];   /* something to draw on its own sketchbook */
} ok_mind;

/* What the okupa notices right now. Strings may be NULL; accents are dropped
 * (que for qu\u00e9) and other non-ASCII bytes too. */
typedef struct {
    const ok_pet *pet;
    int hour, minute;
    int outside;                   /* 1 in the garden, 0 in the attic */
    uint32_t seconds_since_player; /* since the last touch or button */
    const char *player_did;        /* the last thing the player did */
    const char *heard;             /* what the player just said (speech to text) */
    const char *saw;               /* what its camera just saw */
    const char *recent[OK_MIND_RECENT]; /* "wave: Hey there!", newest first */
    int recent_count;
    const char *lines[OK_MIND_LINES];   /* what it said lately, newest first: never again */
    int line_count;
    const char *known[OK_MIND_KNOWN];   /* facts it remembers for good, oldest first */
    int known_count;
    const char *asked;                  /* the question it is waiting to have answered */
    const char *city;                   /* where the console is, if the player said */
    const char *wifi;                   /* the Wi-Fi network it is on: a "place" it knows */
    const char *goal, *plan;            /* its slow mind's current goal and plan */
    const char *diary;                  /* its last diary entry: yesterday, as it remembers it */
    const char *today[24];              /* for the diary: what happened today, oldest first */
    int today_count;
    const char *date;                   /* for the diary: "2026-11-07" */
} ok_mind_obs;

/* The slow mind, every few minutes: what it wants and how it will go about it. */
#define OK_MIND_DIARY 240
#define OK_MIND_DREAM 160
typedef struct {
    char goal[OK_MIND_NOTE + 1], plan[OK_MIND_NOTE + 1], feeling[OK_MIND_NOTE + 1];
} ok_mind_plan;

const char *ok_mind_activity_name(int activity);
const char *ok_mind_mood_name(int mood);
const char *ok_mind_gaze_name(int gaze);
ok_result ok_mind_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len);
/* The slow mind: a goal and a plan for the next minutes, from the same
 * observation. */
ok_result ok_mind_plan_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len);
ok_result ok_mind_plan_parse(const char *body, size_t n, ok_mind_plan *plan);
/* The diary at the end of the day: obs->today and obs->date in, one entry out
 * (up to OK_MIND_DIARY characters). */
ok_result ok_mind_diary_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len);
ok_result ok_mind_diary_parse(const char *body, size_t n, char entry[OK_MIND_DIARY + 1]);
/* The dream it will have tonight, as a picture to draw (may be empty). */
void ok_mind_dream_parse(const char *body, size_t n, char dream[OK_MIND_DREAM + 1]);
/* The provider's whole response body. */
ok_result ok_mind_parse(const char *body, size_t n, ok_mind *decision);
/* One memory line for the next request: "activity", "activity: line", and
 * "(asked: ...)" when it asked something, so it does not ask it again. */
void ok_mind_memory(const ok_mind *d, char out[OK_MIND_MEMORY]);
/* A line it said lately again (the same words, or one inside the other,
 * ignoring case and punctuation): the line is blanked (the rest of the
 * decision stands) and 1 returned. Nothing is ever put in its place. */
int ok_mind_dedupe(ok_mind *d, const char *const *lines, int n);
/* One character of UTF-8 text as printable ASCII: accents dropped (e for
 * e-acute), -1 for anything else that is not ASCII; *used: the bytes it took. */
int ok_mind_ascii(const char *s, size_t left, size_t *used);

#endif
