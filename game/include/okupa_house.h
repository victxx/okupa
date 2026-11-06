#ifndef OKUPA_HOUSE_H
#define OKUPA_HOUSE_H

#include "okupa_monkey.h"
#include <stdint.h>

/* The okupa's attic: plank floor, a round window in the gable, string
 * lights, a mattress, boxes, a crate with a candle, and the player's drawing
 * on the wall. The light follows the clock (dawn, day, golden hour, dusk,
 * night) and goes out while it sleeps at night. The still room is drawn once
 * per change of light and kept; each frame only the okupa and the small
 * living things (dust in the sunbeam, the candle, twinkling stars) are drawn. */

/* Where the okupa lives, wanders and sleeps. */
void ok_house_room(ok_monkey_room *room);

/* The top screen. drawing is the 64x64 sketch (1 bpp) or NULL. */
void ok_house_draw(uint16_t *frame, int hour, int minute, int lights_off, const uint8_t *drawing, const ok_look *look,
                   const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face);

#endif
