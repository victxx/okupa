#include "okupa_pet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ok_pet p, q;
    int64_t t = 1800000000;
    int i;

    ok_pet_init(&p, t);
    assert(p.need[OK_NEED_HUNGER] == 70 && !p.asleep && ok_pet_mood(&p) == OK_MOOD_HAPPY);

    /* Hunger drops a point per 15 minutes, whether time arrives at once or in seconds. */
    q = p;
    ok_pet_update(&p, t + 3600);
    for (i = 1; i <= 3600; i++) ok_pet_update(&q, t + i);
    assert(p.need[OK_NEED_HUNGER] == 66 && !memcmp(p.need, q.need, sizeof p.need));
    assert(p.need[OK_NEED_ENERGY] == 64 && p.need[OK_NEED_FUN] == 65 && p.need[OK_NEED_LOVE] == 68);
    ok_pet_update(&p, t); /* the clock went backwards: nothing happens */
    assert(p.need[OK_NEED_HUNGER] == 66);

    /* Hours alone: the most urgent need sets the mood, then it falls asleep on
     * its own when energy runs out. */
    t += 3600;
    ok_pet_update(&p, t + 8 * 3600);
    assert(p.need[OK_NEED_HUNGER] == 34 && p.need[OK_NEED_ENERGY] == 16 && ok_pet_mood(&p) == OK_MOOD_SLEEPY);
    assert(!strcmp(ok_pet_line(&p, 15), "I could use a nap."));
    p.need[OK_NEED_ENERGY] = 60;
    assert(ok_pet_mood(&p) == OK_MOOD_BORED);
    p.need[OK_NEED_ENERGY] = 16;
    ok_pet_update(&p, t + 12 * 3600);
    assert(p.asleep && ok_pet_mood(&p) == OK_MOOD_ASLEEP && !strcmp(ok_pet_line(&p, 15), "Zzz..."));
    assert(!ok_pet_do(&p, OK_DO_FEED, t + 12 * 3600)); /* asleep: no eating */
    ok_pet_update(&p, t + 12 * 3600 + 100 * 300);      /* sleep restores energy, then it wakes */
    assert(!p.asleep && p.need[OK_NEED_ENERGY] == 100);

    /* Actions and refusals. */
    t += 13 * 3600;
    ok_pet_init(&p, t);
    assert(ok_pet_do(&p, OK_DO_FEED, t) && p.need[OK_NEED_HUNGER] == 100);
    assert(!ok_pet_do(&p, OK_DO_FEED, t) && !strcmp(ok_pet_reply(OK_DO_FEED, 0), "I'm full, really."));
    assert(ok_pet_do(&p, OK_DO_PLAY, t) && p.need[OK_NEED_FUN] == 95 && p.need[OK_NEED_ENERGY] == 60);
    assert(ok_pet_do(&p, OK_DO_PET, t) && p.need[OK_NEED_LOVE] == 90);
    p.need[OK_NEED_ENERGY] = 10;
    assert(!ok_pet_do(&p, OK_DO_PLAY, t) && ok_pet_mood(&p) == OK_MOOD_SLEEPY);
    assert(!strcmp(ok_pet_line(&p, 23), "It's late. Lights off?"));
    assert(ok_pet_do(&p, OK_DO_SLEEP, t) && p.asleep);
    assert(ok_pet_do(&p, OK_DO_WAKE, t) && !p.asleep && !ok_pet_do(&p, OK_DO_WAKE, t));
    p.need[OK_NEED_ENERGY] = 90;
    assert(!ok_pet_do(&p, OK_DO_SLEEP, t));

    /* A long absence is capped at a week, and needs never go below zero. */
    ok_pet_update(&p, t + 365LL * 86400);
    for (i = 0; i < OK_NEEDS; i++) assert(p.need[i] <= OK_NEED_MAX);
    assert(p.need[OK_NEED_HUNGER] == 0);
    puts("pet: needs drift with real time, sleep and wake, actions and refusals");
    return 0;
}
