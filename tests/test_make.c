#include "okupa_act.h"
#include "okupa_json.h"
#include "okupa_make.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ok_result object(const char *content, ok_object *o)
{
    static char body[4096];
    snprintf(body, sizeof body, "{\"choices\":[{\"message\":{\"content\":\"%s\"}}]}", content);
    return ok_make_object_parse(body, strlen(body), o);
}

static ok_result drawing(const char *content, uint8_t px[512], char name[21])
{
    static char body[4096];
    snprintf(body, sizeof body, "{\"choices\":[{\"message\":{\"content\":\"%s\"}}]}", content);
    return ok_make_drawing_parse(body, strlen(body), px, name);
}

int main(void)
{
    static char out[8192];
    uint8_t px[512];
    char name[21];
    ok_object o;
    ok_json root;
    size_t n;
    int x, ink = 0;

    /* The requests are canonical JSON. */
    assert(ok_make_object_request("0GM", "a little lamp", out, sizeof out, &n) == OK_OK);
    assert(ok_json_parse(out, n, &root) == OK_OK && !strncmp(out, "{\"chat_template_kwargs\":", 24));
    assert(strstr(out, "Make a little lamp.") && strstr(out, "16x16x16"));
    assert(ok_make_drawing_request("0GM", "a cat", out, sizeof out, &n) == OK_OK && strstr(out, "Draw a cat."));

    /* An object: boxes kept inside the grid, unknown colours and bad boxes skipped. */
    assert(object("```json\\n{\\\"name\\\":\\\"Desk lamp\\\",\\\"boxes\\\":[[6,0,6,4,1,4,\\\"grey\\\"],"
                  "[7,1,7,2,9,2,\\\"black\\\"],[14,10,5,6,3,6,\\\"yellow\\\"],[1,1,1,1,1,1,\\\"glitter\\\"],[1,2]]}\\n```",
                  &o) == OK_OK);
    assert(o.count == 3 && !strcmp(o.name, "Desk lamp"));
    assert(o.box[0][0] == 6 && o.box[0][6] == 14 && !strcmp(ok_make_colour_name(o.box[1][6]), "black"));
    assert(o.box[2][0] == 14 && o.box[2][3] == 2 && o.box[2][1] + o.box[2][4] <= 16); /* clamped */
    assert(object("{\\\"name\\\":\\\"nothing\\\",\\\"boxes\\\":[]}", &o) == OK_FORMAT);

    /* A drawing: lines and circles become ink; outside the page is refused. */
    assert(drawing("{\\\"name\\\":\\\"Sun\\\",\\\"lines\\\":[[0,0,63,63],[70,0,1,1]],\\\"circles\\\":[[32,32,10],[5,5,99]]}", px,
                   name) == OK_OK);
    assert(!strcmp(name, "Sun") && ok_drawing_get(px, 0, 0) && ok_drawing_get(px, 63, 63) && ok_drawing_get(px, 31, 31));
    assert(ok_drawing_get(px, 42, 32) && ok_drawing_get(px, 32, 22) && !ok_drawing_get(px, 10, 50));
    for (x = 0; x < 512; x++) ink += __builtin_popcount(px[x]);
    assert(ink > 100 && ink < 200);
    assert(drawing("{\\\"lines\\\":[]}", px, name) == OK_FORMAT);
    puts("make: canonical requests; objects kept in the grid; drawings in ink");
    return 0;
}
