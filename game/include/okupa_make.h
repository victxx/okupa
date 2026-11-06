#ifndef OKUPA_MAKE_H
#define OKUPA_MAKE_H

#include "okupa.h"
#include "okupa_mind.h"

/* Things the okupa makes with its mind on 0G: small 3D objects out of boxes
 * and its own drawings out of lines and circles. Both are asked of the model
 * as closed, bounded JSON and checked here; nothing outside the grid, the
 * palette or the counts gets through. */

/* An object: up to 16 boxes on a 16-cube grid (y up, origin on the floor),
 * each {x, y, z, w, h, d, colour} with colour one of OK_MAKE_COLOURS. */
#define OK_MAKE_BOXES 16
#define OK_MAKE_COLOURS 16
typedef struct {
    char name[21];
    uint8_t box[OK_MAKE_BOXES][7];
    int count;
} ok_object;

const char *ok_make_colour_name(int colour); /* "red", "orange", ... */
ok_result ok_make_object_request(const char *model, const char *what, char *out, size_t cap, size_t *len);
ok_result ok_make_object_parse(const char *body, size_t n, ok_object *o);

/* A drawing: up to 40 lines and 8 circles on the 64x64 sketchbook grid,
 * drawn into pixels (okupa_act.h format, 1 = ink). */
ok_result ok_make_drawing_request(const char *model, const char *subject, char *out, size_t cap, size_t *len);
ok_result ok_make_drawing_parse(const char *body, size_t n, uint8_t pixels[512], char name[21]);

#endif
