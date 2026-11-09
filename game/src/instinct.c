#include "okupa_instinct.h"
#include <stdio.h>
#include <string.h>

/* Its phrase book: short lines (at most OK_MIND_LINE characters) for what is
 * going on, and daydreams for its thought bubble. */
#define LINES_OF(a) (int)(sizeof(a) / sizeof(a)[0])

static const char *const fed[] = {"Mmm, crunchy!", "Best meal in this attic.", "You always know.",
                                  "Saving a crumb for later.", "Food tastes better on a DSi."};
static const char *const played[] = {"Again! Again!", "I win! ...right?", "You're fun, you know that?",
                                     "My tail is dizzy.", "Best game ever."};
static const char *const petted[] = {"Hehe, that tickles.", "Right behind the ear!", "Okay, you can stay.",
                                     "Purr. Monkeys purr now.", "More of that, please."};
static const char *const tapped[] = {"Knock knock?", "Hey, that's my window!", "I felt that!",
                                     "Who's poking the glass?", "Yes? I'm listening."};
static const char *const hello[] = {"You're back!", "I kept the attic warm.", "Missed you. A bit.",
                                    "Oh! The lid opened!", "Hi! I didn't touch anything."};
static const char *const hungry[] = {"My tummy is rumbling.", "Is that a snack I smell?", "Feed me, landlord?",
                                     "I'd trade a drawing for a banana.", "So... hungry..."};
static const char *const sleepy[] = {"*yawn*", "My eyes are heavy.", "Five more minutes...",
                                     "Is it bedtime yet?", "I could nap on the stylus."};
static const char *const bored[] = {"Nothing ever happens here.", "Play with me?", "I counted the pixels. Twice.",
                                    "Let's draw something!", "Bored, bored, bored."};
static const char *const lonely[] = {"Hello? Anyone out there?", "It's quiet without you.", "Come back soon, okay?",
                                     "I talked to the wall. It's shy.", "Are you still there?"};
static const char *const happy[] = {"Life is good in here.", "I love this little house.", "Today is a good day.",
                                    "La la la...", "This attic is all mine.", "Look at me go!"};
static const char *const night[] = {"The stars look pixelated.", "Shh, the house is asleep.", "Night owl, huh?",
                                    "Late again? Me too."};
static const char *const garden[] = {"Fresh air! Well, fresh pixels.", "The ivy says hi.", "I like it out here.",
                                     "Smell that? Grass!"};
static const char *const dreams[] = {"What does Wi-Fi taste like?", "Maybe the screen goes on forever.",
                                     "I wonder who else lives in a console.", "One day I'll have a hammock.",
                                     "Do the nodes ever sleep?", "If I stack the frames, can I reach the sky?",
                                     "I should name the spider.", "Bananas in space. Think about it.",
                                     "Someone out there has my drawing.", "The clock in here never lies. Right?"};

static const char *const *const books[] = {fed, played, petted, tapped, hello, hungry, sleepy,
                                            bored, lonely, happy, night, garden, dreams};
static const int book_size[] = {LINES_OF(fed), LINES_OF(played), LINES_OF(petted), LINES_OF(tapped),
                                LINES_OF(hello), LINES_OF(hungry), LINES_OF(sleepy), LINES_OF(bored),
                                LINES_OF(lonely), LINES_OF(happy), LINES_OF(night), LINES_OF(garden),
                                LINES_OF(dreams)};

static uint32_t next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static int said_lately(const ok_mind_obs *obs, const char *line)
{
    int i;
    for (i = 0; i < obs->line_count; i++)
        if (obs->lines[i] && !strcmp(obs->lines[i], line)) return 1;
    return 0;
}

/* One line from the book that it has not said lately, or NULL. */
static const char *pick(const ok_mind_obs *obs, const char *const *book, int n, uint32_t *s)
{
    int start = (int)(next(s) % (uint32_t)n), k;
    for (k = 0; k < n; k++) {
        const char *line = book[(start + k) % n];
        if (!said_lately(obs, line)) return line;
    }
    return NULL;
}

static int did(const ok_mind_obs *obs, const char *what)
{
    return obs->player_did && strstr(obs->player_did, what) != NULL;
}

int ok_instinct_knows(const char *line)
{
    int b, i;
    for (b = 0; b < LINES_OF(books); b++)
        for (i = 0; i < book_size[b]; i++)
            if (!strcmp(books[b][i], line)) return 1;
    return 0;
}

void ok_instinct_decide(const ok_mind_obs *obs, uint32_t seed, ok_mind *out)
{
    static const int calm[] = {OK_MIND_LOOK, OK_MIND_SCRATCH, OK_MIND_STRETCH, OK_MIND_WANDER};
    static const int lively[] = {OK_MIND_HOP, OK_MIND_PLAY, OK_MIND_WAVE, OK_MIND_WANDER, OK_MIND_PEEK};
    const ok_pet *p = obs->pet;
    int mood = p ? ok_pet_mood(p) : OK_MOOD_HAPPY, late = obs->hour >= 22 || obs->hour < 7;
    int around = obs->seconds_since_player < 60, energy = p ? p->need[OK_NEED_ENERGY] : 50;
    uint32_t s = seed * 2654435761u + 0x9e3779b9u;
    const char *line = NULL;
    if (!s) s = 1;
    memset(out, 0, sizeof *out);
    out->x = out->z = out->gaze = -1;
    out->energy = energy < 20 ? 20 : energy > 90 ? 90 : energy;

    /* How it feels, then what it does about it. */
    switch (mood) {
    case OK_MOOD_HUNGRY:
        out->mood = OK_FEEL_HUNGRY;
        out->activity = next(&s) % 2 ? OK_MIND_EAT : OK_MIND_SIGH;
        out->gaze = OK_LOOK_FLOOR;
        break;
    case OK_MOOD_SLEEPY:
    case OK_MOOD_ASLEEP:
        out->mood = OK_FEEL_SLEEPY;
        out->activity = (late && !around) || energy < 20 ? OK_MIND_SLEEP : next(&s) % 2 ? OK_MIND_STRETCH : OK_MIND_SIGH;
        out->energy = 20;
        break;
    case OK_MOOD_BORED:
        out->mood = OK_FEEL_BORED;
        out->activity = next(&s) % 3 == 0 ? (obs->outside ? OK_MIND_GO_INSIDE : OK_MIND_GO_OUTSIDE) : OK_MIND_WANDER;
        break;
    case OK_MOOD_LONELY:
        out->mood = OK_FEEL_LONELY;
        out->activity = around ? OK_MIND_WAVE : OK_MIND_SIGH;
        out->gaze = around ? OK_LOOK_PLAYER : OK_LOOK_DOOR;
        break;
    default:
        out->mood = next(&s) % 3 ? OK_FEEL_HAPPY : OK_FEEL_CURIOUS;
        out->activity = out->mood == OK_FEEL_CURIOUS ? calm[next(&s) % 4] : lively[next(&s) % 5];
        out->gaze = (int)(next(&s) % OK_LOOKS);
        break;
    }
    if (out->activity != OK_MIND_SLEEP && out->activity != OK_MIND_GO_INSIDE && out->activity != OK_MIND_GO_OUTSIDE &&
        next(&s) % 2) {
        out->x = (int)(next(&s) % 101);
        out->z = (int)(next(&s) % 101);
    }

    /* What the player just did comes first; otherwise it speaks now and
     * then, more when someone is there. */
    if (did(obs, "fed")) line = pick(obs, fed, LINES_OF(fed), &s), out->activity = OK_MIND_EAT, out->mood = OK_FEEL_HAPPY;
    else if (did(obs, "played")) line = pick(obs, played, LINES_OF(played), &s), out->activity = OK_MIND_HOP, out->mood = OK_FEEL_HAPPY;
    else if (did(obs, "petted")) line = pick(obs, petted, LINES_OF(petted), &s), out->activity = OK_MIND_WAVE, out->mood = OK_FEEL_HAPPY;
    else if (did(obs, "turned the console on") || did(obs, "opened")) line = pick(obs, hello, LINES_OF(hello), &s), out->activity = OK_MIND_WAVE;
    else if (did(obs, "tapped") || did(obs, "touched")) line = pick(obs, tapped, LINES_OF(tapped), &s), out->gaze = OK_LOOK_PLAYER;
    else if (next(&s) % (around ? 2u : 4u) == 0) {
        switch (out->mood) {
        case OK_FEEL_HUNGRY: line = pick(obs, hungry, LINES_OF(hungry), &s); break;
        case OK_FEEL_SLEEPY: line = pick(obs, sleepy, LINES_OF(sleepy), &s); break;
        case OK_FEEL_BORED: line = pick(obs, bored, LINES_OF(bored), &s); break;
        case OK_FEEL_LONELY: line = pick(obs, lonely, LINES_OF(lonely), &s); break;
        default:
            line = late ? pick(obs, night, LINES_OF(night), &s)
                   : obs->outside ? pick(obs, garden, LINES_OF(garden), &s)
                                  : pick(obs, happy, LINES_OF(happy), &s);
        }
    }
    if (out->activity == OK_MIND_SLEEP) line = NULL; /* it just drifts off */
    if (line) snprintf(out->line, sizeof out->line, "%s", line);
    else if (out->activity != OK_MIND_SLEEP && next(&s) % 3 == 0) { /* or a daydream, not said */
        const char *d = dreams[next(&s) % (uint32_t)LINES_OF(dreams)];
        snprintf(out->thought, sizeof out->thought, "%s", d);
    }
}
