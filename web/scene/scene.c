/* The console's top screen for the web: the same attic, okupa and gallery code the DSi runs
 * (game/src/house.c, monkey.c, r3d.c), compiled to WebAssembly. 256x192, DS colours turned
 * into RGBA for a canvas. No heap, no libc beyond the few helpers below. */
#include "okupa_house.h"
#include "okupa_act.h"
#include <string.h>

void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d; const unsigned char *q = s;
    if (p < q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

#define W 256
#define H 192
static uint16_t frame[W * H], skin[OK_SKIN * OK_SKIN];
static uint32_t rgba[W * H];
static uint8_t pic[OK_DRAWING_BYTES], pics[OK_HOUSE_FRAMES][OK_DRAWING_BYTES];
static ok_monkey_anim anim;
static ok_look look;
static ok_monkey_face face;
static int hang_slot = -1, hang_t;

uint8_t *scene_pic(void) { return pic; }
uint32_t *scene_rgba(void) { return rgba; }

void scene_init(uint32_t seed, int place, int wear, int top, int colour)
{
    ok_monkey_room room;
    ok_house_place(place);
    if (place == OK_PLACE_GARDEN) ok_house_garden(&room);
    else ok_house_room(&room);
    ok_monkey_init(&anim, &room, 0, seed);
    look.wear = (uint8_t)wear; look.top = (uint8_t)top; look.colour = (uint8_t)colour;
    ok_monkey_skin(&look, skin);
}

/* Hangs the 64x64 one-bit picture written at scene_pic() in a slot; animate it if asked. */
void scene_hang(int slot, int animate)
{
    if (slot < 0 || slot >= OK_HOUSE_FRAMES) return;
    memcpy(pics[slot], pic, sizeof pic);
    ok_house_frame(slot, pics[slot]);
    if (animate && slot < 6) { hang_slot = slot; hang_t = 1; }
}

void scene_event(int event) { ok_monkey_event(&anim, event); }
void scene_move(int move) { ok_monkey_direct(&anim, move); }
void scene_step(int mood)
{
    ok_monkey_step(&anim, mood, look.wear);
    if (hang_t && ++hang_t > OK_HOUSE_HANG_TICKS) { hang_t = 0; hang_slot = -1; }
}

uint32_t *scene_draw(int hour, int minute, int asleep)
{
    int i;
    ok_house_hanging(hang_t ? hang_slot : -1, hang_t);
    ok_house_draw(frame, hour, minute, asleep, NULL, &look, skin, &anim, &face);
    for (i = 0; i < W * H; i++) {
        uint32_t c = frame[i], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
        rgba[i] = 0xff000000u | (b << 16) | (g << 8) | r; /* little-endian RGBA bytes */
    }
    return rgba;
}
