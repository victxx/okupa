/* Just enough of <string.h> for the game's renderer in WebAssembly (no libc there). */
#ifndef OKUPA_WASM_STRING_H
#define OKUPA_WASM_STRING_H
#include <stddef.h>
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
#endif
