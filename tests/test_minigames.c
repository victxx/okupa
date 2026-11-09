#include "okupa_minigames.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static ok_catch c, d;
    static ok_simon s;
    int k, t, caught = 0;

    /* Banana catch: they fall, a tap on one catches it, the floor loses it, it ends after 20 s. */
    ok_catch_start(&c, 7);
    d = c;
    for (t = 0; t < 600; t++) ok_catch_tick(&c), ok_catch_tick(&d);
    assert(!memcmp(&c, &d, sizeof c)); /* the same seed, the same game */
    for (k = 0; k < OK_CATCH_BANANAS; k++) {
        assert(c.x[k] >= 8 && c.x[k] + OK_CATCH_SIZE <= 248 && c.y[k] <= OK_CATCH_FLOOR - OK_CATCH_SIZE);
        if (c.y[k] >= 0) caught += ok_catch_tap(&c, c.x[k] + 5, c.y[k] + 5);
    }
    assert(caught >= 1 && c.caught == caught && c.missed > 0);
    assert(!ok_catch_tap(&c, 0, 175)); /* the floor is not a banana */
    for (t = 0; t < OK_CATCH_TICKS; t++) ok_catch_tick(&c);
    assert(c.over && !ok_catch_tap(&c, c.x[0] + 5, c.y[0] + 5));

    /* Copy me: it shows the row, then waits; right taps grow it, a wrong one ends it. */
    ok_simon_start(&s, 99);
    assert(s.length == 1 && s.state == OK_SIMON_SHOW && ok_simon_tap(&s, 0) == -1);
    for (t = 0; t < 200 && s.state == OK_SIMON_SHOW; t++) ok_simon_tick(&s);
    assert(s.state == OK_SIMON_YOURS);
    for (k = 0; k < 5; k++) {
        int i, n = s.length;
        for (i = 0; i < n; i++) assert(ok_simon_tap(&s, s.row[i]) == 1);
        assert(s.rounds == k + 1 && s.length == n + 1 && s.state == OK_SIMON_SHOW);
        for (t = 0; t < 2000 && s.state == OK_SIMON_SHOW; t++) {
            ok_simon_tick(&s);
            assert(s.lit == -1 || s.lit == s.row[s.at]); /* only the pad due is lit */
        }
        assert(s.state == OK_SIMON_YOURS);
    }
    assert(ok_simon_tap(&s, (s.row[0] + 1) % 4) == 0 && s.state == OK_SIMON_OVER && s.rounds == 5);
    assert(ok_simon_tap(&s, 0) == -1);
    puts("minigames: banana catch and copy me play the same from the same seed, catch, miss, grow, end");
    return 0;
}
