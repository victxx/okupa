#include "okupa_act.h"
#include <string.h>

static const uint8_t magic[4] = {'O', 'K', 'P', 'A'};

static ok_result header(uint8_t kind, size_t payload, uint8_t *out, size_t cap, size_t *len)
{
    if (!out || !len) return OK_ARGUMENT;
    if (cap < OK_ACT_HEADER + payload) return OK_RANGE;
    memcpy(out, magic, 4);
    out[4] = 1;
    out[5] = kind;
    *len = OK_ACT_HEADER + payload;
    return OK_OK;
}

ok_result ok_act_hello(uint8_t *out, size_t cap, size_t *len)
{
    return header(OK_ACT_HELLO, 0, out, cap, len);
}

ok_result ok_act_drawing(const uint8_t pixels[OK_DRAWING_BYTES], uint8_t *out, size_t cap, size_t *len)
{
    ok_result r;
    if (!pixels) return OK_ARGUMENT;
    r = header(OK_ACT_DRAWING, OK_DRAWING_BYTES, out, cap, len);
    if (r == OK_OK) memcpy(out + OK_ACT_HEADER, pixels, OK_DRAWING_BYTES);
    return r;
}

ok_result ok_act_photo(const uint8_t tones[OK_PHOTO_BYTES], uint8_t *out, size_t cap, size_t *len)
{
    ok_result r;
    if (!tones) return OK_ARGUMENT;
    r = header(OK_ACT_PHOTO, OK_PHOTO_BYTES, out, cap, len);
    if (r == OK_OK) memcpy(out + OK_ACT_HEADER, tones, OK_PHOTO_BYTES);
    return r;
}

static int printable(const uint8_t *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] >= 0x7f) return 0;
    return 1;
}

ok_result ok_act_letter(const char *text, uint8_t *out, size_t cap, size_t *len)
{
    size_t n;
    ok_result r;
    if (!text) return OK_ARGUMENT;
    n = strlen(text);
    if (!n || n > OK_LETTER_MAX || !printable((const uint8_t *)text, n)) return OK_ARGUMENT;
    r = header(OK_ACT_LETTER, n, out, cap, len);
    if (r == OK_OK) memcpy(out + OK_ACT_HEADER, text, n);
    return r;
}

ok_result ok_act_decode(const uint8_t *data, size_t n, ok_act *act)
{
    if (!data || !act) return OK_ARGUMENT;
    act->drawing = NULL;
    act->photo = NULL;
    act->text = NULL;
    act->text_len = 0;
    if (n < OK_ACT_HEADER || memcmp(data, magic, 4) || data[4] != 1) return OK_FORMAT;
    act->kind = data[5];
    if (act->kind == OK_ACT_HELLO && n == OK_ACT_HEADER) return OK_OK;
    if (act->kind == OK_ACT_DRAWING && n == OK_ACT_HEADER + OK_DRAWING_BYTES) {
        act->drawing = data + OK_ACT_HEADER;
        return OK_OK;
    }
    if (act->kind == OK_ACT_PHOTO && n == OK_ACT_HEADER + OK_PHOTO_BYTES) {
        act->photo = data + OK_ACT_HEADER;
        return OK_OK;
    }
    if (act->kind == OK_ACT_LETTER && n > OK_ACT_HEADER && n <= OK_ACT_HEADER + OK_LETTER_MAX &&
        printable(data + OK_ACT_HEADER, n - OK_ACT_HEADER)) {
        act->text = (const char *)data + OK_ACT_HEADER;
        act->text_len = n - OK_ACT_HEADER;
        return OK_OK;
    }
    return OK_FORMAT;
}

int ok_drawing_get(const uint8_t pixels[OK_DRAWING_BYTES], unsigned x, unsigned y)
{
    if (x >= OK_DRAWING_SIDE || y >= OK_DRAWING_SIDE) return 0;
    return (pixels[y * 8 + x / 8] >> (7 - x % 8)) & 1;
}

void ok_drawing_set(uint8_t pixels[OK_DRAWING_BYTES], unsigned x, unsigned y, int ink)
{
    uint8_t bit;
    if (x >= OK_DRAWING_SIDE || y >= OK_DRAWING_SIDE) return;
    bit = (uint8_t)(0x80 >> (x % 8));
    if (ink) pixels[y * 8 + x / 8] |= bit;
    else pixels[y * 8 + x / 8] &= (uint8_t)~bit;
}

unsigned ok_photo_get(const uint8_t tones[OK_PHOTO_BYTES], unsigned x, unsigned y)
{
    if (x >= OK_DRAWING_SIDE || y >= OK_DRAWING_SIDE) return 0;
    return (tones[y * 16 + x / 4] >> (6 - 2 * (x % 4))) & 3;
}

void ok_photo_set(uint8_t tones[OK_PHOTO_BYTES], unsigned x, unsigned y, unsigned tone)
{
    unsigned shift;
    if (x >= OK_DRAWING_SIDE || y >= OK_DRAWING_SIDE) return;
    shift = 6 - 2 * (x % 4);
    tones[y * 16 + x / 4] = (uint8_t)((tones[y * 16 + x / 4] & ~(3u << shift)) | ((tone & 3u) << shift));
}
