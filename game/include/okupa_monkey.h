#ifndef OKUPA_MONKEY_H
#define OKUPA_MONKEY_H

#include "okupa_r3d.h"
#include <stdint.h>

/* The okupa: a little monkey drawn with the 3D renderer, in clothes the
 * player can change. Its skin is a 128x128 texture (fur, face, top); the
 * eyes and mouth are pixels laid over the head afterwards, so they stay crisp
 * at DSi size and change with its mood.
 *
 * It moves on springs: every joint eases towards where the moment wants it,
 * so poses blend, hops overshoot and land, and the tail and ears follow a
 * beat behind. What it does next (look around, scratch, wander, peek from
 * behind its mask...) is chosen from a seeded random sequence. */
enum { OK_WEAR_NONE, OK_WEAR_HEADPHONES, OK_WEAR_BANDANA, OK_WEAR_GLASSES, OK_WEAR_SCARF, OK_WEAR_FLOWER, OK_WEAR_LEAF,
       OK_WEAR_CAP, OK_WEAR_SMILEY, OK_WEARS };
enum { OK_TOP_HOODIE, OK_TOP_TEE, OK_TOP_STRIPES, OK_TOPS };
#define OK_COLOURS 8
/* Its mood, from the pet. */
enum { OK_POSE_IDLE, OK_POSE_HAPPY, OK_POSE_HUNGRY, OK_POSE_SLEEPY, OK_POSE_ASLEEP, OK_POSE_BORED, OK_POSE_LONELY,
       OK_POSES };
/* What the player did. */
enum { OK_EVENT_FEED, OK_EVENT_PLAY, OK_EVENT_PET, OK_EVENT_REFUSE, OK_EVENT_HELLO };
/* What it is doing. */
enum { OK_MOVE_IDLE, OK_MOVE_LOOK, OK_MOVE_SCRATCH, OK_MOVE_STRETCH, OK_MOVE_HOP, OK_MOVE_PEEK, OK_MOVE_WANDER,
       OK_MOVE_SIGH, OK_MOVE_WAVE, OK_MOVE_EAT, OK_MOVE_PLAY, OK_MOVE_PETTED, OK_MOVE_REFUSE, OK_MOVE_TO_BED,
       OK_MOVE_SLEEP, OK_MOVE_WAKE, OK_MOVE_TO_DOOR, OK_MOVES };

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

enum { OK_SPRING_BOB, OK_SPRING_LEAN, OK_SPRING_ROLL, OK_SPRING_NOD, OK_SPRING_TURN, OK_SPRING_TILT, OK_SPRING_RAISE_L,
       OK_SPRING_RAISE_R, OK_SPRING_FWD_L, OK_SPRING_FWD_R, OK_SPRING_LEG_L, OK_SPRING_LEG_R, OK_SPRING_TAIL0,
       OK_SPRING_TAIL1, OK_SPRING_TAIL2, OK_SPRING_TAIL3, OK_SPRING_EAR, OK_SPRING_SQUASH, OK_SPRING_MASK,
       OK_SPRING_YAW, OK_SPRING_LIE, OK_SPRING_BALL, OK_SPRING_CHEST, OK_SPRING_ELBOW_L, OK_SPRING_ELBOW_R,
       OK_SPRING_LIFT_L, OK_SPRING_LIFT_R, OK_SPRING_TAIL4, OK_SPRING_TAIL5, OK_SPRING_WALK, OK_SPRING_REACH, OK_SPRINGS };
/* LEG_L/R are where each foot is put, forward of the hip (hundredths), and
 * LIFT_L/R how high it is raised; the legs reach it by inverse kinematics. */

typedef struct {
    int32_t x, v; /* value and speed, 1/256 of a degree or a hundredth */
} ok_spring;

/* Places in the house, in hundredths of a unit. */
typedef struct {
    int32_t home_x, home_z, bed_x, bed_y, bed_z;
    int32_t spot_x[3], spot_z[3]; /* where it may wander to */
    int32_t door_x, door_z;       /* where it stands to go out (or in) */
    int32_t face_yaw;             /* which way it faces when standing about (degrees; 0 = +z) */
} ok_monkey_room;

typedef struct {
    ok_spring s[OK_SPRINGS];
    ok_monkey_room room;
    int32_t x, z, goal_x, goal_z; /* hundredths */
    int mood, move, move_t, stage, next_idle, walking, phase;
    int blink, look, bites;
    int gaze_turn, gaze_nod, gaze_t; /* where it is glancing while idle */
    int32_t gait;                     /* walk cycle, 1/4096 of a stride pair; advances with distance */
    /* The tail as a chain of eleven points in the room (hundredths), pulled
     * towards its resting curl, swinging with inertia. */
    int32_t tail[11][3], tail_was[11][3];
    int tail_ready;
    int directed;     /* a move asked for (OK_MOVE_* + 1), started at the next natural break */
    int at_door;      /* 1 once a TO_DOOR walk has arrived: the caller changes place */
    uint32_t ticks, seed;
} ok_monkey_anim;

/* What the face and the effects around it need once the frame is finished. */
typedef struct {
    int eye_x[2], eye_y[2], mouth_x, mouth_y, nose_x, nose_y, top_x, top_y;
    int32_t eye_z[2], mouth_z, nose_z;
    int mask_x, mask_y, mask_ux, mask_uy, mask_vx, mask_vy; /* the smiley, 1/16 pixel axes */
    int head_id, mask_id, smiley_id;
    int eyes, mouth, look, blush, hearts, zzz, glasses;
} ok_monkey_face;

const char *ok_wear_name(int wear);
const char *ok_top_name(int top);
uint16_t ok_top_colour(int colour);

/* The texture made from a look: fur, face and the chosen top. */
void ok_monkey_skin(const ok_look *look, uint16_t skin[OK_SKIN * OK_SKIN]);

void ok_monkey_init(ok_monkey_anim *a, const ok_monkey_room *room, int asleep, uint32_t seed);
void ok_monkey_event(ok_monkey_anim *a, int event);
/* Someone (the okupa's mind) chose its next move: it starts at the next
 * natural break, as if its own random picker had chosen it; with no choice
 * it keeps picking for itself. Moves that need a reason the player gives
 * (eating, playing) play as gestures; sleep follows the pet, not this.
 * OK_MOVE_TO_DOOR walks to room.door_x/z and sets at_door. */
void ok_monkey_direct(ok_monkey_anim *a, int move);
/* One tick of 1/60 s. mood is OK_POSE_*; wear lets it play with what it wears. */
void ok_monkey_step(ok_monkey_anim *a, int mood, int wear);
/* Draws it where the animation has it (world space, hundredths -> units).
 * Call before ok_r3d_finish; skin may be the player's own texture. */
void ok_monkey_draw(const ok_look *look, const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face);
/* After ok_r3d_finish: eyes, mouth, cheeks, glasses, the smiley's face. */
void ok_monkey_face_draw(uint16_t *frame, const ok_monkey_face *face);

#endif
