#include "okupa_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static ok_memory m, back;
    char text[2048], fact[64];
    ok_mind_obs obs;
    size_t n;
    int i;

    ok_memory_init(&m);
    assert(ok_memory_add(&m, "The player is called Victor") == 1);
    assert(ok_memory_add(&m, "the player is called victor.") == 0); /* the same fact */
    assert(ok_memory_add(&m, "  ") == 0 && ok_memory_add(&m, "") == 0);
    assert(ok_memory_add(&m, "Victor likes mangoes") == 1 && m.count == 2);

    /* Saved and loaded back as lines. */
    assert(ok_memory_save(&m, text, sizeof text, &n) == OK_OK);
    assert(!strcmp(text, "The player is called Victor\nVictor likes mangoes\n"));
    ok_memory_load(&back, text, n);
    assert(back.count == 2 && !strcmp(back.fact[1], "Victor likes mangoes"));
    ok_memory_load(&back, "a\r\n\r\nb", 6);
    assert(back.count == 2 && !strcmp(back.fact[0], "a") && !strcmp(back.fact[1], "b"));

    /* Full: the oldest is forgotten first. */
    for (i = 0; i < OK_MIND_KNOWN; i++) {
        snprintf(fact, sizeof fact, "Fact number %d", i);
        ok_memory_add(&m, fact);
    }
    assert(m.count == OK_MIND_KNOWN && !strcmp(m.fact[0], "Fact number 0"));
    assert(!strcmp(m.fact[OK_MIND_KNOWN - 1], "Fact number 15"));

    memset(&obs, 0, sizeof obs);
    ok_memory_tell(&m, &obs);
    assert(obs.known_count == OK_MIND_KNOWN && obs.known[0] == m.fact[0]);
    assert(ok_memory_save(&m, text, 10, &n) == OK_RANGE);
    puts("memory: kept once, saved as lines, oldest forgotten when full");
    return 0;
}
