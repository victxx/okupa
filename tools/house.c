/* The attic through the day, and a short film of the okupa living in it,
 * drawn with the game's own code. Host only.
 *   build/okupa-house <out dir> [frames dir]
 * house-<time>.png: stills; with a frames dir, one PNG per 1/20 s of the film. */
#include "okupa_house.h"
#include "png.h"

static uint16_t frame[OK_R3D_W * OK_R3D_H], skin[OK_SKIN * OK_SKIN];
static uint8_t drawing[512];

/* A small sun with a smile, as if the player had drawn it. */
static void sketch(void)
{
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int dx = x - 32, dy = y - 30, d = dx * dx + dy * dy;
            int ink = (d > 13 * 13 && d < 15 * 15) || (d > 19 * 19 && d < 26 * 26 && (dx == 0 || dy == 0 || dx == dy || dx == -dy)) ||
                      ((dx == -5 || dx == 5) && dy > -5 && dy < -1) || (dy == 5 && dx > -5 && dx < 5) || (dy == 4 && (dx == -5 || dx == 5));
            if (ink) drawing[y * 8 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
        }
}

static void still(const char *dir, const char *name, int hour, int minute, int mood, int ticks)
{
    static ok_monkey_anim a;
    ok_monkey_room room;
    ok_monkey_face face;
    ok_look look = {OK_WEAR_NONE, OK_TOP_HOODIE, 0};
    char path[512];
    int i;
    ok_house_room(&room);
    ok_monkey_init(&a, &room, mood == OK_POSE_ASLEEP, 11);
    a.next_idle = 100000;
    for (i = 0; i < ticks; i++) ok_monkey_step(&a, mood, look.wear);
    ok_monkey_skin(&look, skin);
    ok_house_draw(frame, hour, minute, mood == OK_POSE_ASLEEP, drawing, &look, skin, &a, &face);
    snprintf(path, sizeof path, "%s/house-%s.png", dir, name);
    png_write(path, frame, OK_R3D_W, OK_R3D_H, 2);
}

/* The film: a morning of moods and moves, then bedtime. */
static void film(const char *dir)
{
    static ok_monkey_anim a;
    static const struct {
        int at, what, arg;
    } script[] = {
        {0, 0, OK_MOVE_LOOK}, {200, 1, OK_EVENT_FEED}, {440, 1, OK_EVENT_PET}, {620, 1, OK_EVENT_PLAY},
        {860, 0, OK_MOVE_PEEK}, {1100, 0, OK_MOVE_WANDER}, {1560, 0, OK_MOVE_HOP}, {1640, 2, 0}, {2240, 3, 0},
    };
    ok_monkey_room room;
    ok_monkey_face face;
    ok_look look = {OK_WEAR_NONE, OK_TOP_HOODIE, 0};
    char path[512];
    int t, k = 0, mood = OK_POSE_IDLE, n = 0;
    ok_house_room(&room);
    ok_monkey_init(&a, &room, 0, 5);
    ok_monkey_skin(&look, skin);
    for (t = 0; t < 2240; t++) {
        while (k < (int)(sizeof script / sizeof script[0]) && script[k].at == t) {
            if (script[k].what == 0) {
                a.move = script[k].arg;
                a.move_t = 0;
                a.stage = 0;
                a.look = 1;
            } else if (script[k].what == 1) {
                ok_monkey_event(&a, script[k].arg);
            } else if (script[k].what == 2) {
                mood = OK_POSE_ASLEEP;
            }
            k++;
        }
        a.next_idle = 100000;
        ok_monkey_step(&a, mood, look.wear);
        if (t % 3) continue;
        ok_house_draw(frame, mood == OK_POSE_ASLEEP ? 22 : 10, 30, mood == OK_POSE_ASLEEP && a.move == OK_MOVE_SLEEP,
                      drawing, &look, skin, &a, &face);
        snprintf(path, sizeof path, "%s/f%04d.png", dir, n++);
        png_write(path, frame, OK_R3D_W, OK_R3D_H, 1);
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/preview";
    sketch();
    still(dir, "dawn", 6, 40, OK_POSE_SLEEPY, 200);
    still(dir, "day", 10, 30, OK_POSE_IDLE, 60);
    still(dir, "golden", 18, 50, OK_POSE_HAPPY, 60);
    still(dir, "night", 22, 15, OK_POSE_IDLE, 60);
    still(dir, "asleep", 23, 30, OK_POSE_ASLEEP, 60);
    if (argc > 2) film(argv[2]);
    return 0;
}
