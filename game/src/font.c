#include "okupa_font.h"

/* The upstream file stores values above 127 in plain char; the bit patterns
 * are what we read, so silence the conversion warning instead of editing it. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wconstant-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Woverflow"
#endif
#include "font8x8_basic.h"

const uint8_t (*ok_font(void))[8]
{
    return (const uint8_t (*)[8])font8x8_basic;
}
