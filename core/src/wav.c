#include "okupa_wav.h"
#include <string.h>

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void ok_wav_header(uint8_t out[OK_WAV_HEADER], uint32_t rate, uint32_t samples)
{
    memcpy(out, "RIFF", 4);
    le32(out + 4, 36 + samples);
    memcpy(out + 8, "WAVEfmt ", 8);
    le32(out + 16, 16);   /* fmt chunk size */
    out[20] = 1;          /* PCM */
    out[21] = 0;
    out[22] = 1;          /* mono */
    out[23] = 0;
    le32(out + 24, rate);
    le32(out + 28, rate); /* bytes a second: one byte a sample */
    out[32] = 1;          /* block align */
    out[33] = 0;
    out[34] = 8;          /* bits a sample */
    out[35] = 0;
    memcpy(out + 36, "data", 4);
    le32(out + 40, samples);
}

int ok_wav_unsigned8(uint8_t *s, size_t n)
{
    size_t i, mid = 0;
    for (i = 0; i < n; i++) mid += s[i] >= 64 && s[i] < 192;
    if (mid * 2 >= n) return 0; /* mostly around 128: already unsigned */
    for (i = 0; i < n; i++) s[i] ^= 0x80;
    return 1;
}

int ok_wav_level8(const uint8_t *s, size_t n)
{
    size_t i;
    uint32_t sum = 0;
    if (!n) return 0;
    for (i = 0; i < n; i++) sum += (uint32_t)(s[i] >= 128 ? s[i] - 128 : 128 - s[i]);
    return (int)(sum / n);
}
