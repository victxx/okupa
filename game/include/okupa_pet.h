#ifndef OKUPA_PET_H
#define OKUPA_PET_H

#include <stdint.h>

/* The okupa's body: four needs that drift with real time, a bedtime, a mood.
 * Pure logic on a clock you pass in, the same on the DSi, the host or a phone. */
enum { OK_NEED_HUNGER, OK_NEED_ENERGY, OK_NEED_FUN, OK_NEED_LOVE, OK_NEEDS };
enum { OK_MOOD_HAPPY, OK_MOOD_HUNGRY, OK_MOOD_SLEEPY, OK_MOOD_BORED, OK_MOOD_LONELY, OK_MOOD_ASLEEP };
enum { OK_DO_FEED, OK_DO_PLAY, OK_DO_PET, OK_DO_SLEEP, OK_DO_WAKE };
#define OK_NEED_MAX 100

typedef struct {
    uint8_t need[OK_NEEDS]; /* 100 = fully satisfied */
    uint8_t asleep;
    int64_t updated;        /* unix seconds of the last update */
    uint32_t carry[OK_NEEDS]; /* seconds not yet turned into a point of change */
} ok_pet;

void ok_pet_init(ok_pet *p, int64_t now);
/* Lets the time since the last update pass (a clock going backwards is ignored). */
void ok_pet_update(ok_pet *p, int64_t now);
/* 1 if the okupa went along with it, 0 if it refused (full, too tired, ...). */
int ok_pet_do(ok_pet *p, int action, int64_t now);
int ok_pet_mood(const ok_pet *p);
/* What it says right now, in English, for the mood (and the hour, 0..23). */
const char *ok_pet_line(const ok_pet *p, int hour);
/* What it says after an action it accepted (1) or refused (0). */
const char *ok_pet_reply(int action, int accepted);

#endif
