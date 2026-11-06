#ifndef OKUPA_MIND_H
#define OKUPA_MIND_H

#include "okupa.h"
#include "okupa_pet.h"

/* The okupa's mind on 0G: what it observes goes out as an OpenAI-style chat
 * request; what comes back must pick from closed sets. Anything else is
 * refused, and the caller falls back to the safe stop: the okupa naps and
 * nothing is made up. */
enum { OK_ACT_EAT, OK_ACT_NAP, OK_ACT_PLAY, OK_ACT_TIDY, OK_ACT_READ, OK_ACT_WAVE, OK_ACT_WINDOW, OK_ACTIVITIES };
enum { OK_FEEL_HAPPY, OK_FEEL_SLEEPY, OK_FEEL_BORED, OK_FEEL_HUNGRY, OK_FEEL_LONELY, OK_FEEL_CURIOUS, OK_FEELINGS };
#define OK_MIND_LINE 40

typedef struct {
    int activity, mood;
    char line[OK_MIND_LINE + 1]; /* printable ASCII only */
} ok_mind;

const char *ok_mind_activity_name(int activity);
const char *ok_mind_mood_name(int mood);
ok_result ok_mind_request(const char *model, const ok_pet *p, int hour, int minute, const char *last_action,
                          uint32_t minutes_away, char *out, size_t cap, size_t *len);
/* The provider's whole response body. */
ok_result ok_mind_parse(const char *body, size_t n, ok_mind *decision);

#endif
