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
/* The outside's 16 textures, the same way (game/src/outside_skin.c). */
enum { OK_OUT_TILES, OK_OUT_MOSS_TILES, OK_OUT_GRASS, OK_OUT_FLOWERS, OK_OUT_EARTH, OK_OUT_COBBLE, OK_OUT_STUCCO,
       OK_OUT_SIDING, OK_OUT_DOOR, OK_OUT_BARK, OK_OUT_LEAVES, OK_OUT_DECK, OK_OUT_STONE, OK_OUT_BRICK, OK_OUT_IVY,
       OK_OUT_CLOUD };
extern const uint16_t ok_outside_skin[128 * 128];

/* Where the okupa lives, wanders and sleeps. */
void ok_house_room(ok_monkey_room *room);

/* The gallery: twelve frames. ok_house_frame hangs a 64x64 one-bit picture
 * (OK_DRAWING_BYTES, as okupa_act.h packs it) in a slot, or takes it down
 * with NULL; the picture is copied. In the attic, slots 0-3 run along the
 * wall over the bed and the crate, 4 and 5 sit either side of the window;
 * in the garden, 6-8 hang on the brick wall of the terrace and 9-11 stand
 * on easels. Empty frames stay up, waiting. */
#define OK_HOUSE_FRAMES 12
void ok_house_frame(int slot, const uint8_t *pixels);
/* A new picture arriving in an attic slot (0-5): t ticks into its hanging
 * (drop, bump, swing, settle, about 2.5 s; slot -1 when none). */
#define OK_HOUSE_HANG_TICKS 170
void ok_house_hanging(int slot, int t);

/* Where the okupa is: in the attic, or out on the terrace with the garden
 * gallery. ok_house_garden gives the terrace's places, for ok_monkey_init. */
enum { OK_PLACE_ATTIC, OK_PLACE_GARDEN };
void ok_house_place(int where);
int ok_house_where(void);
void ok_house_garden(ok_monkey_room *room);

/* The house from outside, for the intro: the island, the house, the tree,
 * the terrace, under the sky of that hour, seen from eye towards target. */
void ok_house_outside(uint16_t *frame, ok_v3 eye, ok_v3 target, int focal, int hour, int minute, uint32_t ticks);

/* The top screen. drawing is a picture for frame 0 while ok_house_frame has
 * hung nothing there (NULL: the frame stays empty). */
void ok_house_draw(uint16_t *frame, int hour, int minute, int lights_off, const uint8_t *drawing, const ok_look *look,
                   const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face);

#endif
