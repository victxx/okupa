#include "okupa_act.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static uint8_t pixels[OK_DRAWING_BYTES], out[OK_ACT_MAX + 4];
    static const uint8_t hello[] = {'O', 'K', 'P', 'A', 1, 0};
    ok_act act;
    size_t len;
    unsigned x, y;

    assert(ok_act_hello(out, sizeof out, &len) == OK_OK && len == 6 && !memcmp(out, hello, 6));
    assert(ok_act_decode(out, len, &act) == OK_OK && act.kind == OK_ACT_HELLO && !act.drawing);

    /* A diagonal and a border: pixel order is row-major, most significant bit first. */
    for (x = 0; x < 64; x++) {
        ok_drawing_set(pixels, x, x, 1);
        ok_drawing_set(pixels, x, 0, 1);
    }
    assert(pixels[0] == 0xff && pixels[8] == 0x40 && pixels[63 * 8 + 7] == 0x01);
    ok_drawing_set(pixels, 99, 0, 1); /* out of bounds: ignored */
    assert(ok_act_drawing(pixels, out, sizeof out, &len) == OK_OK && len == 518);
    assert(!memcmp(out, "OKPA\x01\x01", 6));
    assert(ok_act_decode(out, len, &act) == OK_OK && act.kind == OK_ACT_DRAWING && act.drawing == out + 6);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) assert(ok_drawing_get(act.drawing, x, y) == (x == y || y == 0));
    ok_drawing_set(pixels, 5, 0, 0);
    assert(!ok_drawing_get(pixels, 5, 0) && ok_drawing_get(pixels, 6, 0));

    /* Anything else is refused: wrong magic, version, kind or length. */
    assert(ok_act_decode(out, len - 1, &act) == OK_FORMAT);
    assert(ok_act_decode(out, 6, &act) == OK_FORMAT);          /* drawing without pixels */
    memcpy(out, hello, 6);
    assert(ok_act_decode(out, 7, &act) == OK_FORMAT);          /* hello with a tail */
    out[4] = 2; assert(ok_act_decode(out, 6, &act) == OK_FORMAT);
    out[4] = 1; out[5] = 9; assert(ok_act_decode(out, 6, &act) == OK_FORMAT);
    out[5] = 0; out[0] = 'P'; assert(ok_act_decode(out, 6, &act) == OK_FORMAT);
    assert(ok_act_decode(out, 3, &act) == OK_FORMAT);
    assert(ok_act_drawing(pixels, out, 517, &len) == OK_RANGE);
    assert(ok_act_hello(out, 5, &len) == OK_RANGE);
    /* A letter: plain text, 1..140 bytes. */
    assert(ok_act_letter("Dear Victor, I saw lanterns in my dream.", out, sizeof out, &len) == OK_OK && len == 6 + 40);
    assert(out[5] == 2 && ok_act_decode(out, len, &act) == OK_OK && act.kind == OK_ACT_LETTER);
    assert(act.text_len == 40 && !memcmp(act.text, "Dear Victor", 11) && !act.drawing);
    assert(ok_act_letter("", out, sizeof out, &len) == OK_ARGUMENT);
    assert(ok_act_letter("caf\xc3\xa9", out, sizeof out, &len) == OK_ARGUMENT);
    {
        char longer[142];
        memset(longer, 'a', 141);
        longer[141] = 0;
        assert(ok_act_letter(longer, out, sizeof out, &len) == OK_ARGUMENT);
        longer[140] = 0;
        assert(ok_act_letter(longer, out, sizeof out, &len) == OK_OK && len == 146);
    }
    out[10] = 7; /* a control byte in the text */
    assert(ok_act_decode(out, len, &act) == OK_FORMAT);
    assert(ok_act_decode(out, 6, &act) == OK_FORMAT); /* an empty letter is not one */
    puts("act: hello, drawing and letter encode/decode, pixel order, strict shapes");
    return 0;
}
