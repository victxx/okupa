#include "okupa_monkey.h"
#include <string.h>

#define RGB(r, g, b) ((uint16_t)(0x8000u | (r) | ((g) << 5) | ((b) << 10)))
#define FUR RGB(15, 9, 5)
#define FUR_DARK RGB(12, 7, 4)
#define CREAM RGB(30, 24, 18)
#define EAR_IN RGB(28, 18, 15)
#define INK RGB(3, 2, 2)
#define WHITE RGB(31, 31, 31)
#define BLUSH RGB(30, 17, 15)
#define DEG(d) ((d) * OK_TURN / 360)

/* Sizes in hundredths of a unit (a unit is about 10 cm). */
#define H(v) ((ok_fx)((v) * 4096 / 100))
static ok_v3 P(int x, int y, int z)
{
    return ok_v3_make(H(x), H(y), H(z));
}

static const ok_part parts[OK_PARTS] = {
    {"head", 0, 0, 64, 48},     {"top", 64, 0, 64, 48},      {"eye patches", 0, 48, 32, 32},
    {"muzzle", 32, 48, 32, 32}, {"ear", 64, 48, 16, 32},     {"inner ear", 80, 48, 16, 32},
    {"hands, feet", 96, 48, 32, 32}, {"sleeve", 0, 80, 32, 48}, {"leg", 32, 80, 32, 48},
    {"tail", 64, 80, 32, 48},   {"hood", 96, 80, 32, 48},
};

static const uint16_t tops[OK_COLOURS] = {RGB(9, 19, 17), RGB(29, 12, 10), RGB(29, 22, 7), RGB(6, 10, 20),
                                          RGB(21, 16, 27), RGB(17, 26, 20), RGB(29, 27, 23), RGB(8, 8, 9)};

const ok_part *ok_monkey_part(int part)
{
    return part >= 0 && part < OK_PARTS ? &parts[part] : &parts[0];
}

const char *ok_wear_name(int wear)
{
    static const char *n[OK_WEARS] = {"None", "Headset", "Bandana", "Glasses", "Scarf", "Flower", "Leaf", "Cap", "Smiley"};
    return wear >= 0 && wear < OK_WEARS ? n[wear] : "";
}

const char *ok_top_name(int top)
{
    static const char *n[OK_TOPS] = {"Hoodie", "T-shirt", "Stripes"};
    return top >= 0 && top < OK_TOPS ? n[top] : "";
}

uint16_t ok_top_colour(int colour)
{
    return tops[(unsigned)colour % OK_COLOURS];
}

static uint16_t shade(uint16_t c, int num, int den)
{
    int r = (c & 31) * num / den, g = (c >> 5 & 31) * num / den, b = (c >> 10 & 31) * num / den;
    return (uint16_t)(0x8000u | (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10);
}

static void fill(uint16_t *skin, int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) skin[j * OK_SKIN + i] = c;
}

void ok_monkey_skin(const ok_look *look, uint16_t skin[OK_SKIN * OK_SKIN])
{
    const ok_part *p;
    uint16_t top = ok_top_colour(look->colour);
    int i;
    fill(skin, 0, 0, OK_SKIN, OK_SKIN, FUR);
    /* Fur: a slightly darker crown and back of the head. */
    p = &parts[OK_PART_HEAD];
    fill(skin, p->x, p->y, p->w, p->h, FUR);
    fill(skin, p->x, p->y, p->w, 6, FUR_DARK);
    fill(skin, p->x, p->y, 10, p->h, FUR_DARK);
    fill(skin, p->x + p->w - 10, p->y, 10, p->h, FUR_DARK);
    p = &parts[OK_PART_EYES];
    fill(skin, p->x, p->y, p->w, p->h, CREAM);
    p = &parts[OK_PART_MUZZLE];
    fill(skin, p->x, p->y, p->w, p->h, RGB(31, 22, 17)); /* the snout a touch warmer than the face */
    p = &parts[OK_PART_EAR_IN];
    fill(skin, p->x, p->y, p->w, p->h, EAR_IN);
    p = &parts[OK_PART_HANDS];
    fill(skin, p->x, p->y, p->w, p->h, CREAM);
    /* The top, and sleeves: full for the hoodie, short for the tees. */
    p = &parts[OK_PART_TOP];
    fill(skin, p->x, p->y, p->w, p->h, top);
    if (look->top == OK_TOP_STRIPES)
        for (i = 6; i < p->h - 4; i += 8) fill(skin, p->x, p->y + i, p->w, 3, RGB(30, 29, 26));
    if (look->top == OK_TOP_HOODIE) {
        fill(skin, p->x + 20, p->y + 28, 24, 8, shade(top, 4, 5)); /* front pocket */
        fill(skin, p->x + 29, p->y + 12, 1, 9, RGB(29, 28, 26));   /* strings */
        fill(skin, p->x + 34, p->y + 12, 1, 9, RGB(29, 28, 26));
    } else {
        fill(skin, p->x + 26, p->y + 7, 12, 3, shade(top, 4, 5)); /* collar */
    }
    fill(skin, p->x, p->y + p->h - 4, p->w, 4, shade(top, 4, 5)); /* hem */
    p = &parts[OK_PART_SLEEVE];
    fill(skin, p->x, p->y, p->w, look->top == OK_TOP_HOODIE ? p->h : p->h / 3, top);
    if (look->top == OK_TOP_HOODIE) fill(skin, p->x, p->y + p->h - 9, p->w, 5, shade(top, 4, 5)); /* cuff */
    if (look->top == OK_TOP_STRIPES) fill(skin, p->x, p->y + 6, p->w, 3, RGB(30, 29, 26));
    p = &parts[OK_PART_HOOD];
    fill(skin, p->x, p->y, p->w, p->h, shade(top, 9, 10));
}

/* ------------------------------------------------------------- animation */

#define Q 256 /* springs hold values times this */

/* Stiffness and damping per spring, in 1/256 per tick. Stiff springs follow
 * closely; soft ones lag and overshoot, which is what makes it look alive. */
static const uint8_t stiff[OK_SPRINGS] = {
    180, 60, 60, 70, 60, 60, 110, 110, 110, 110, 250, 250, /* bob .. legs */
    90, 50, 30, 20,                                        /* tail, each joint softer */
    60, 200, 60, 40, 25, 250,                              /* ear, squash, mask, yaw, lie, ball */
    60, 110, 110, 250, 250, 14, 10, 30, 120,               /* chest, elbows, foot lifts, tail tip, walking, reach */
};
static const uint8_t damp[OK_SPRINGS] = {
    91, 61, 60, 65, 62, 60, 77, 77, 77, 77, 125, 125,
    43, 28, 22, 18,
    22, 56, 70, 64, 56, 179,
    60, 77, 77, 125, 125, 16, 14, 60, 85,
};

static int32_t isqrt(int32_t v);

static uint32_t rnd(ok_monkey_anim *a)
{
    uint32_t x = a->seed ? a->seed : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return a->seed = x;
}

static int below(ok_monkey_anim *a, int n)
{
    return (int)(rnd(a) % (uint32_t)n);
}

/* sin of (ticks * speed / 4096 turns), scaled to -n..n. */
static int wave(uint32_t ticks, int speed, int n)
{
    return (int)((int64_t)ok_r3d_sin((int)(ticks * (uint32_t)speed)) * n / 4096);
}

static void start(ok_monkey_anim *a, int move)
{
    a->move = move;
    a->move_t = 0;
    a->stage = 0;
    a->bites = 0;
}

void ok_monkey_init(ok_monkey_anim *a, const ok_monkey_room *room, int asleep, uint32_t seed)
{
    memset(a, 0, sizeof *a);
    a->room = *room;
    a->seed = seed | 1;
    a->x = a->goal_x = room->home_x;
    a->z = a->goal_z = room->home_z;
    a->s[OK_SPRING_SQUASH].x = 100 * Q;
    a->next_idle = 90;
    if (asleep) {
        a->move = OK_MOVE_SLEEP;
        a->x = room->bed_x;
        a->z = room->bed_z + 420;
        a->s[OK_SPRING_LIE].x = 100 * Q;
    }
}

void ok_monkey_event(ok_monkey_anim *a, int event)
{
    if (a->move == OK_MOVE_SLEEP || a->move == OK_MOVE_TO_BED || a->move == OK_MOVE_WAKE) return;
    a->goal_x = a->x; /* whatever it was walking to can wait */
    a->goal_z = a->z;
    a->walking = 0;
    switch (event) {
    case OK_EVENT_FEED: start(a, OK_MOVE_EAT); break;
    case OK_EVENT_PLAY: start(a, OK_MOVE_PLAY); break;
    case OK_EVENT_PET: start(a, OK_MOVE_PETTED); break;
    case OK_EVENT_REFUSE: start(a, OK_MOVE_REFUSE); break;
    default: start(a, OK_MOVE_WAVE); break;
    }
}

/* The next thing to do while nothing is asked of it, by mood. */
void ok_monkey_direct(ok_monkey_anim *a, int move)
{
    if (move < 0 || move >= OK_MOVES || move == OK_MOVE_IDLE || move == OK_MOVE_SLEEP || move == OK_MOVE_TO_BED || move == OK_MOVE_WAKE)
        return;
    a->directed = move + 1;
    if (a->move == OK_MOVE_IDLE && !a->walking) a->next_idle = 1; /* standing about: now is a natural break */
}

static void pick(ok_monkey_anim *a, int wear)
{
    if (a->directed) {
        start(a, a->directed - 1);
        a->directed = 0;
        a->look = below(a, 2) ? 1 : -1;
        a->at_door = 0;
        if (a->move == OK_MOVE_EAT || a->move == OK_MOVE_PLAY) a->bites = 0;
        return;
    }
    static const uint8_t idle[] = {OK_MOVE_LOOK, OK_MOVE_SCRATCH, OK_MOVE_LOOK, OK_MOVE_WANDER, OK_MOVE_LOOK, OK_MOVE_STRETCH};
    static const uint8_t happy[] = {OK_MOVE_HOP, OK_MOVE_WAVE, OK_MOVE_WANDER, OK_MOVE_LOOK, OK_MOVE_HOP, OK_MOVE_SCRATCH};
    static const uint8_t bored[] = {OK_MOVE_SIGH, OK_MOVE_LOOK, OK_MOVE_SIGH, OK_MOVE_WANDER, OK_MOVE_SCRATCH, OK_MOVE_STRETCH};
    static const uint8_t lonely[] = {OK_MOVE_LOOK, OK_MOVE_WAVE, OK_MOVE_LOOK, OK_MOVE_SIGH, OK_MOVE_WAVE, OK_MOVE_LOOK};
    static const uint8_t sleepy[] = {OK_MOVE_STRETCH, OK_MOVE_LOOK, OK_MOVE_SIGH, OK_MOVE_STRETCH, OK_MOVE_SCRATCH, OK_MOVE_LOOK};
    const uint8_t *set = a->mood == OK_POSE_HAPPY ? happy : a->mood == OK_POSE_BORED ? bored
                         : a->mood == OK_POSE_LONELY ? lonely : a->mood == OK_POSE_SLEEPY ? sleepy : idle;
    int m = set[below(a, 6)];
    if (wear == OK_WEAR_SMILEY && below(a, 8) == 0) m = OK_MOVE_PEEK;
    if (a->mood == OK_POSE_HUNGRY) m = below(a, 3) ? OK_MOVE_LOOK : OK_MOVE_SIGH;
    start(a, m);
    a->look = below(a, 2) ? 1 : -1;
}

/* Walks towards the goal; 1 once there. */
static int walk(ok_monkey_anim *a, int32_t gx, int32_t gz)
{
    int32_t dx = gx - a->x, dz = gz - a->z, d;
    a->goal_x = gx;
    a->goal_z = gz;
    d = (int32_t)(dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
    if (d < 8) {
        a->walking = 0;
        return 1;
    }
    a->walking = 1;
    {
        /* It speeds up and slows down with the walk spring, turns before it
         * goes, and the stride advances with the ground covered, so each
         * planted foot stays where it was put. */
        int ang = ok_r3d_atan2(dx, dz), yaw = (int)((int64_t)a->s[OK_SPRING_YAW].x * OK_TURN / (360 * Q));
        int off = ((ang - yaw) & (OK_TURN - 1)) > OK_TURN / 2 ? OK_TURN - ((ang - yaw) & (OK_TURN - 1)) : (ang - yaw) & (OK_TURN - 1);
        int32_t blend = a->s[OK_SPRING_WALK].x / Q, speed = 6 * (blend < 0 ? 0 : blend > 100 ? 100 : blend) / 100;
        if (off > OK_TURN / 6) speed = speed / 3; /* mostly turning on the spot */
        if (d < 40 && speed > 2) speed = 2;     /* arriving */
        a->x += (int32_t)((int64_t)ok_r3d_sin(ang) * speed / 4096);
        a->z += (int32_t)((int64_t)ok_r3d_cos(ang) * speed / 4096);
        if ((dx > 0) != (gx - a->x > 0) && dx) a->x = gx;
        if ((dz > 0) != (gz - a->z > 0) && dz) a->z = gz;
        a->gait += speed * 4096 / 120 + (speed < 2 ? 60 : 0); /* 120 hundredths a cycle: two steps of 60 */
    }
    return 0;
}

/* Back to standing about; it takes its time before the next thing, like a
 * pet that is mostly just there. */
static void done(ok_monkey_anim *a, int min, int range)
{
    start(a, OK_MOVE_IDLE);
    a->next_idle = a->directed ? 20 : 240 + 2 * (min + below(a, range)); /* a move asked for goes next */
}


/* ----------------------------------------------------------------- tail */

typedef struct {
    ok_fx m[3][3];
} m3;

static void m3_mul(m3 *a, ok_fx r[3][3])
{
    ok_fx o[3][3];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            o[i][j] = ok_fx_mul(a->m[i][0], r[0][j]) + ok_fx_mul(a->m[i][1], r[1][j]) + ok_fx_mul(a->m[i][2], r[2][j]);
    memcpy(a->m, o, sizeof o);
}

static void m3_rot(m3 *a, int axis, int angle)
{
    ok_fx s = ok_r3d_sin(angle), c = ok_r3d_cos(angle);
    ok_fx x[3][3] = {{4096, 0, 0}, {0, c, -s}, {0, s, c}}, y[3][3] = {{c, 0, s}, {0, 4096, 0}, {-s, 0, c}},
          z[3][3] = {{c, -s, 0}, {s, c, 0}, {0, 0, 4096}};
    m3_mul(a, axis == 0 ? x : axis == 1 ? y : z);
}

static void m3_apply(const m3 *a, const int32_t v[3], int32_t out[3])
{
    int i;
    for (i = 0; i < 3; i++) out[i] = (int32_t)(((int64_t)a->m[i][0] * v[0] + (int64_t)a->m[i][1] * v[1] + (int64_t)a->m[i][2] * v[2]) / 4096);
}

/* Where a point of the hips' frame is in the room, the way ok_monkey_draw
 * places the body (standing or lying). */
static void body_to_room(const ok_monkey_anim *a, const int32_t p[3], int32_t out[3])
{
    int lie = a->s[OK_SPRING_LIE].x / Q, yaw;
    int32_t q[3] = {p[0], p[1], p[2]}, r[3];
    m3 m;
    lie = lie < 0 ? 0 : lie > 100 ? 100 : lie;
    yaw = (int)((int64_t)a->s[OK_SPRING_YAW].x * OK_TURN / (360 * Q)) * (100 - lie) / 100;
    if (lie) {
        memset(&m, 0, sizeof m);
        m.m[0][0] = m.m[1][1] = m.m[2][2] = 4096;
        q[1] -= 190;
        m3_rot(&m, 2, DEG(90) * lie / 100);
        m3_apply(&m, q, r);
        q[0] = r[0], q[1] = r[1] + 160, q[2] = r[2];
    }
    memset(&m, 0, sizeof m);
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 4096;
    m3_rot(&m, 1, yaw);
    m3_apply(&m, q, r);
    out[0] = r[0] + a->x + (a->room.bed_x - a->x) * lie / 100;
    out[1] = r[1] + a->room.bed_y * lie / 100;
    out[2] = r[2] + a->z + (a->room.bed_z - a->z) * lie / 100;
}

#define TAIL_LINK 52

/* The tail's resting curl: down and back from the rump, then up into a
 * question mark, swaying with its springs and a slow wave. */
static void tail_rest(const ok_monkey_anim *a, int32_t rest[11][3])
{
    static const int8_t curl[10] = {-22, 8, 14, 18, 22, 26, 30, 34, 36, 30};
    int32_t q[3] = {40, 195 + a->s[OK_SPRING_BOB].x / Q, -112}, step[3] = {0, 0, -TAIL_LINK}, d[3];
    int i, sway = (int)((int64_t)a->s[OK_SPRING_TAIL0].x * OK_TURN / (360 * Q));
    m3 m;
    memset(&m, 0, sizeof m);
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 4096;
    {   /* out to its right when standing; along the legs, under the blanket, in bed */
        int lie = a->s[OK_SPRING_LIE].x / Q;
        lie = lie < 0 ? 0 : lie > 100 ? 100 : lie;
        m3_rot(&m, 1, -DEG(78) * (100 - lie) / 100);
        m3_rot(&m, 0, -DEG(90) * lie / 100);
    }
    body_to_room(a, q, rest[0]);
    for (i = 0; i < 10; i++) {
        m3_rot(&m, 1, sway / 4 + (int)((int64_t)ok_r3d_sin((int)(a->ticks * 11u) - i * 360) * DEG(5) / 4096));
        {   /* in bed it lies along the mattress instead of curling up */
            int lie = a->s[OK_SPRING_LIE].x / Q;
            lie = lie < 0 ? 0 : lie > 100 ? 100 : lie;
            m3_rot(&m, 0, DEG(curl[i]) * (100 - lie) / 100 - DEG(4) * lie / 100 +
                              (int)((int64_t)ok_r3d_sin((int)(a->ticks * 7u) - i * 300) * DEG(3) / 4096));
        }
        m3_apply(&m, step, d);
        q[0] += d[0], q[1] += d[1], q[2] += d[2];
        body_to_room(a, q, rest[i + 1]);
    }
}

/* Each point keeps its speed (a little damped), is pulled towards where the
 * curl wants it, softer towards the tip, then held at one link from the
 * point before: so it lags when the body turns or hops and whips after. */
static void tail_step(ok_monkey_anim *a)
{
    int32_t rest[11][3];
    int i, k;
    tail_rest(a, rest);
    if (!a->tail_ready) {
        memcpy(a->tail, rest, sizeof rest);
        memcpy(a->tail_was, rest, sizeof rest);
        a->tail_ready = 1;
        return;
    }
    memcpy(a->tail[0], rest[0], sizeof rest[0]);
    memcpy(a->tail_was[0], rest[0], sizeof rest[0]);
    for (i = 1; i < 11; i++) {
        int32_t next[3], d[3], len;
        int pull = 18 - i;
        for (k = 0; k < 3; k++) {
            next[k] = a->tail[i][k] + (a->tail[i][k] - a->tail_was[i][k]) * 90 / 100 + (rest[i][k] - a->tail[i][k]) * pull / 100;
            d[k] = next[k] - a->tail[i - 1][k];
        }
        len = isqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 400 || len < 1) { /* lost (a jump to the bed): start again from the curl */
            memcpy(a->tail, rest, sizeof rest);
            memcpy(a->tail_was, rest, sizeof rest);
            return;
        }
        memcpy(a->tail_was[i], a->tail[i], sizeof a->tail[i]);
        for (k = 0; k < 3; k++) a->tail[i][k] = a->tail[i - 1][k] + d[k] * TAIL_LINK / len;
    }
}

void ok_monkey_step(ok_monkey_anim *a, int mood, int wear)
{
    int32_t t[OK_SPRINGS];
    int i, mt, yaw_free = 1;
    uint32_t k;
    memset(t, 0, sizeof t);
    a->ticks++;
    mt = ++a->move_t;
    a->mood = mood;
    k = a->ticks;
    if (mood == OK_POSE_ASLEEP && a->move != OK_MOVE_TO_BED && a->move != OK_MOVE_SLEEP) start(a, OK_MOVE_TO_BED);
    if (mood != OK_POSE_ASLEEP && (a->move == OK_MOVE_TO_BED || a->move == OK_MOVE_SLEEP)) start(a, OK_MOVE_WAKE);
    if (a->move == OK_MOVE_IDLE && --a->next_idle <= 0) pick(a, wear);
    mt = a->move_t;

    /* Glances: every second or three it looks somewhere else, quickly, the way
     * eyes jump, and the head follows on its spring. */
    if (--a->gaze_t <= 0) {
        a->gaze_turn = below(a, 29) - 14;
        a->gaze_nod = below(a, 11) - 5;
        a->gaze_t = 50 + below(a, 160);
        if (below(a, 4) == 0) a->gaze_turn = a->gaze_nod = 0; /* back to you */
    }
    /* The mood sets the resting pose. */
    t[OK_SPRING_SQUASH] = 100 + wave(k, 17, 1);
    t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 14;
    t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 32;
    t[OK_SPRING_CHEST] = wave(k, 17, 2); /* breathing */
    t[OK_SPRING_TAIL0] = wave(k, 19, 9);
    switch (mood) {
    case OK_POSE_HAPPY:
        t[OK_SPRING_TURN] = a->gaze_turn / 2;
        t[OK_SPRING_BOB] = wave(k, 70, 14) < 0 ? -wave(k, 70, 14) : wave(k, 70, 14);
        t[OK_SPRING_ROLL] = wave(k, 35, 5);
        t[OK_SPRING_RAISE_L] = 18 + wave(k, 70, 6);
        t[OK_SPRING_RAISE_R] = 18 - wave(k, 70, 6);
        t[OK_SPRING_TAIL0] = wave(k, 35, 20);
        t[OK_SPRING_TILT] = wave(k, 35, 4);
        break;
    case OK_POSE_HUNGRY:
        t[OK_SPRING_LEAN] = 7;
        t[OK_SPRING_NOD] = 11;
        t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = -26;
        t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 30;
        t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 62;
        t[OK_SPRING_CHEST] = 9;
        t[OK_SPRING_BOB] = wave(k, 15, 3);
        if (k % 400 < 30) t[OK_SPRING_ROLL] = wave(k, 600, 3); /* tummy rumble */
        t[OK_SPRING_TAIL0] = wave(k, 12, 5) - 6;
        break;
    case OK_POSE_SLEEPY:
        t[OK_SPRING_CHEST] = 7 + wave(k, 11, 3);
        t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 6;
        t[OK_SPRING_NOD] = 13 + wave(k, 11, 7);
        t[OK_SPRING_TILT] = 7;
        t[OK_SPRING_BOB] = wave(k, 11, 6) - 8;
        t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 3;
        t[OK_SPRING_TAIL0] = wave(k, 9, 4) - 10;
        break;
    case OK_POSE_BORED:
        t[OK_SPRING_CHEST] = 6;
        t[OK_SPRING_LEAN] = -4;
        t[OK_SPRING_NOD] = 5;
        t[OK_SPRING_ROLL] = wave(k, 9, 4);
        t[OK_SPRING_TILT] = -6 + wave(k, 9, 3);
        t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 4;
        t[OK_SPRING_TAIL0] = wave(k, 9, 4) - 12;
        t[OK_SPRING_BOB] = -5;
        break;
    case OK_POSE_LONELY:
        t[OK_SPRING_NOD] = -4 + a->gaze_nod;
        t[OK_SPRING_TILT] = 5;
        t[OK_SPRING_TURN] = a->gaze_turn * 3 / 2;
        t[OK_SPRING_BOB] = wave(k, 20, 4);
        break;
    default:
        t[OK_SPRING_BOB] = wave(k, 17, 5);
        t[OK_SPRING_TURN] = a->gaze_turn;
        t[OK_SPRING_NOD] = a->gaze_nod;
        t[OK_SPRING_ROLL] = wave(k, 9, 3);                    /* weight shifting from foot to foot */
        t[OK_SPRING_TILT] = wave(k, 11, 3) - wave(k, 9, 2); /* the head stays level against it */
        t[OK_SPRING_RAISE_L] = 9 + wave(k, 17, 2);
        t[OK_SPRING_RAISE_R] = 9 - wave(k, 17, 2);
    }

    /* What it is doing on top of that. */
    switch (a->move) {
    case OK_MOVE_LOOK:
        t[OK_SPRING_TURN] = mt < 80 ? 38 * a->look : mt < 150 ? -26 * a->look : 0;
        t[OK_SPRING_NOD] += mt > 40 && mt < 70 ? -6 : 0;
        t[OK_SPRING_TILT] += 6 * a->look;
        if (mt > 190) done(a, 90, 200);
        break;
    case OK_MOVE_SCRATCH:
        t[OK_SPRING_RAISE_R] = mt > 15 && mt < 110 ? 128 + wave(k, 300, 7) : 9;
        t[OK_SPRING_FWD_R] = mt > 15 && mt < 110 ? 30 : 0;
        t[OK_SPRING_ELBOW_R] = mt > 15 && mt < 110 ? 105 + wave(k, 300, 10) : 16;
        t[OK_SPRING_TILT] = mt < 110 ? -12 : 0;
        t[OK_SPRING_TURN] = -8;
        if (mt > 140) done(a, 120, 240);
        break;
    case OK_MOVE_STRETCH:
        if (mt > 10 && mt < 95) {
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 140;
            t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 22;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 8;
            t[OK_SPRING_LEAN] = -6;
            t[OK_SPRING_CHEST] = -10;
            t[OK_SPRING_NOD] = -14;
            t[OK_SPRING_SQUASH] = 109;
            t[OK_SPRING_BOB] = 12;
        }
        if (mt > 130) done(a, 150, 240);
        break;
    case OK_MOVE_HOP: {
        int h = mt - 12;
        if (mt < 12) {
            t[OK_SPRING_BOB] = -22; /* crouch */
            t[OK_SPRING_SQUASH] = 86;
            t[OK_SPRING_CHEST] = 10;
        } else if (h < 22) {
            t[OK_SPRING_BOB] = (int)(4 * 130 * h * (22 - h) / (22 * 22)); /* the jump */
            t[OK_SPRING_SQUASH] = h < 8 ? 114 : 100;
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 70;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 40;
            t[OK_SPRING_CHEST] = -6;
        } else if (h < 30) {
            t[OK_SPRING_SQUASH] = 88; /* landing */
            t[OK_SPRING_BOB] = -14;
        }
        if (mt > 70) done(a, 90, 180);
        break;
    }
    case OK_MOVE_PEEK:
        if (mt < 150) {
            t[OK_SPRING_MASK] = mt > 12 ? 100 : 0;
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = mt < 40 || mt > 120 ? 35 : 12;
            t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = mt < 40 || mt > 120 ? 70 : 10;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = mt < 40 || mt > 120 ? 95 : 20;
            t[OK_SPRING_ROLL] = wave(k, 60, 6);
            t[OK_SPRING_TILT] = wave(k, 60, 10);
        }
        if (mt > 150 && mt < 190) t[OK_SPRING_BOB] = 30; /* boo! */
        if (mt > 230) done(a, 150, 260);
        break;
    case OK_MOVE_WANDER:
        yaw_free = 0;
        if (a->stage == 0) {
            if (mt == 1) a->phase = below(a, 3);
            if (walk(a, a->room.spot_x[a->phase], a->room.spot_z[a->phase])) a->stage = 1, a->move_t = 0;
        } else if (a->stage == 1) {
            t[OK_SPRING_NOD] = -10;
            t[OK_SPRING_TURN] = mt < 60 ? 20 * a->look : -20 * a->look;
            if (mt > 140) a->stage = 2;
        } else if (walk(a, a->room.home_x, a->room.home_z)) {
            done(a, 120, 240);
        }
        break;
    case OK_MOVE_SIGH:
        if (mt < 90) {
            t[OK_SPRING_BOB] = mt < 30 ? 10 : -16;
            t[OK_SPRING_NOD] = mt < 30 ? -6 : 18;
            t[OK_SPRING_CHEST] = mt < 30 ? -8 : 12;
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 1;
            t[OK_SPRING_TAIL0] = -16;
        }
        if (mt > 130) done(a, 150, 240);
        break;
    case OK_MOVE_WAVE:
        t[OK_SPRING_RAISE_R] = mt < 100 ? 112 + wave(k, 220, 8) : 9;
        t[OK_SPRING_FWD_R] = mt < 100 ? 22 : 0;
        t[OK_SPRING_ELBOW_R] = mt < 100 ? 45 + wave(k, 220, 30) : 16;
        t[OK_SPRING_TILT] = mt < 100 ? 9 : 0;
        t[OK_SPRING_BOB] += mt < 100 ? wave(k, 110, 4) : 0;
        if (mt > 120) done(a, 120, 240);
        break;
    case OK_MOVE_EAT: {
        int bite = mt > 30 && mt < 150 ? (mt - 30) % 40 : 99;
        /* The right hand is placed by inverse kinematics (REACH: 0 holding
         * the banana by the tummy, 100 at the mouth); the left rests on the tummy. */
        t[OK_SPRING_RAISE_L] = -18;
        t[OK_SPRING_FWD_L] = 25;
        t[OK_SPRING_ELBOW_L] = 55;
        t[OK_SPRING_REACH] = bite < 16 ? 100 : 0;
        if (bite < 16) t[OK_SPRING_NOD] = 6;
        if (bite == 12) a->bites++;
        t[OK_SPRING_NOD] += bite >= 14 && bite < 99 ? wave(k, 400, 3) : 0; /* chewing */
        if (mt > 160) {
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 120;
            t[OK_SPRING_FWD_R] = t[OK_SPRING_FWD_L] = 0;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 30;
            t[OK_SPRING_BOB] = (mt - 160) < 20 ? (mt - 160) * 4 : 0;
        }
        if (mt > 210) done(a, 120, 200);
        break;
    }
    case OK_MOVE_PLAY: {
        int toss = mt % 72, air = toss - 14;
        t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 22;
        t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 55;
        t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 45;
        if (air >= 0 && air < 46) {
            int dir = (mt / 72) % 2 ? -1 : 1;
            t[OK_SPRING_BALL] = 4 * 340 * air * (46 - air) / (46 * 46);
            t[OK_SPRING_TURN] = dir * (air - 23) * 12 / 23; /* the head follows it across */
            t[OK_SPRING_NOD] = -(int)(t[OK_SPRING_BALL] / 18);
            t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 120;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 15;
        } else if (toss < 14) {
            t[OK_SPRING_BOB] = toss < 7 ? -10 : 8;
        }
        if (air == 46) a->bites++;
        if (mt > 72 * 3) done(a, 90, 160);
        break;
    }
    case OK_MOVE_PETTED:
        if (mt < 150) {
            t[OK_SPRING_TILT] = 14 + wave(k, 55, 10);
            t[OK_SPRING_ROLL] = wave(k, 55, 6);
            t[OK_SPRING_TAIL0] = wave(k, 160, 26);
            t[OK_SPRING_BOB] = wave(k, 110, 8) < 0 ? 0 : wave(k, 110, 8);
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = -16;
            t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 30;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 80;
            t[OK_SPRING_CHEST] = wave(k, 55, 4);
        }
        if (mt > 170) done(a, 90, 180);
        break;
    case OK_MOVE_REFUSE:
        if (mt < 60) {
            t[OK_SPRING_TURN] = wave(k, 240, 24);
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = -40;
            t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 40;
            t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 105;
            t[OK_SPRING_LEAN] = -5;
        }
        if (mt > 80) done(a, 120, 200);
        break;
    case OK_MOVE_TO_BED:
        yaw_free = 0;
        if (a->stage == 0) {
            if (walk(a, a->room.bed_x, a->room.bed_z + 420)) a->stage = 1, a->move_t = 0;
        } else {
            if (mt < 18) { /* a little hop up onto the mattress */
                t[OK_SPRING_BOB] = mt < 6 ? -18 : 46;
                t[OK_SPRING_SQUASH] = mt < 6 ? 88 : 108;
            }
            t[OK_SPRING_LIE] = mt > 8 ? 100 : 0;
            if (mt > 80) start(a, OK_MOVE_SLEEP);
        }
        t[OK_SPRING_NOD] = 12;
        break;
    case OK_MOVE_SLEEP:
        t[OK_SPRING_LIE] = 100;
        t[OK_SPRING_BOB] = wave(k, 10, 3);
        t[OK_SPRING_CHEST] = 8 + wave(k, 10, 3); /* slow sleeping breaths */
        t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 4; /* hands tucked under the chin */
        t[OK_SPRING_FWD_L] = t[OK_SPRING_FWD_R] = 64;
        t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 108;
        t[OK_SPRING_TURN] = 0;
        t[OK_SPRING_NOD] = 16;
        t[OK_SPRING_TILT] = 0;
        t[OK_SPRING_TAIL0] = 0;
        a->x = a->room.bed_x;
        a->z = a->room.bed_z + 420;
        break;
    case OK_MOVE_TO_DOOR:
        yaw_free = 0;
        if (!a->at_door && walk(a, a->room.door_x, a->room.door_z)) a->at_door = 1;
        break;
    case OK_MOVE_WAKE:
        yaw_free = 0;
        if (a->stage == 0) {
            t[OK_SPRING_LIE] = 0;
            if (mt > 70) a->stage = 1, a->move_t = 0;
        } else if (a->stage == 1) {
            t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = mt > 10 && mt < 80 ? 142 : 9;
            t[OK_SPRING_LEAN] = mt < 80 ? -10 : 0;
            t[OK_SPRING_SQUASH] = mt > 10 && mt < 80 ? 108 : 100;
            if (mt > 110) a->stage = 2;
        } else if (walk(a, a->room.home_x, a->room.home_z)) {
            done(a, 120, 200);
        }
        break;
    default: break;
    }

    /* Walking: legs and arms swing, the body bobs twice a stride. */
    if (a->move == OK_MOVE_IDLE && (a->x != a->room.home_x || a->z != a->room.home_z)) walk(a, a->room.home_x, a->room.home_z);
    t[OK_SPRING_WALK] = a->walking ? 100 : 0;
    if (a->walking) {
        /* The body rides the gait: up over each planted foot, rolling onto
         * it, arms swinging against the legs, the head kept level. */
        int g = (int)(a->gait & 4095), b = a->s[OK_SPRING_WALK].x / Q;
        int sw = (int)((int64_t)ok_r3d_sin(g) * 1000 / 4096), up = (int)((int64_t)ok_r3d_sin(2 * g) * 1000 / 4096);
        b = b < 0 ? 0 : b > 100 ? 100 : b;
        t[OK_SPRING_FWD_L] = -sw * 30 / 1000 * b / 100;
        t[OK_SPRING_FWD_R] = sw * 30 / 1000 * b / 100;
        t[OK_SPRING_ELBOW_L] = t[OK_SPRING_ELBOW_R] = 34;
        t[OK_SPRING_RAISE_L] = t[OK_SPRING_RAISE_R] = 12;
        t[OK_SPRING_BOB] = ((up < 0 ? -up : up) * 12 / 1000 - 6) * b / 100;
        t[OK_SPRING_LEAN] = 6 * b / 100;
        t[OK_SPRING_ROLL] = sw * 5 / 1000 * b / 100;
        t[OK_SPRING_TILT] = -sw * 3 / 1000 * b / 100;
        t[OK_SPRING_NOD] = 3;
        t[OK_SPRING_TURN] = 0;
        t[OK_SPRING_YAW] = ok_r3d_atan2(a->goal_x - a->x, a->goal_z - a->z) * 360 / OK_TURN;
        if (t[OK_SPRING_YAW] > 180) t[OK_SPRING_YAW] -= 360;
    } else {
        t[OK_SPRING_YAW] = yaw_free ? 0 : a->s[OK_SPRING_YAW].x / Q;
        if (a->move == OK_MOVE_TO_BED || a->move == OK_MOVE_SLEEP || a->move == OK_MOVE_WAKE) t[OK_SPRING_YAW] = 0;
    }

    /* Follow-through: the tail whips a beat behind, ears flop against a hop,
     * the head lags the body. */
    t[OK_SPRING_TAIL1] = a->s[OK_SPRING_TAIL0].x / Q;
    t[OK_SPRING_TAIL2] = a->s[OK_SPRING_TAIL1].x / Q;
    t[OK_SPRING_TAIL3] = a->s[OK_SPRING_TAIL2].x / Q;
    t[OK_SPRING_TAIL4] = a->s[OK_SPRING_TAIL3].x / Q;
    t[OK_SPRING_TAIL5] = a->s[OK_SPRING_TAIL4].x / Q;
    t[OK_SPRING_EAR] = -a->s[OK_SPRING_BOB].v * 3 / 512;
    t[OK_SPRING_NOD] -= a->s[OK_SPRING_BOB].v * 3 / 1280;

    /* Turning the shortest way round. */
    {
        int32_t d = t[OK_SPRING_YAW] * Q - a->s[OK_SPRING_YAW].x;
        while (d > 180 * Q) d -= 360 * Q, a->s[OK_SPRING_YAW].x += 360 * Q;
        while (d < -180 * Q) d += 360 * Q, a->s[OK_SPRING_YAW].x -= 360 * Q;
    }
    for (i = 0; i < OK_SPRINGS; i++) {
        ok_spring *s = &a->s[i];
        s->v += (t[i] * Q - s->x) * stiff[i] / 2048 - s->v * damp[i] / 256;
        s->x += s->v;
    }

    tail_step(a);

    /* Blinks at random, now and then twice. */
    if (a->blink > 0) a->blink--;
    else if (below(a, 200) == 0) a->blink = below(a, 3) ? 7 : 20;
}

/* ------------------------------------------------------------------ body */

static int next_id;

static void part(const uint16_t *skin, int which, int id)
{
    const ok_part *p = &parts[which];
    ok_r3d_texture(skin, OK_SKIN);
    ok_r3d_uv(p->x, p->y, p->w, p->h);
    ok_r3d_material(FUR, id, OK_R3D_EDGE | OK_R3D_TEXTURED);
}

static int solid(uint16_t c)
{
    ok_r3d_material(c, ++next_id, OK_R3D_EDGE);
    return next_id;
}

static void blob(int x, int y, int z, int rx, int ry, int rz)
{
    ok_r3d_ellipsoid(P(x, y, z), P(rx, ry, rz), 10, 7);
}

static void limb(int x0, int y0, int z0, int x1, int y1, int z1, int r)
{
    ok_r3d_capsule(P(x0, y0, z0), P(x1, y1, z1), H(r), 8);
}

static int val(const ok_monkey_anim *a, int s)
{
    return a->s[s].x / Q;
}

static int deg(const ok_monkey_anim *a, int s)
{
    return (int)((int64_t)a->s[s].x * OK_TURN / (360 * Q));
}

static void headphones(void)
{
    static const int arc[7][2] = {{248, 640}, {235, 735}, {180, 830}, {0, 905}, {-180, 830}, {-235, 735}, {-248, 640}};
    int i;
    solid(RGB(6, 6, 7));
    for (i = 0; i < 6; i++) limb(arc[i][0], arc[i][1], -20, arc[i + 1][0], arc[i + 1][1], -20, 20);
    solid(RGB(28, 9, 8));
    blob(-255, 625, -10, 70, 100, 100);
    blob(255, 625, -10, 70, 100, 100);
}

static void flower(void)
{
    int i;
    solid(RGB(31, 20, 24));
    for (i = 0; i < 5; i++) {
        int a = DEG(i * 72 + 90);
        blob(175 + (int)((int64_t)ok_r3d_cos(a) * 50 / 4096), 830 + (int)((int64_t)ok_r3d_sin(a) * 50 / 4096), 120, 42, 42, 22);
    }
    solid(RGB(31, 27, 8));
    blob(175, 830, 140, 26, 26, 18);
}

/* The smiley mask: on the side of the head, or pulled down over the face. */
static void smiley(const ok_monkey_anim *a, ok_monkey_face *f)
{
    int m = val(a, OK_SPRING_MASK), cx, cy;
    /* On the side it sits tangent to the skull (up and out, facing where the
     * head's surface faces) so no part of it sinks in; over the face it sits
     * just in front of the snout. */
    int x = 172 + (0 - 172) * m / 100, y = 772 + (600 - 772) * m / 100, z = 134 + (280 - 134) * m / 100;
    int ry = 50 + (0 - 50) * m / 100, rx = -32 + (0 + 32) * m / 100, rz = -14 + (0 + 14) * m / 100;
    ok_r3d_push();
    ok_r3d_translate(P(x, y, z));
    ok_r3d_rotate_y(DEG(ry));
    ok_r3d_rotate_x(DEG(rx));
    ok_r3d_rotate_z(DEG(rz));
    f->smiley_id = solid(RGB(31, 26, 3));
    ok_r3d_ellipsoid(P(0, 0, 0), P(108, 108, 24), 14, 6);
    solid(RGB(4, 4, 3));
    limb(-102, 8, -10, -136, 24, -90, 8);
    if (ok_r3d_project(P(0, 0, 25), &cx, &cy, NULL)) {
        int ux, uy, vx, vy;
        ok_r3d_project(P(92, 0, 25), &ux, &uy, NULL);
        ok_r3d_project(P(0, 92, 25), &vx, &vy, NULL);
        f->mask_x = cx;
        f->mask_y = cy;
        f->mask_ux = ux - cx;
        f->mask_uy = uy - cy;
        f->mask_vx = vx - cx;
        f->mask_vy = vy - cy;
    } else {
        f->smiley_id = 0;
    }
    ok_r3d_pop();
}

static void wear(int which, const ok_monkey_anim *a, ok_monkey_face *f)
{
    switch (which) {
    case OK_WEAR_HEADPHONES: headphones(); break;
    case OK_WEAR_BANDANA:
        solid(RGB(27, 7, 7));
        ok_r3d_ellipsoid(P(0, 735, -5), P(247, 48, 240), 12, 5);
        blob(150, 745, -195, 55, 45, 45);
        limb(150, 745, -200, 215, 610, -250, 22);
        limb(150, 745, -200, 110, 600, -270, 22);
        break;
    case OK_WEAR_FLOWER: flower(); break;
    case OK_WEAR_LEAF:
        solid(RGB(9, 13, 5));
        limb(0, 860, 0, 10, 960, 0, 10);
        solid(RGB(10, 22, 8));
        ok_r3d_push();
        ok_r3d_translate(P(60, 975, 0));
        ok_r3d_rotate_z(DEG(25) + deg(a, OK_SPRING_EAR) / 2);
        blob(0, 0, 0, 75, 20, 42);
        ok_r3d_pop();
        break;
    case OK_WEAR_CAP:
        solid(RGB(7, 12, 24));
        ok_r3d_ellipsoid(P(0, 740, -10), P(246, 150, 242), 12, 7);
        ok_r3d_ellipsoid(P(0, 765, -235), P(130, 22, 115), 10, 4);
        solid(RGB(29, 28, 26));
        blob(0, 885, -10, 28, 20, 28);
        break;
    case OK_WEAR_SMILEY: smiley(a, f); break;
    default: break;
    }
}

static int32_t isqrt(int32_t v)
{
    int32_t r = 0, bit = 1 << 30;
    if (v <= 0) return 0;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

#define THIGH 75
#define SHIN 72

/* Where the gait puts a foot: forward of the hip and lifted, scaled by how
 * much it is walking. Planted half the cycle (moving back exactly as fast as
 * the body goes forward), swinging the other half on an eased arc. */
static void gait_foot(const ok_monkey_anim *a, int right, int *off, int *lift)
{
    int q = (int)((a->gait + (right ? 2048 : 0)) & 4095), b = a->s[OK_SPRING_WALK].x / Q;
    b = b < 0 ? 0 : b > 100 ? 100 : b;
    if (q < 2048) {
        *off = 30 - 60 * q / 2048;
        *lift = 0;
    } else {
        *off = -30 + (int)(60 - (int64_t)(ok_r3d_cos((q - 2048) / 2) + 4096) * 60 / 8192);
        *lift = (int)((int64_t)ok_r3d_sin((q - 2048) / 2) * 44 / 4096);
    }
    *off = *off * b / 100;
    *lift = *lift * b / 100;
}

/* Two-bone inverse kinematics: the thigh's angle forward and the knee's bend
 * (1/4096 turns) that put the ankle dz forward of and dy below the hip. */
static void reach(int dz, int dy, int *thigh, int *knee)
{
    int32_t d = isqrt(dz * dz + dy * dy), c1, c2;
    int phi = ok_r3d_atan2(dz, dy);
    if (d >= THIGH + SHIN - 2) { /* out of reach: in the air, the legs tuck up a little */
        *thigh = phi + DEG(14);
        *knee = DEG(30);
        return;
    }
    if (d < 40) d = 40;
    c1 = (THIGH * THIGH + d * d - SHIN * SHIN) * 4096 / (2 * THIGH * d);
    c2 = (THIGH * THIGH + SHIN * SHIN - d * d) * 4096 / (2 * THIGH * SHIN);
    c1 = c1 > 4096 ? 4096 : c1 < -4096 ? -4096 : c1;
    c2 = c2 > 4096 ? 4096 : c2 < -4096 ? -4096 : c2;
    *thigh = phi + ok_r3d_atan2(isqrt(4096 * 4096 - c1 * c1), c1);
    *knee = OK_TURN / 2 - ok_r3d_atan2(isqrt(4096 * 4096 - c2 * c2), c2);
}

/* A part of the skin, or only some of its rows (for limbs in two pieces). */
static void part_rows(const uint16_t *skin, int which, int id, int from, int to)
{
    const ok_part *p = &parts[which];
    ok_r3d_texture(skin, OK_SKIN);
    ok_r3d_uv(p->x, p->y + p->h * from / 100, p->w, p->h * (to - from) / 100);
    ok_r3d_material(FUR, id, OK_R3D_EDGE | OK_R3D_TEXTURED);
}

static void hand(const uint16_t *skin, int s, int id)
{
    part(skin, OK_PART_HANDS, id);
    ok_r3d_ellipsoid(P(s * 4, -28, 12), P(46, 42, 36), 8, 6);   /* palm */
    ok_r3d_ellipsoid(P(s * 4, -58, 16), P(38, 24, 30), 8, 5);   /* curled fingers */
    ok_r3d_ellipsoid(P(-s * 22, -20, 36), P(16, 24, 16), 6, 4); /* thumb */
}

/* An arm reaching a wrist point (hundredths, chest frame): the elbow found by
 * the law of cosines, bending down, out and back like a real one. */
static void arm_reach(const uint16_t *skin, int s, int hand_id, int sleeve_id, const int32_t wrist[3], int bites)
{
    static const int banana_pts[4][3] = {{0, 12, 18}, {-12, 58, 30}, {-28, 96, 34}, {-40, 122, 28}};
    const int32_t sh[3] = {s * 140, 410, 5};
    int32_t d[3], len, along, up, pole[3] = {s * 600, -800, -300}, pd, e[3], pl;
    int i, n = 3 - (bites > 3 ? 3 : bites);
    for (i = 0; i < 3; i++) d[i] = wrist[i] - sh[i];
    len = isqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 158) len = 158;
    if (len < 20) len = 20;
    along = (84 * 84 + len * len - 76 * 76) / (2 * len);       /* shoulder to the elbow's foot, along d */
    up = isqrt(84 * 84 - along * along);                        /* and out from the line */
    pd = (pole[0] * d[0] + pole[1] * d[1] + pole[2] * d[2]) / len; /* pole without its part along d */
    for (i = 0; i < 3; i++) pole[i] -= pd * d[i] / len;
    pl = isqrt(pole[0] * pole[0] + pole[1] * pole[1] + pole[2] * pole[2]);
    if (pl < 1) pl = 1;
    for (i = 0; i < 3; i++) e[i] = sh[i] + d[i] * along / len + pole[i] * up / pl;
    part_rows(skin, OK_PART_SLEEVE, sleeve_id, 0, 50);
    limb(e[0], e[1], e[2], sh[0], sh[1], sh[2], 44);
    part_rows(skin, OK_PART_SLEEVE, sleeve_id, 50, 100);
    limb(wrist[0], wrist[1], wrist[2], e[0], e[1], e[2], 38);
    part(skin, OK_PART_HANDS, hand_id);
    ok_r3d_ellipsoid(P(wrist[0], wrist[1] + 4, wrist[2] + 14), P(44, 40, 36), 8, 6);
    ok_r3d_ellipsoid(P(wrist[0] - s * 26, wrist[1] + 12, wrist[2] + 36), P(16, 24, 16), 6, 4);
    solid(RGB(31, 27, 8));
    for (i = 0; i < n; i++)
        limb(wrist[0] + s * banana_pts[i][0], wrist[1] + banana_pts[i][1], wrist[2] + banana_pts[i][2],
             wrist[0] + s * banana_pts[i + 1][0], wrist[1] + banana_pts[i + 1][1], wrist[2] + banana_pts[i + 1][2], 24);
    solid(RGB(14, 9, 3));
    if (n) blob(wrist[0] + s * banana_pts[n][0], wrist[1] + banana_pts[n][1], wrist[2] + banana_pts[n][2], 16, 16, 16);
}

/* The face for this moment, from its mood and what it is doing. */
static void expression(const ok_look *look, const ok_monkey_anim *a, ok_monkey_face *face)
{
    int mood = a->mood, mv = a->move;
    face->glasses = look->wear == OK_WEAR_GLASSES;
    /* The eyes get there first: they point where the head is going. */
    face->look = a->move == OK_MOVE_LOOK ? a->look
                 : a->move == OK_MOVE_IDLE && !a->walking ? (a->gaze_turn > 6 ? 1 : a->gaze_turn < -6 ? -1 : 0) : 0;
    face->eyes = 'o';
    face->mouth = 's';
    switch (mood) {
    case OK_POSE_HUNGRY: face->eyes = 'd', face->mouth = 'w'; break;
    case OK_POSE_SLEEPY: face->eyes = 'h', face->mouth = a->ticks % 420 > 330 ? 'y' : '.'; break;
    case OK_POSE_BORED: face->eyes = 'h', face->mouth = '-'; break;
    case OK_POSE_LONELY: face->eyes = 'o', face->mouth = '.'; break;
    case OK_POSE_HAPPY: face->mouth = 'O'; break;
    default: break;
    }
    switch (mv) {
    case OK_MOVE_HOP: face->eyes = 'j', face->mouth = 'O'; break;
    case OK_MOVE_STRETCH: if (a->move_t > 15 && a->move_t < 95) face->eyes = 's', face->mouth = 'y'; break;
    case OK_MOVE_PEEK: face->mouth = a->move_t > 150 ? 'O' : 's', face->eyes = a->move_t > 150 ? 'j' : 'o'; break;
    case OK_MOVE_SIGH: face->eyes = 'h', face->mouth = a->move_t < 40 ? 'O' : '-'; break;
    case OK_MOVE_WAVE: face->eyes = 'o', face->mouth = 'O'; break;
    case OK_MOVE_SCRATCH: face->eyes = 'h'; break;
    case OK_MOVE_EAT: face->eyes = a->move_t > 160 ? 'j' : 'o', face->mouth = a->move_t > 160 ? 'O' : (a->ticks / 6) % 2 ? 'c' : '.'; break;
    case OK_MOVE_PLAY: face->eyes = val(a, OK_SPRING_BALL) > 60 ? 'u' : 'j', face->mouth = 'O'; break;
    case OK_MOVE_PETTED: face->eyes = 'j', face->mouth = 'O', face->blush = 1, face->hearts = a->move_t; break;
    case OK_MOVE_REFUSE: face->eyes = 's', face->mouth = '-'; break;
    case OK_MOVE_TO_BED: face->eyes = 'h', face->mouth = '.'; break;
    case OK_MOVE_SLEEP: face->eyes = 's', face->mouth = '.', face->zzz = (int)(a->ticks % 1000000); break;
    case OK_MOVE_WAKE: face->eyes = a->stage < 2 ? 'h' : 'o', face->mouth = a->stage == 1 ? 'y' : 's'; break;
    default: break;
    }
    if (a->blink && (face->eyes == 'o' || face->eyes == 'd' || face->eyes == 'u')) face->eyes = 'b';
}

#define INK3 RGB(3, 2, 2)

/* Eyes, nose and mouth as small solids on the face, so they turn, light and
 * outline with the head instead of floating on it. */
static void face3d(const uint16_t *skin, const ok_monkey_face *f)
{
    int s;
    for (s = -1; s <= 1; s += 2) {
        int x = s * 82 + f->look * 8, y = 652;
        switch (f->eyes) {
        case 'j': /* smiling eyes, ^ ^ */
            solid(INK3);
            limb(x - 26, y - 8, 220, x - 12, y + 6, 226, 7);
            limb(x - 12, y + 6, 226, x + 12, y + 6, 226, 7);
            limb(x + 12, y + 6, 226, x + 26, y - 8, 220, 7);
            break;
        case 's': /* asleep, u u */
            solid(INK3);
            limb(x - 26, y + 4, 220, x - 12, y - 8, 226, 7);
            limb(x - 12, y - 8, 226, x + 12, y - 8, 226, 7);
            limb(x + 12, y - 8, 226, x + 26, y + 4, 220, 7);
            break;
        case 'b': /* a blink */
            solid(INK3);
            limb(x - 24, y - 4, 224, x + 24, y - 4, 224, 6);
            break;
        default:
            solid(INK3);
            ok_r3d_ellipsoid(P(x, y, 206), P(30, 40, 22), 8, 6);
            ok_r3d_material(WHITE, ++next_id, OK_R3D_UNLIT);
            ok_r3d_ellipsoid(P(x - 9, y + 14 + (f->eyes == 'u' ? 6 : f->eyes == 'd' ? -8 : 0), 227), P(9, 10, 5), 6, 4);
            if (f->eyes == 'h') { /* heavy lids */
                part(skin, OK_PART_HEAD, f->head_id);
                ok_r3d_ellipsoid(P(x, y + 22, 210), P(36, 26, 24), 8, 5);
            }
            if (f->eyes == 'd') { /* worried brows, high in the middle */
                solid(RGB(9, 5, 3));
                limb(x + s * 26, y + 50, 226, x - s * 18, y + 62, 228, 7);
            }
        }
    }
    solid(RGB(11, 6, 4)); /* nostrils */
    ok_r3d_ellipsoid(P(-11, 574, 234), P(7, 5, 5), 6, 4);
    ok_r3d_ellipsoid(P(11, 574, 234), P(7, 5, 5), 6, 4);
    solid(INK3);
    switch (f->mouth) {
    case 'O':
    case 'y': {
        int tall = f->mouth == 'y' ? 30 : 22;
        ok_r3d_ellipsoid(P(0, 512, 224), P(f->mouth == 'y' ? 22 : 30, tall, 12), 8, 6);
        ok_r3d_material(RGB(30, 12, 13), ++next_id, 0);
        ok_r3d_ellipsoid(P(0, 512 - tall / 2, 231), P(16, 9, 6), 6, 4); /* tongue */
        break;
    }
    case 'c': ok_r3d_ellipsoid(P(0, 514, 226), P(18, 14, 10), 6, 4); break;
    case '.': ok_r3d_ellipsoid(P(0, 514, 228), P(12, 10, 6), 6, 4); break;
    case '-': limb(-24, 516, 229, 24, 516, 229, 5); break;
    case 'w':
        limb(-28, 512, 228, -10, 522, 230, 5);
        limb(-10, 522, 230, 10, 512, 230, 5);
        limb(10, 512, 230, 28, 522, 228, 5);
        break;
    default: /* a little smile */
        limb(-32, 530, 226, -14, 514, 231, 6);
        limb(-14, 514, 231, 14, 514, 231, 6);
        limb(14, 514, 231, 32, 530, 226, 6);
    }
    ok_r3d_material(f->blush ? RGB(31, 15, 16) : RGB(31, 19, 17), ++next_id, 0); /* cheeks */
    ok_r3d_ellipsoid(P(-116, 590, 182), P(24, 12, 10), 6, 4);
    ok_r3d_ellipsoid(P(116, 590, 182), P(24, 12, 10), 6, 4);
}

void ok_monkey_draw(const ok_look *look, const uint16_t *skin, const ok_monkey_anim *a, ok_monkey_face *face)
{
    int s, i, lie = val(a, OK_SPRING_LIE), sq = val(a, OK_SPRING_SQUASH), hands, fur_id, mv = a->move;
    int bob = val(a, OK_SPRING_BOB), bare = look->wear == OK_WEAR_NONE || look->wear == OK_WEAR_GLASSES ||
                                            look->wear == OK_WEAR_SCARF || look->wear == OK_WEAR_SMILEY;
    int32_t x, y, z;
    lie = lie < 0 ? 0 : lie > 100 ? 100 : lie;
    memset(face, 0, sizeof *face);
    expression(look, a, face);
    next_id = 100;
    /* The tail, from its simulated chain (room coordinates). */
    if (a->tail_ready) {
        part_rows(skin, OK_PART_TAIL, 230, 0, 60);
        for (i = 0; i < 10; i++)
            limb(a->tail[i][0], a->tail[i][1], a->tail[i][2], a->tail[i + 1][0], a->tail[i + 1][1], a->tail[i + 1][2], 30 - i * 3 / 2);
    }
    /* Where it is: standing in the room, or on the bed. */
    x = a->x + (a->room.bed_x - a->x) * lie / 100;
    y = a->room.bed_y * lie / 100;
    z = a->z + (a->room.bed_z - a->z) * lie / 100;
    ok_r3d_push();
    ok_r3d_translate(P(x, y, z));
    if (lie < 60) { /* a soft shadow: three layers, darker to the middle, a little off to the right */
        int k, r = 270 - lie - bob / 2;
        for (k = 0; k < 3; k++) {
            ok_r3d_material(9 + k, k, OK_R3D_SHADOW);
            ok_r3d_disc(P(26, 9 + k, -14), H(r * (100 - k * 26) / 100), H(r * 74 * (100 - k * 26) / 10000), 16);
        }
    }
    ok_r3d_rotate_y(deg(a, OK_SPRING_YAW) * (100 - lie) / 100);
    if (lie) {
        ok_r3d_translate(P(0, 160, 0));
        ok_r3d_rotate_z(DEG(90) * lie / 100);
        ok_r3d_translate(P(0, -190, 0));
    }
    /* Legs: hip, knee and ankle, the feet kept on the floor while it bobs. */
    hands = ++next_id;
    fur_id = ++next_id;
    for (s = -1; s <= 1; s += 2) {
        int k = s > 0, thigh = DEG(6) + DEG(64) * lie / 100, knee = DEG(12) + DEG(88) * lie / 100, off, lift;
        gait_foot(a, k, &off, &lift);
        if (lie < 30)
            reach(val(a, k ? OK_SPRING_LEG_R : OK_SPRING_LEG_L) + off + 6,
                  188 + bob - 50 - val(a, k ? OK_SPRING_LIFT_R : OK_SPRING_LIFT_L) - lift, &thigh, &knee);
        ok_r3d_push();
        ok_r3d_translate(P(s * 72, 188 + bob, 0));
        ok_r3d_rotate_z(s * DEG(9)); /* a little bow-legged, so the knees show */
        ok_r3d_rotate_x(-thigh);
        part_rows(skin, OK_PART_LEG, fur_id, 0, 50);
        limb(0, -THIGH, 0, 0, 0, 0, 50);
        ok_r3d_translate(P(0, -THIGH, 0));
        ok_r3d_rotate_x(knee);
        ok_r3d_rotate_z(-s * DEG(18));
        part_rows(skin, OK_PART_LEG, fur_id, 50, 100);
        limb(0, -SHIN, 0, 0, 0, 0, 42);
        ok_r3d_translate(P(0, -SHIN, 0));
        ok_r3d_rotate_z(s * DEG(9));
        ok_r3d_rotate_x(thigh - knee); /* the foot stays level */
        part(skin, OK_PART_HANDS, hands);
        ok_r3d_ellipsoid(P(s * 6, -24, 30), P(56, 28, 86), 10, 6);
        ok_r3d_ellipsoid(P(-s * 36, -28, 84), P(20, 18, 26), 6, 4); /* the big toe, monkey-style */
        ok_r3d_pop();
    }
    /* The pelvis: bob, lean, roll and squash around the hips. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 170 + bob, 0));
    ok_r3d_rotate_x(deg(a, OK_SPRING_LEAN));
    ok_r3d_rotate_z(deg(a, OK_SPRING_ROLL));
    ok_r3d_scale(H(200 - sq), H(sq), H(200 - sq));
    ok_r3d_translate(P(0, -170, 0));
    part_rows(skin, OK_PART_TOP, ++next_id, 45, 100);
    ok_r3d_ellipsoid(P(0, 272, 10), P(170, 118, 146), 12, 8); /* a round tummy */
    /* The chest bends over the belly: breathing, sighs, a stretch. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 295, 0));
    ok_r3d_rotate_x(deg(a, OK_SPRING_CHEST));
    ok_r3d_translate(P(0, -295, 0));
    part_rows(skin, OK_PART_TOP, next_id, 0, 55);
    ok_r3d_ellipsoid(P(0, 362, 0), P(148, 112 + wave(a->ticks, 17, 3), 124), 12, 8); /* narrower shoulders */
    if (look->top == OK_TOP_HOODIE) {
        part(skin, OK_PART_HOOD, ++next_id);
        ok_r3d_ellipsoid(P(0, 440, -80), P(150, 72, 100), 10, 6);
    }
    if (look->wear == OK_WEAR_SCARF) {
        solid(RGB(28, 9, 8));
        ok_r3d_ellipsoid(P(0, 452, 0), P(158, 52, 140), 12, 5);
        solid(RGB(26, 7, 7));
        blob(70, 370, 128, 42, 90, 28);
    }
    /* Arms: shoulder, elbow, wrist, a hand with a thumb. */
    for (s = -1; s <= 1; s += 2) {
        int k = s > 0;
        if (k && mv == OK_MOVE_EAT && a->move_t < 165) { /* the banana to the mouth, by IK */
            int r = val(a, OK_SPRING_REACH), w[3];
            int32_t wrist[3];
            r = r < 0 ? 0 : r > 100 ? 100 : r;
            w[0] = 92 + (40 - 92) * r / 100, w[1] = 290 + (412 - 290) * r / 100, w[2] = 150 + (210 - 150) * r / 100;
            wrist[0] = w[0], wrist[1] = w[1], wrist[2] = w[2];
            arm_reach(skin, s, hands, ++next_id, wrist, a->bites);
            continue;
        }
        ok_r3d_push();
        ok_r3d_translate(P(s * 140, 410, 5));
        ok_r3d_rotate_z(s * deg(a, k ? OK_SPRING_RAISE_R : OK_SPRING_RAISE_L));
        ok_r3d_rotate_x(-deg(a, k ? OK_SPRING_FWD_R : OK_SPRING_FWD_L));
        part_rows(skin, OK_PART_SLEEVE, ++next_id, 0, 50);
        limb(s * 18, -82, 6, 0, 0, 0, 44);
        ok_r3d_translate(P(s * 18, -82, 6));
        ok_r3d_rotate_x(-deg(a, k ? OK_SPRING_ELBOW_R : OK_SPRING_ELBOW_L));
        ok_r3d_rotate_z(-s * deg(a, k ? OK_SPRING_ELBOW_R : OK_SPRING_ELBOW_L) / 2); /* and in, so the elbow shows from the front */
        part_rows(skin, OK_PART_SLEEVE, next_id, 50, 100);
        limb(s * 6, -76, 6, 0, 0, 0, 38);
        ok_r3d_translate(P(s * 6, -76, 6));
        hand(skin, s, hands);
        ok_r3d_pop();
    }
    if (mv == OK_MOVE_PLAY) {
        static uint16_t stripes[8];
        static const uint16_t c[4] = {RGB(29, 7, 8), RGB(31, 30, 27), RGB(6, 13, 28), RGB(31, 29, 8)};
        int h = val(a, OK_SPRING_BALL), j;
        for (j = 0; j < 8; j++) stripes[j] = c[j / 2];
        ok_r3d_push();
        {   /* tossed from one hand to the other in an arc over its head */
            int toss = a->move_t % 72, air = toss - 14, dir = (a->move_t / 72) % 2 ? -1 : 1, x = -dir * 120;
            if (air >= 0 && air < 46) x = -dir * 120 + dir * 240 * air / 46;
            else if (toss >= 60) x = dir * 120;
            ok_r3d_translate(P(x, 330 + h * 2, 180));
        }
        ok_r3d_rotate_x((int)(a->ticks * 90u));           /* spinning */
        ok_r3d_rotate_z(DEG(20));
        ok_r3d_texture(stripes, 8);
        ok_r3d_uv(0, 0, 8, 1);
        ok_r3d_material(RGB(29, 7, 8), ++next_id, OK_R3D_EDGE | OK_R3D_TEXTURED);
        ok_r3d_ellipsoid(P(0, 0, 0), P(70, 70, 70), 10, 7);
        ok_r3d_pop();
    }
    /* Head, turning on the neck. */
    ok_r3d_push();
    ok_r3d_translate(P(0, 450, 0));
    ok_r3d_rotate_y(deg(a, OK_SPRING_TURN));
    ok_r3d_rotate_x(deg(a, OK_SPRING_NOD));
    ok_r3d_rotate_z(deg(a, OK_SPRING_TILT));
    ok_r3d_translate(P(0, -450, 0));
    face->head_id = ++next_id;
    part(skin, OK_PART_HEAD, face->head_id);
    ok_r3d_ellipsoid(P(0, 632, -10), P(236, 224, 226), 12, 9);
    ok_r3d_ellipsoid(P(0, 728, 118), P(150, 38, 80), 10, 5);   /* brow */
    if (bare) { /* a tuft of hair */
        limb(0, 845, 20, -14, 880, 46, 15);
        limb(26, 840, 10, 40, 870, 30, 12);
        limb(-26, 840, 10, -44, 866, 26, 12);
    }
    if (look->wear != OK_WEAR_HEADPHONES)
        for (s = -1; s <= 1; s += 2) {
            ok_r3d_push();
            ok_r3d_translate(P(s * 215, 650, -10));
            ok_r3d_rotate_z(s * (deg(a, OK_SPRING_EAR) + wave(a->ticks, 29, 1) * DEG(2)));
            part(skin, OK_PART_EAR, face->head_id);
            blob(s * 22, -10, 0, 46, 86, 86);
            part(skin, OK_PART_EAR_IN, face->head_id + 50);
            blob(s * 42, -10, 16, 24, 56, 56);
            ok_r3d_pop();
        }
    face->mask_id = ++next_id;
    /* A heart-shaped face: two lobes round the eyes over one soft disc, and a
     * gentle muzzle low on it. */
    part(skin, OK_PART_EYES, face->mask_id);
    blob(-78, 650, 124, 92, 98, 96);
    blob(78, 650, 124, 92, 98, 96);
    ok_r3d_ellipsoid(P(0, 572, 118), P(150, 124, 116), 12, 8);
    part(skin, OK_PART_MUZZLE, face->mask_id);
    blob(0, 540, 160, 106, 72, 76);
    face3d(skin, face);
    wear(look->wear, a, face);
    for (i = 0; i < 2; i++) /* where glasses go */
        if (!ok_r3d_project(P(i ? 82 : -82, 652, 228), &face->eye_x[i], &face->eye_y[i], &face->eye_z[i])) face->eye_x[i] = -100;
    if (!ok_r3d_project(P(0, 900, 0), &face->top_x, &face->top_y, NULL)) face->top_x = -100;
    ok_r3d_pop();
    ok_r3d_pop();
    ok_r3d_pop();
    ok_r3d_pop();

}

/* ------------------------------------------------------------------ face */

static void sprite(uint16_t *frame, const ok_monkey_face *f, int cx, int cy, const char *const rows[], int n, int masked)
{
    int r, c, w = (int)strlen(rows[0]);
    for (r = 0; r < n; r++)
        for (c = 0; c < w; c++) {
            int x = cx - w / 2 + c, y = cy - n / 2 + r, id = ok_r3d_id_at(x, y);
            char k = rows[r][c];
            if (k == '.' || x < 0 || y < 0 || x >= OK_R3D_W || y >= OK_R3D_H) continue;
            if (masked && id != f->head_id && id != f->mask_id) continue;
            frame[y * OK_R3D_W + x] = k == '#' ? INK : k == 'o' ? WHITE : k == 'w' ? RGB(30, 30, 28) : k == 'k' ? RGB(1, 1, 2)
                                      : k == 'f' ? RGB(13, 8, 4) : k == 'p' ? BLUSH : k == 'h' ? RGB(30, 9, 12)
                                      : k == 'z' ? RGB(26, 27, 31) : k == 'n' ? RGB(14, 8, 6) : RGB(27, 11, 11);
        }
}

static const char *const glasses[] = {"..####..", ".#....#.", "#......#", "#......#", "#......#", "#......#",
                                      ".#....#.", "..####.."};
static const char *const bridge[] = {"###"};
static const char *const heart[] = {".hh.hh.", "hhhhhhh", "hhhhhhh", ".hhhhh.", "..hhh..", "...h..."};
static const char *const zed[] = {"zzzz", "..z.", ".z..", "zzzz"};
static const char *const zed_big[] = {"zzzzz", "...z.", "..z..", ".z...", "zzzzz"};

/* A face point counts only if nothing stands in front of it (the head turned
 * away, an arm or the mask across it). */
static int seen(int x, int y, int32_t z)
{
    int32_t d = ok_r3d_depth_at(x, y);
    return x > -50 && d && z >= d - d / 32;
}

/* The smiley's face: drawn in the mask's own plane, so it turns with it. */
static void smiley_face(uint16_t *frame, const ok_monkey_face *f)
{
    int64_t det = (int64_t)f->mask_ux * f->mask_vy - (int64_t)f->mask_uy * f->mask_vx;
    int r = (f->mask_ux < 0 ? -f->mask_ux : f->mask_ux) + (f->mask_uy < 0 ? -f->mask_uy : f->mask_uy) +
            (f->mask_vx < 0 ? -f->mask_vx : f->mask_vx) + (f->mask_vy < 0 ? -f->mask_vy : f->mask_vy) + 2, x, y;
    if (!f->smiley_id || !det) return;
    for (y = f->mask_y - r; y <= f->mask_y + r; y++)
        for (x = f->mask_x - r; x <= f->mask_x + r; x++) {
            int64_t dx = x - f->mask_x, dy = y - f->mask_y;
            /* Local coordinates, 100 = the rim: d = a U + b V. */
            int64_t a = (dx * f->mask_vy - dy * f->mask_vx) * 100 / det;
            int64_t b = (dy * f->mask_ux - dx * f->mask_uy) * 100 / det;
            int64_t e, rr;
            int ink = 0;
            if (ok_r3d_id_at(x, y) != f->smiley_id) continue;
            e = (a < 0 ? -a : a) - 34;
            if (e * e * 24 * 24 + (b - 28) * (b - 28) * 11 * 11 < 11 * 11 * 24 * 24) ink = 1; /* eyes */
            rr = a * a + (b - 4) * (b - 4);
            if (rr > 44 * 44 && rr < 60 * 60 && b < -10) ink = 1; /* the smile */
            if (ink) frame[y * OK_R3D_W + x] = INK;
        }
}

void ok_monkey_face_draw(uint16_t *frame, const ok_monkey_face *f)
{
    int i;
    smiley_face(frame, f);
    if (f->glasses && seen(f->eye_x[0], f->eye_y[0], f->eye_z[0]) && seen(f->eye_x[1], f->eye_y[1], f->eye_z[1])) {
        for (i = 0; i < 2; i++) sprite(frame, f, f->eye_x[i], f->eye_y[i], glasses, 8, 1);
        sprite(frame, f, (f->eye_x[0] + f->eye_x[1]) / 2, f->eye_y[0] - 1, bridge, 1, 1);
    }
    /* Hearts rise while it is petted; z's drift up while it sleeps. */
    if (f->hearts && f->top_x > -50)
        for (i = 0; i < 3; i++) {
            int t = (f->hearts + i * 40) % 120;
            if (t < 100) sprite(frame, f, f->top_x + (i - 1) * 14 + (t / 12 % 2 ? 1 : -1), f->top_y - t / 3, heart, 6, 0);
        }
    if (f->zzz && f->top_x > -50)
        for (i = 0; i < 2; i++) {
            int t = (f->zzz + i * 90) % 180;
            sprite(frame, f, f->top_x + 12 + t / 10, f->top_y - 4 - t / 5, i ? zed_big : zed, i ? 5 : 4, 0);
        }
}
