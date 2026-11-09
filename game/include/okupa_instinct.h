#ifndef OKUPA_INSTINCT_H
#define OKUPA_INSTINCT_H

#include "okupa_mind.h"

/* The okupa's own small mind, on the console: no network and no model. From
 * its needs, the hour, where it is and what the player just did, it picks a
 * move and, now and then, a line or a daydream from its own phrase book. It
 * never repeats a line in obs->lines. seed: any number that changes between
 * calls (a timer); the same seed and obs give the same decision.
 *
 * It is the okupa's mind whenever 0G is not configured or not answering, and
 * what it says is its own: nothing here is presented as 0G's. */
void ok_instinct_decide(const ok_mind_obs *obs, uint32_t seed, ok_mind *out);

/* 1 if the line came from the phrase book (for tests and the Dev screen). */
int ok_instinct_knows(const char *line);

#endif
