#include "okupa_wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t h[OK_WAV_HEADER], s[1000];
    size_t i;
    ok_wav_header(h, 16000, 1000);
    assert(!memcmp(h, "RIFF", 4) && h[4] == 0x0c && h[5] == 0x04 && !memcmp(h + 8, "WAVEfmt ", 8));
    assert(h[20] == 1 && h[22] == 1 && h[24] == 0x80 && h[25] == 0x3e && h[34] == 8);
    assert(!memcmp(h + 36, "data", 4) && h[40] == 0xe8 && h[41] == 0x03);

    /* Unsigned samples near 128 stay; signed ones near 0 are flipped. */
    for (i = 0; i < sizeof s; i++) s[i] = (uint8_t)(128 + (i % 21) - 10);
    assert(!ok_wav_unsigned8(s, sizeof s) && s[0] == 118 && ok_wav_level8(s, sizeof s) == 5);
    for (i = 0; i < sizeof s; i++) s[i] = (uint8_t)((i % 21) - 10); /* -10..10 as bytes */
    assert(ok_wav_unsigned8(s, sizeof s) == 1 && s[0] == 118 && s[10] == 128);
    memset(s, 128, sizeof s);
    assert(ok_wav_level8(s, sizeof s) == 0);
    puts("wav: header, signed or unsigned 8-bit samples, loudness");
    return 0;
}
