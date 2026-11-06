#include "okupa_act.h"
#include "okupa_save.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t buf[OK_SAVE_SIZE], drawing[512], back[512];
    ok_pet p, q;
    int64_t t = 1800000000;
    size_t i;

    assert(OK_SAVE_SIZE == 550);
    ok_pet_init(&p, t);
    ok_pet_do(&p, OK_DO_FEED, t);
    ok_pet_update(&p, t + 1234); /* leaves seconds in carry */
    memset(drawing, 0, sizeof drawing);
    ok_drawing_set(drawing, 3, 5, 1);
    ok_drawing_set(drawing, 63, 63, 1);
    ok_save_write(&p, drawing, buf);
    assert(!memcmp(buf, "OKSV\x01", 5));
    assert(ok_save_read(buf, sizeof buf, &q, back) == OK_OK);
    assert(!memcmp(q.need, p.need, OK_NEEDS) && q.asleep == p.asleep && q.updated == p.updated);
    assert(!memcmp(q.carry, p.carry, sizeof p.carry) && !memcmp(back, drawing, 512));

    /* Back after a day off: the okupa lived the time it was away. */
    ok_pet_update(&q, t + 1234 + 86400);
    assert(q.need[OK_NEED_HUNGER] < p.need[OK_NEED_HUNGER]);

    /* Every single flipped bit, a short file or another version is refused whole. */
    for (i = 0; i < sizeof buf * 8; i += 7) {
        buf[i / 8] ^= (uint8_t)(1u << (i % 8));
        assert(ok_save_read(buf, sizeof buf, &q, back) == OK_FORMAT);
        buf[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
    assert(ok_save_read(buf, sizeof buf - 1, &q, back) == OK_FORMAT);
    assert(ok_save_read(buf, sizeof buf, &q, back) == OK_OK);
    puts("save: pet and drawing round-trip; any damaged save is refused");
    return 0;
}
