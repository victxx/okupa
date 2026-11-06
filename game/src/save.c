#include "okupa_save.h"
#include "okupa_eth.h"
#include <string.h>

static void put(uint8_t *p, uint64_t v, int n)
{
    int i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t get(const uint8_t *p, int n)
{
    uint64_t v = 0;
    while (n--) v = v << 8 | p[n];
    return v;
}

void ok_save_write(const ok_pet *p, const uint8_t drawing[512], uint8_t out[OK_SAVE_SIZE])
{
    uint8_t *q = out;
    int i;
    memcpy(q, "OKSV", 4);
    q[4] = OK_SAVE_VERSION;
    q += 5;
    memcpy(q, p->need, OK_NEEDS);
    q += OK_NEEDS;
    *q++ = p->asleep;
    put(q, (uint64_t)p->updated, 8);
    q += 8;
    for (i = 0; i < OK_NEEDS; i++, q += 4) put(q, p->carry[i], 4);
    memcpy(q, drawing, 512);
    q += 512;
    put(q, ok_crc32(0, out, (size_t)(q - out)), 4);
}

ok_result ok_save_read(const uint8_t *in, size_t n, ok_pet *p, uint8_t drawing[512])
{
    ok_pet pet;
    const uint8_t *q = in + 5;
    int i;
    if (!in || !p || !drawing) return OK_ARGUMENT;
    if (n != OK_SAVE_SIZE || memcmp(in, "OKSV", 4) || in[4] != OK_SAVE_VERSION) return OK_FORMAT;
    if (get(in + OK_SAVE_SIZE - 4, 4) != ok_crc32(0, in, OK_SAVE_SIZE - 4)) return OK_FORMAT;
    memset(&pet, 0, sizeof pet);
    memcpy(pet.need, q, OK_NEEDS);
    q += OK_NEEDS;
    pet.asleep = *q++;
    pet.updated = (int64_t)get(q, 8);
    q += 8;
    for (i = 0; i < OK_NEEDS; i++, q += 4) pet.carry[i] = (uint32_t)get(q, 4);
    for (i = 0; i < OK_NEEDS; i++)
        if (pet.need[i] > OK_NEED_MAX) return OK_FORMAT;
    if (pet.asleep > 1) return OK_FORMAT;
    *p = pet;
    memcpy(drawing, q, 512);
    return OK_OK;
}
