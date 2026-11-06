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
#define OK_MIND_TEXT 160 /* what it heard or saw, cut to this */
#define OK_MIND_NOTE 60  /* a thought, a question, a fact to keep */
#define OK_MIND_KNOWN 16 /* facts it keeps about its life and the player */

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
} ok_mind;

/* What the okupa notices right now. Strings may be NULL; non-ASCII bytes
 * (a transcript with accents, say) are dropped. */
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
    const char *known[OK_MIND_KNOWN];   /* facts it remembers for good, oldest first */
    int known_count;
    const char *asked;                  /* the question it is waiting to have answered */
    const char *city;                   /* where the console is, if the player said */
    const char *wifi;                   /* the Wi-Fi network it is on: a "place" it knows */
} ok_mind_obs;

const char *ok_mind_activity_name(int activity);
const char *ok_mind_mood_name(int mood);
const char *ok_mind_gaze_name(int gaze);
ok_result ok_mind_request(const char *model, const ok_mind_obs *obs, char *out, size_t cap, size_t *len);
/* The provider's whole response body. */
ok_result ok_mind_parse(const char *body, size_t n, ok_mind *decision);
/* One memory line for the next request: "activity" or "activity: line". */
void ok_mind_memory(const ok_mind *d, char out[OK_MIND_LINE + 24]);

#endif
