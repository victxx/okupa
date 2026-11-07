#include "okupa_pet.h"
#include <string.h>

/* Seconds per point, awake and asleep: a need empties in about a day if
 * ignored, so two or three short visits keep it happy. Negative = recovers:
 * a minute of sleep a point of energy, so even an empty okupa is up again
 * in under two hours, not a whole night. */
static const int32_t awake_rate[OK_NEEDS] = {900, 600, 720, 1800};
static const int32_t asleep_rate[OK_NEEDS] = {1800, -60, 0, 3600};
#define WAKE_ENERGY 25 /* woken up, it has at least this: it does not drop straight back to sleep */

static void change(ok_pet *p, int need, int delta)
{
    int v = p->need[need] + delta;
    p->need[need] = (uint8_t)(v < 0 ? 0 : v > OK_NEED_MAX ? OK_NEED_MAX : v);
}

void ok_pet_init(ok_pet *p, int64_t now)
{
    memset(p, 0, sizeof *p);
    memset(p->need, 70, sizeof p->need);
    p->updated = now;
}

void ok_pet_update(ok_pet *p, int64_t now)
{
    int64_t elapsed = now - p->updated;
    int i;
    if (elapsed <= 0) return;
    if (elapsed > 7 * 86400) elapsed = 7 * 86400; /* a week away is enough */
    p->updated = now;
    for (i = 0; i < OK_NEEDS; i++) {
        int32_t rate = p->asleep ? asleep_rate[i] : awake_rate[i];
        int64_t total;
        if (rate == 0) continue;
        total = p->carry[i] + elapsed;
        change(p, i, (int)(rate > 0 ? -(total / rate) : total / -rate));
        p->carry[i] = (uint32_t)(total % (rate > 0 ? rate : -rate));
    }
    if (p->asleep && p->need[OK_NEED_ENERGY] >= OK_NEED_MAX) p->asleep = 0;
    if (!p->asleep && p->need[OK_NEED_ENERGY] == 0) p->asleep = 1;
}

int ok_pet_do(ok_pet *p, int action, int64_t now)
{
    ok_pet_update(p, now);
    if (p->asleep && action != OK_DO_WAKE) return 0;
    switch (action) {
    case OK_DO_FEED:
        if (p->need[OK_NEED_HUNGER] > 90) return 0;
        change(p, OK_NEED_HUNGER, 30);
        return 1;
    case OK_DO_PLAY:
        if (p->need[OK_NEED_ENERGY] < 15) return 0;
        change(p, OK_NEED_FUN, 25);
        change(p, OK_NEED_ENERGY, -4);
        change(p, OK_NEED_LOVE, 5);
        return 1;
    case OK_DO_PET:
        change(p, OK_NEED_LOVE, 15);
        return 1;
    case OK_DO_SLEEP:
        if (p->need[OK_NEED_ENERGY] > 80) return 0;
        p->asleep = 1;
        memset(p->carry, 0, sizeof p->carry);
        return 1;
    case OK_DO_WAKE:
        if (!p->asleep) return 0;
        p->asleep = 0;
        if (p->need[OK_NEED_ENERGY] < WAKE_ENERGY) p->need[OK_NEED_ENERGY] = WAKE_ENERGY;
        memset(p->carry, 0, sizeof p->carry);
        return 1;
    }
    return 0;
}

int ok_pet_mood(const ok_pet *p)
{
    static const int moods[OK_NEEDS] = {OK_MOOD_HUNGRY, OK_MOOD_SLEEPY, OK_MOOD_BORED, OK_MOOD_LONELY};
    int i, low = 0;
    if (p->asleep) return OK_MOOD_ASLEEP;
    for (i = 1; i < OK_NEEDS; i++)
        if (p->need[i] < p->need[low]) low = i;
    return p->need[low] < 35 ? moods[low] : OK_MOOD_HAPPY;
}

const char *ok_pet_line(const ok_pet *p, int hour)
{
    switch (ok_pet_mood(p)) {
    case OK_MOOD_ASLEEP: return "Zzz...";
    case OK_MOOD_HUNGRY: return "Is there anything to eat?";
    case OK_MOOD_SLEEPY: return hour >= 22 || hour < 7 ? "It's late. Lights off?" : "I could use a nap.";
    case OK_MOOD_BORED: return "I'm bored. Play with me?";
    case OK_MOOD_LONELY: return "Oh, you're back!";
    }
    if (hour < 7) return "Why are you up so late?";
    if (hour < 12) return "Morning! Nice day in here.";
    if (hour < 20) return "This house is mine now.";
    return "Cozy evening, isn't it?";
}

const char *ok_pet_reply(int action, int accepted)
{
    switch (action) {
    case OK_DO_FEED: return accepted ? "Yum! Thank you." : "I'm full, really.";
    case OK_DO_PLAY: return accepted ? "Again! Again!" : "Too tired to play...";
    case OK_DO_PET: return "Hehe, that tickles.";
    case OK_DO_SLEEP: return accepted ? "Good night..." : "I'm not sleepy!";
    case OK_DO_WAKE: return accepted ? "Mmh? I'm up, I'm up." : "I'm awake already.";
    }
    return "?";
}
