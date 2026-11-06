#ifndef OKUPA_H
#define OKUPA_H

#include <stddef.h>
#include <stdint.h>

/* Portable core: C99, no heap, no I/O. Every function reports through this. */
typedef enum {
    OK_OK = 0,
    OK_ARGUMENT,  /* bad pointer or size */
    OK_RANGE,     /* value or buffer out of bounds */
    OK_FORMAT,    /* malformed input */
    OK_CRYPTO     /* a primitive refused the input */
} ok_result;

const char *ok_result_name(ok_result r);

/* Overwrite secrets in a way the compiler may not optimise away. */
void ok_wipe(void *p, size_t n);

/* Optional: called now and then inside long computations (checking a block
 * body) so a platform with cooperative threads can let others run. */
extern void (*ok_yield)(void);

#endif
