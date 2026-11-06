#ifndef OKUPA_JSON_H
#define OKUPA_JSON_H

#include "okupa.h"

/* Read-only JSON in place, no allocation: values are spans of the input.
 * Enough to pick fields out of an inference answer; nesting is capped. */
typedef struct {
    const char *at;
    size_t len;
} ok_json;

/* The whole text must be exactly one value (whitespace around it allowed). */
ok_result ok_json_parse(const char *text, size_t n, ok_json *value);
/* Member of an object by key (keys compared after unescaping is not needed:
 * only plain ASCII keys are looked up). OK_RANGE if absent. */
ok_result ok_json_member(const ok_json *object, const char *key, ok_json *value);
ok_result ok_json_index(const ok_json *array, size_t i, ok_json *value);
/* A string value, unescaped to UTF-8 and NUL-terminated. */
ok_result ok_json_string(const ok_json *value, char *out, size_t cap, size_t *len);

#endif
