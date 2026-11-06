#ifndef OKUPA_MONKEY_H
#define OKUPA_MONKEY_H

#include "okupa_r3d.h"
#include <stdint.h>

/* The okupa: a little monkey drawn with the 3D renderer, in clothes the
 * player can change. Its skin is a 128x128 texture (fur, face, top); the
 * face's eyes and mouth are pixels laid over the head afterwards, so they
 * stay crisp at DSi size and can change with its mood. */
enum { OK_WEAR_NONE, OK_WEAR_HEADPHONES, OK_WEAR_BANDANA, OK_WEAR_GLASSES, OK_WEAR_SCARF, OK_WEAR_FLOWER, OK_WEAR_LEAF,
       OK_WEAR_CAP, OK_WEARS };
enum { OK_TOP_HOODIE, OK_TOP_TEE, OK_TOP_STRIPES, OK_TOPS };
#define OK_COLOURS 8
enum { OK_POSE_IDLE, OK_POSE_HAPPY, OK_POSE_HUNGRY, OK_POSE_SLEEPY, OK_POSE_ASLEEP, OK_POSES };

typedef struct {
    uint8_t wear, top, colour;
} ok_look;

/* The skin texture and the rectangle each part wraps around. */
#define OK_SKIN 128
enum { OK_PART_HEAD, OK_PART_TOP, OK_PART_EYES, OK_PART_MUZZLE, OK_PART_EAR, OK_PART_EAR_IN, OK_PART_HANDS,
       OK_PART_SLEEVE, OK_PART_LEG, OK_PART_TAIL, OK_PART_HOOD, OK_PARTS };
typedef struct {
    const char *name;
    uint8_t x, y, w, h;
} ok_part;
const ok_part *ok_monkey_part(int part);

/* What the face needs once the 3D frame is finished. */
typedef struct {
    int eye_x[2], eye_y[2], mouth_x, mouth_y;
    int32_t eye_z[2], mouth_z;
    int head_id, mask_id, pose, wear, blink, open;
} ok_monkey_face;

const char *ok_wear_name(int wear);
const char *ok_top_name(int top);
uint16_t ok_top_colour(int colour);

/* The texture made from a look: fur, face and the chosen top. */
void ok_monkey_skin(const ok_look *look, uint16_t skin[OK_SKIN * OK_SKIN]);
/* Draws it at the current transform, feet at the origin, facing +z (lying on
 * its back, head towards -x, when asleep). skin may be the player's own
 * texture. ticks drive the small animations. Call before ok_r3d_finish. */
void ok_monkey_draw(const ok_look *look, const uint16_t *skin, int pose, uint32_t ticks, ok_monkey_face *face);
/* After ok_r3d_finish: eyes, mouth, cheeks (and glasses). */
void ok_monkey_face_draw(uint16_t *frame, const ok_monkey_face *face);

#endif
