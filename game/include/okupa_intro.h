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
/* Its parts: the picture with no fade, the fade at t (towards the colour
 * to, k/1000 of the way) and the words on the bottom screen. */
void ok_intro_film_scene(uint16_t *top, uint32_t t);
void ok_intro_film_fade(uint32_t t, uint16_t *to, int *k);
void ok_intro_film_words(uint16_t *bottom, uint32_t t);
/* Set by the platform when it has the film baked (okupa_film.h): it draws
 * the top screen for t, faded, and returns 1; 0 and the film is drawn live. */
extern int (*ok_intro_baked)(uint16_t *top, uint32_t t);

#endif
