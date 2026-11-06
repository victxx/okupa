#include "okupa_json.h"
#include "okupa_mind.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ok_result parse(const char *content, ok_mind *d)
{
    static char body[2048];
    snprintf(body, sizeof body, "{\"id\":\"c\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"%s\"}}]}", content);
    return ok_mind_parse(body, strlen(body), d);
}

int main(void)
{
    static char out[2048], text[512];
    ok_json root, v, m;
    ok_mind d;
    ok_pet p;
    size_t n;

    /* The request is valid JSON and carries what the okupa observes. */
    ok_pet_init(&p, 1800000000);
    assert(ok_mind_request("glm-5.3", &p, 15, 20, "player drew a lamp", 95, out, sizeof out, &n) == OK_OK);
    assert(ok_json_parse(out, n, &root) == OK_OK && ok_json_member(&root, "model", &v) == OK_OK);
    assert(ok_json_member(&root, "messages", &v) == OK_OK && ok_json_index(&v, 1, &m) == OK_OK);
    assert(ok_json_member(&m, "content", &v) == OK_OK && ok_json_string(&v, text, sizeof text, &n) == OK_OK);
    assert(strstr(text, "\"time\":\"15:20\"") && strstr(text, "\"food\":70") && strstr(text, "drew a lamp"));
    assert(ok_json_parse(text, n, &root) == OK_OK);
    assert(ok_mind_request("glm-5.3", &p, 15, 20, "a \"quote\"", 1, out, sizeof out, &n) == OK_ARGUMENT);
    assert(ok_mind_request("glm-5.3", &p, 15, 20, NULL, 1, out, 100, &n) == OK_RANGE);

    /* Plain, fenced and chatty answers are read; only the closed sets count. */
    assert(parse("{\\\"activity\\\":\\\"wave\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"Nice lamp!\\\"}", &d) == OK_OK);
    assert(d.activity == OK_ACT_WAVE && d.mood == OK_FEEL_HAPPY && !strcmp(d.line, "Nice lamp!"));
    assert(parse("```json\\n{\\\"activity\\\":\\\"stare_out_window\\\",\\\"mood\\\":\\\"curious\\\",\\\"line\\\":\\\"Is it raining?\\\"}\\n```", &d) == OK_OK);
    assert(d.activity == OK_ACT_WINDOW && !strcmp(ok_mind_mood_name(d.mood), "curious"));
    assert(parse("Sure! {\\\"activity\\\":\\\"eat\\\",\\\"mood\\\":\\\"hungry\\\",\\\"line\\\":\\\"Snack time. This line is far too long to fit on the screen\\\"}", &d) == OK_OK);
    assert(strlen(d.line) == OK_MIND_LINE);
    assert(parse("{\\\"activity\\\":\\\"rob_a_bank\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"hi\\\"}", &d) == OK_FORMAT);
    assert(parse("{\\\"activity\\\":\\\"nap\\\",\\\"mood\\\":\\\"furious\\\",\\\"line\\\":\\\"hi\\\"}", &d) == OK_FORMAT);
    assert(parse("{\\\"activity\\\":\\\"nap\\\",\\\"mood\\\":\\\"sleepy\\\"}", &d) == OK_FORMAT);
    assert(parse("{\\\"activity\\\":\\\"nap\\\",\\\"mood\\\":\\\"sleepy\\\",\\\"line\\\":\\\"\\\\u00e9\\\"}", &d) == OK_FORMAT);
    assert(parse("I would rather not.", &d) == OK_FORMAT);
    assert(ok_mind_parse("{\"error\":\"rate limited\"}", 25, &d) == OK_FORMAT);
    puts("mind: request carries the observation; answers outside the closed sets are refused");
    return 0;
}
