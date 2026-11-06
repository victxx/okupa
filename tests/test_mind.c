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
    static char out[8192], text[2048];
    char memory[OK_MIND_LINE + 24];
    ok_json root, v, m;
    ok_mind_obs obs;
    ok_mind d;
    ok_pet p;
    size_t n;

    /* The request is valid JSON and carries what the okupa notices, with its
     * own recent decisions as memory; odd text is escaped or dropped. */
    ok_pet_init(&p, 1800000000);
    memset(&obs, 0, sizeof obs);
    obs.pet = &p;
    obs.hour = 15;
    obs.minute = 20;
    obs.seconds_since_player = 7;
    obs.player_did = "drew a lamp";
    obs.heard = "hola \"okupa\", \xc2\xbfqu\xc3\xa9 tal?";
    obs.saw = "a person with glasses";
    obs.recent[0] = "wave: Hey there!";
    obs.recent[1] = "look";
    obs.recent_count = 2;
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_OK);
    assert(ok_json_parse(out, n, &root) == OK_OK && ok_json_member(&root, "model", &v) == OK_OK);
    assert(!strncmp(out, "{\"chat_template_kwargs\":", 24)); /* keys sorted, as Go re-encodes it */
    assert(ok_json_member(&root, "messages", &v) == OK_OK && ok_json_index(&v, 1, &m) == OK_OK);
    assert(ok_json_member(&m, "content", &v) == OK_OK && ok_json_string(&v, text, sizeof text, &n) == OK_OK);
    assert(ok_json_parse(text, n, &root) == OK_OK);
    assert(strstr(text, "\"time\":\"15:20\"") && strstr(text, "\"food\":70") && strstr(text, "drew a lamp"));
    assert(strstr(text, "\"player_said\":\"hola \\\"okupa\\\", qu tal?\""));
    assert(strstr(text, "\"okupa_sees\":\"a person with glasses\"") && strstr(text, "\"place\":\"attic\""));
    assert(strstr(text, "\"recent\":[\"wave: Hey there!\",\"look\"]"));
    obs.heard = obs.saw = obs.player_did = NULL;
    obs.recent_count = 0;
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_OK && !strstr(out, "player_said"));
    assert(ok_mind_request("glm-5.3", &obs, out, 100, &n) == OK_RANGE);
    obs.pet = NULL;
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_ARGUMENT);

    /* Plain, fenced and chatty answers are read; only the closed sets count. */
    assert(parse("{\\\"activity\\\":\\\"wave\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"Nice lamp!\\\"}", &d) == OK_OK);
    assert(d.activity == OK_MIND_WAVE && d.mood == OK_FEEL_HAPPY && !strcmp(d.line, "Nice lamp!"));
    ok_mind_memory(&d, memory);
    assert(!strcmp(memory, "wave: Nice lamp!"));
    assert(parse("{\\\"activity\\\":\\\"go_outside\\\",\\\"mood\\\":\\\"bored\\\",\\\"line\\\":\\\"\\\"}", &d) == OK_OK);
    assert(d.activity == OK_MIND_GO_OUTSIDE && !d.line[0]); /* quiet is allowed */
    ok_mind_memory(&d, memory);
    assert(!strcmp(memory, "go_outside"));
    assert(parse("```json\\n{\\\"activity\\\":\\\"look\\\",\\\"mood\\\":\\\"curious\\\",\\\"line\\\":\\\"Is it raining?\\\"}\\n```", &d) == OK_OK);
    assert(d.activity == OK_MIND_LOOK && !strcmp(ok_mind_mood_name(d.mood), "curious"));
    assert(parse("Sure! {\\\"activity\\\":\\\"eat\\\",\\\"mood\\\":\\\"hungry\\\",\\\"line\\\":\\\"Snack time. This line is far too long to fit on the screen\\\"}", &d) == OK_OK);
    assert(strlen(d.line) == OK_MIND_LINE);
    assert(parse("{\\\"activity\\\":\\\"rob_a_bank\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"hi\\\"}", &d) == OK_FORMAT);
    assert(parse("{\\\"activity\\\":\\\"sleep\\\",\\\"mood\\\":\\\"furious\\\",\\\"line\\\":\\\"hi\\\"}", &d) == OK_FORMAT);
    assert(parse("{\\\"activity\\\":\\\"sleep\\\",\\\"mood\\\":\\\"sleepy\\\"}", &d) == OK_FORMAT);
    assert(parse("{\\\"activity\\\":\\\"sleep\\\",\\\"mood\\\":\\\"sleepy\\\",\\\"line\\\":\\\"\\\\u00e9\\\"}", &d) == OK_OK &&
           !d.line[0]); /* nothing printable left: it stays quiet */
    assert(parse("I would rather not.", &d) == OK_FORMAT);
    assert(ok_mind_parse("{\"error\":\"rate limited\"}", 25, &d) == OK_FORMAT);
    puts("mind: request carries the observation; answers outside the closed sets are refused");
    return 0;
}
