#include "okupa_instinct.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill(ok_pet *p, int hunger, int energy, int fun, int love)
{
    ok_pet_init(p, 1800000000);
    p->need[OK_NEED_HUNGER] = (uint8_t)hunger;
    p->need[OK_NEED_ENERGY] = (uint8_t)energy;
    p->need[OK_NEED_FUN] = (uint8_t)fun;
    p->need[OK_NEED_LOVE] = (uint8_t)love;
}

int main(void)
{
    ok_pet pet;
    ok_mind_obs obs;
    ok_mind d, e;
    uint32_t seed;
    int spoke = 0, slept = 0;

    memset(&obs, 0, sizeof obs);
    obs.pet = &pet;
    obs.hour = 15;

    /* Always a move and a mood from the closed sets; the same seed, the same decision. */
    fill(&pet, 90, 90, 90, 90);
    for (seed = 1; seed < 400; seed++) {
        ok_instinct_decide(&obs, seed, &d);
        assert(d.activity >= 0 && d.activity < OK_ACTIVITIES && d.mood >= 0 && d.mood < OK_FEELINGS);
        assert(d.gaze >= -1 && d.gaze < OK_LOOKS && d.x >= -1 && d.x <= 100 && d.z >= -1 && d.z <= 100);
        assert(strlen(d.line) <= OK_MIND_LINE && strlen(d.thought) <= OK_MIND_NOTE);
        assert(!d.line[0] || ok_instinct_knows(d.line)); /* its own words, from its book */
        spoke += d.line[0] != 0;
        slept += d.activity == OK_MIND_SLEEP;
        ok_instinct_decide(&obs, seed, &e);
        assert(!memcmp(&d, &e, sizeof d));
    }
    assert(spoke > 50 && spoke < 350); /* it talks now and then, not every time */
    assert(!slept);                    /* rested in the afternoon: no naps */

    /* Its needs show: hungry eats or sighs; tired at night with nobody there sleeps, silently. */
    fill(&pet, 10, 90, 90, 90);
    ok_instinct_decide(&obs, 7, &d);
    assert(d.mood == OK_FEEL_HUNGRY && (d.activity == OK_MIND_EAT || d.activity == OK_MIND_SIGH));
    fill(&pet, 90, 10, 90, 90);
    obs.hour = 23;
    obs.seconds_since_player = 600;
    ok_instinct_decide(&obs, 7, &d);
    assert(d.mood == OK_FEEL_SLEEPY && d.activity == OK_MIND_SLEEP && !d.line[0]);
    obs.hour = 15;
    obs.seconds_since_player = 0;

    /* What the player did comes first, and the answer fits it. */
    fill(&pet, 60, 90, 90, 90);
    obs.player_did = "fed you";
    ok_instinct_decide(&obs, 3, &d);
    assert(d.activity == OK_MIND_EAT && d.line[0]);
    obs.player_did = "petted you";
    ok_instinct_decide(&obs, 3, &d);
    assert(d.activity == OK_MIND_WAVE && d.line[0]);

    /* Never a line it said lately: with every petting line said, it stays quiet. */
    {
        static const char *said[] = {"Hehe, that tickles.", "Right behind the ear!", "Okay, you can stay.",
                                     "Purr. Monkeys purr now.", "More of that, please."};
        int k;
        for (k = 0; k < 5; k++) obs.lines[k] = said[k];
        obs.line_count = 5;
        for (seed = 1; seed < 50; seed++) {
            ok_instinct_decide(&obs, seed, &d);
            for (k = 0; k < 5; k++) assert(strcmp(d.line, said[k]));
        }
        obs.line_count = 4; /* one left: that one */
        ok_instinct_decide(&obs, 11, &d);
        assert(!strcmp(d.line, "More of that, please."));
    }
    assert(!ok_instinct_knows("I am 0G's mind."));
    puts("instinct: its own moves and lines from needs, hour and the player; never a recent line; no 0G");
    return 0;
}
