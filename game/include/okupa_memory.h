#ifndef OKUPA_MEMORY_H
#define OKUPA_MEMORY_H

#include "okupa.h"
#include "okupa_mind.h"

/* What the okupa keeps for good: facts its mind on 0G chose to remember
 * (the player's name, what they like...). Bounded, without repeats, oldest
 * forgotten first; one fact a line on the SD card. */
typedef struct {
    char fact[OK_MIND_KNOWN][OK_MIND_NOTE + 1];
    int count;
} ok_memory;

void ok_memory_init(ok_memory *m);
/* 1 if it was new and kept; 0 if empty or already known (case, spaces and
 * punctuation aside). */
int ok_memory_add(ok_memory *m, const char *fact);
/* From the text of the file (lines); unknown or empty lines are skipped. */
void ok_memory_load(ok_memory *m, const char *text, size_t n);
ok_result ok_memory_save(const ok_memory *m, char *out, size_t cap, size_t *len);
/* Points obs->known at its facts. */
void ok_memory_tell(const ok_memory *m, ok_mind_obs *obs);

#endif
