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

/* The attic's 16 textures, 32x32 tiles in a 128x128 skin (game/src/room_skin.c). */
enum { OK_ROOM_PLANKS, OK_ROOM_PLASTER, OK_ROOM_BRICK, OK_ROOM_BEAM, OK_ROOM_RUG, OK_ROOM_TICKING, OK_ROOM_CARDBOARD,
       OK_ROOM_CRATE, OK_ROOM_KNIT, OK_ROOM_COTTON, OK_ROOM_LEAVES, OK_ROOM_TIN, OK_ROOM_WALLPAPER, OK_ROOM_CLAY,
       OK_ROOM_FRAME, OK_ROOM_BOARDS };
extern const uint16_t ok_room_skin[128 * 128];

/* Where the okupa lives, wanders and sleeps. */
void ok_house_room(ok_monkey_room *room);

/* The gallery on the gable wall: six frames. ok_house_frame hangs a 64x64
 * one-bit picture (OK_DRAWING_BYTES, as okupa_act.h packs it) in a slot, or
 * takes it down with NULL; the picture is copied. Slots 0-3 run along the
 * wall over the bed and the crate, 4 and 5 sit either side of the window. */
#define OK_HOUSE_FRAMES 6
void ok_house_frame(int slot, const uint8_t *pixels);

/* The top screen. drawing is a picture for frame 0 while ok_house_frame has
 * hung nothing there (NULL: the frame stays empty). */
void ok_house_draw(uint16_t *frame, int hour, int minute, int lights_off, const uint8_t *drawing, const ok_look *look,
                   const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face);

#endif
