#ifndef OKUPA_MINIGAMES_H
#define OKUPA_MINIGAMES_H

#include <stdint.h>

/* Small games on the console's own, no network: the logic only, a tick at
 * 60 Hz and a tap at a time; the screens draw them (game/src/ui.c).
 *
 * Banana catch: bananas fall down the bottom screen; tap them before they
 * reach the floor. Twenty seconds; each one caught is a bite for the okupa.
 *
 * Copy me: four pads light up in a row that grows by one each round; tap
 * them back in the same order. A mistake ends it; the rounds made are fun. */
#define OK_CATCH_BANANAS 6
#define OK_CATCH_TICKS (20 * 60)
#define OK_SIMON_MAX 32

typedef struct {
    int x[OK_CATCH_BANANAS], y[OK_CATCH_BANANAS]; /* top-left of each banana, -1 y: none */
    int speed[OK_CATCH_BANANAS];                  /* pixels a tick, in 1/16 */
    int sub[OK_CATCH_BANANAS];
    int caught, missed, ticks, over;
    uint32_t rng;
} ok_catch;

void ok_catch_start(ok_catch *g, uint32_t seed);
void ok_catch_tick(ok_catch *g);
/* 1 if (x, y) hit a banana (it is caught). */
int ok_catch_tap(ok_catch *g, int x, int y);
#define OK_CATCH_SIZE 20 /* a banana's box, pixels */
#define OK_CATCH_FLOOR 160

enum { OK_SIMON_SHOW, OK_SIMON_YOURS, OK_SIMON_OVER };
typedef struct {
    uint8_t row[OK_SIMON_MAX];
    int length, at, state, ticks, lit, rounds;
    uint32_t rng;
} ok_simon;

void ok_simon_start(ok_simon *g, uint32_t seed);
void ok_simon_tick(ok_simon *g);
/* The player taps pad 0..3: 1 right, 0 wrong (it is over), -1 not now. */
int ok_simon_tap(ok_simon *g, int pad);

#endif
