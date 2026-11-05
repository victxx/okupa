#include "okupa.h"

const char *ok_result_name(ok_result r)
{
    switch (r) {
    case OK_OK: return "ok";
    case OK_ARGUMENT: return "argument";
    case OK_RANGE: return "range";
    case OK_FORMAT: return "format";
    case OK_CRYPTO: return "crypto";
    }
    return "unknown";
}

void ok_wipe(void *p, size_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (n--) *b++ = 0;
}
