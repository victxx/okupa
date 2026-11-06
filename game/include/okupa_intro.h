#ifndef OKUPA_INTRO_H
#define OKUPA_INTRO_H

#include <stdint.h>

/* What the console shows when it starts: a splash (the name, "by victorxva",
 * printed in halftone like an old studio card) and then a short film, the
 * house seen from outside at night, the camera flying in through the round
 * attic window. Both are pure functions of the tick (60 a second). */
#define OK_SPLASH_TICKS 330
#define OK_FILM_TICKS 780

void ok_intro_splash(uint16_t *top, uint16_t *bottom, uint32_t t);
void ok_intro_film(uint16_t *top, uint16_t *bottom, uint32_t t);

#endif
