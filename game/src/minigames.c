#include "okupa_minigames.h"
#include <string.h>

static uint32_t next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

/* ---- Banana catch -------------------------------------------------------- */

static void drop(ok_catch *g, int k)
{
    g->x[k] = 8 + (int)(next(&g->rng) % (256 - 16 - OK_CATCH_SIZE));
    g->y[k] = -OK_CATCH_SIZE - (int)(next(&g->rng) % 90); /* staggered above the screen */
    g->speed[k] = 14 + (int)(next(&g->rng) % 14) + g->caught / 3; /* faster as it goes */
    g->sub[k] = 0;
}

void ok_catch_start(ok_catch *g, uint32_t seed)
{
    int k;
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1;
    for (k = 0; k < OK_CATCH_BANANAS; k++) {
        drop(g, k);
        g->y[k] -= k * 30;
    }
}

void ok_catch_tick(ok_catch *g)
{
    int k;
    if (g->over) return;
    if (++g->ticks >= OK_CATCH_TICKS) {
        g->over = 1;
        return;
    }
    for (k = 0; k < OK_CATCH_BANANAS; k++) {
        g->sub[k] += g->speed[k];
        g->y[k] += g->sub[k] / 16;
        g->sub[k] %= 16;
        if (g->y[k] > OK_CATCH_FLOOR - OK_CATCH_SIZE) { /* on the floor: lost */
            g->missed++;
            drop(g, k);
        }
    }
}

int ok_catch_tap(ok_catch *g, int x, int y)
{
    int k;
    if (g->over) return 0;
    for (k = 0; k < OK_CATCH_BANANAS; k++)
        if (g->y[k] > -OK_CATCH_SIZE && x >= g->x[k] - 4 && x < g->x[k] + OK_CATCH_SIZE + 4 && y >= g->y[k] - 4 &&
            y < g->y[k] + OK_CATCH_SIZE + 4) {
            g->caught++;
            drop(g, k);
            return 1;
        }
    return 0;
}

/* ---- Copy me ------------------------------------------------------------- */

#define SIMON_ON 26  /* ticks a pad stays lit while showing */
#define SIMON_GAP 10 /* ticks between two */

static void grow(ok_simon *g)
{
    if (g->length < OK_SIMON_MAX) g->row[g->length++] = (uint8_t)(next(&g->rng) % 4);
    g->state = OK_SIMON_SHOW;
    g->at = 0;
    g->ticks = -40; /* a breath before it starts */
    g->lit = -1;
}

void ok_simon_start(ok_simon *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1;
    grow(g);
}

void ok_simon_tick(ok_simon *g)
{
    if (g->state != OK_SIMON_SHOW) {
        if (g->ticks > 0 && --g->ticks == 0) g->lit = -1; /* the pad the player tapped dims */
        return;
    }
    g->ticks++;
    if (g->ticks < 0) return;
    if (g->ticks < SIMON_ON) {
        g->lit = g->row[g->at];
    } else {
        g->lit = -1;
        if (g->ticks >= SIMON_ON + SIMON_GAP) {
            g->ticks = 0;
            if (++g->at == g->length) { /* shown whole: the player's turn */
                g->state = OK_SIMON_YOURS;
                g->at = 0;
            }
        }
    }
}

int ok_simon_tap(ok_simon *g, int pad)
{
    if (g->state != OK_SIMON_YOURS || pad < 0 || pad > 3) return -1;
    g->lit = pad;
    g->ticks = 12;
    if (pad != g->row[g->at]) {
        g->state = OK_SIMON_OVER;
        return 0;
    }
    if (++g->at == g->length) {
        g->rounds++;
        if (g->length == OK_SIMON_MAX) g->state = OK_SIMON_OVER; /* the whole row: won */
        else grow(g);
    }
    return 1;
}
