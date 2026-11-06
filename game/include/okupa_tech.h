#ifndef OKUPA_TECH_H
#define OKUPA_TECH_H

#include "okupa_facts.h"
#include <stdint.h>

/* The technical side of the console, behind SELECT: a guided "how it works"
 * walk-through and one page per layer (network, discovery, peers, the
 * transaction, the block check, 0G, the machine). Every value shown comes
 * from ok_facts; a missing one says so instead of showing an example. */
enum { OK_TECH_HOW, OK_TECH_NET, OK_TECH_FIND, OK_TECH_PEERS, OK_TECH_TX, OK_TECH_BLOCK, OK_TECH_0G, OK_TECH_DSI,
       OK_TECH_PAGES };
#define OK_TECH_STEPS 9

typedef struct {
    int page;
    int step;      /* walk-through step */
    int scroll;    /* raw transaction bytes */
    uint32_t ticks;
} ok_tech;

void ok_tech_init(ok_tech *t);
/* Stylus on the bottom screen (tabs, Back / Next); 1 if something changed. */
int ok_tech_touch(ok_tech *t, int x, int y);
/* Buttons: -1 / +1 for L / R (pages) or left / right (steps, scrolling). */
void ok_tech_page(ok_tech *t, int delta);
void ok_tech_step(ok_tech *t, int delta);
void ok_tech_render(const ok_tech *t, const ok_facts *f, const uint8_t (*font)[8], uint16_t *top, uint16_t *bottom);

#endif
