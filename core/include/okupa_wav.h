#ifndef OKUPA_WAV_H
#define OKUPA_WAV_H

#include "okupa.h"

/* What the microphone heard, as a WAV file for speech to text: 8-bit mono
 * PCM, which WAV stores unsigned (silence is 128). */
#define OK_WAV_HEADER 44

void ok_wav_header(uint8_t out[OK_WAV_HEADER], uint32_t rate, uint32_t samples);
/* Microphones differ in whether their 8-bit samples are signed (silence 0)
 * or unsigned (silence 128); this tells which from where they sit and makes
 * them unsigned. Returns 1 if it flipped them. */
int ok_wav_unsigned8(uint8_t *samples, size_t n);
/* How loud it was, 0..127: the mean distance from silence. */
int ok_wav_level8(const uint8_t *samples, size_t n);

#endif
