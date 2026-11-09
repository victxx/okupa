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
    char memory[OK_MIND_MEMORY];
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
    obs.lines[0] = "You touched me!";
    obs.lines[1] = "Who's there?";
    obs.line_count = 2;
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_OK);
    assert(ok_json_parse(out, n, &root) == OK_OK && ok_json_member(&root, "model", &v) == OK_OK);
    assert(!strncmp(out, "{\"chat_template_kwargs\":", 24)); /* keys sorted, as Go re-encodes it */
    assert(ok_json_member(&root, "messages", &v) == OK_OK && ok_json_index(&v, 1, &m) == OK_OK);
    assert(ok_json_member(&m, "content", &v) == OK_OK && ok_json_string(&v, text, sizeof text, &n) == OK_OK);
    assert(ok_json_parse(text, n, &root) == OK_OK);
    assert(strstr(text, "\"time\":\"15:20\"") && strstr(text, "\"food\":70") && strstr(text, "drew a lamp"));
    assert(!strncmp(text, "{\"player_said\":\"hola \\\"okupa\\\", que tal?\"", 39)); /* first, accents dropped */
    assert(strstr(text, "\"energy\":70") && !strstr(text, "\"sleep\":"));
    assert(strstr(text, "\"okupa_sees\":\"a person with glasses\"") && strstr(text, "\"place\":\"attic\""));
    assert(strstr(text, "\"recent\":[\"wave: Hey there!\",\"look\"]"));
    assert(strstr(text, "\"your_recent_lines\":[\"You touched me!\",\"Who's there?\"]"));
    obs.line_count = 0;
    /* What it knows for good, the question it is waiting on, the city. */
    obs.known[0] = "The player is called Victor";
    obs.known[1] = "Victor likes mangoes";
    obs.known_count = 2;
    obs.asked = "What is your name?";
    obs.city = "Mumbai";
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_OK && ok_json_parse(out, n, &root) == OK_OK);
    assert(ok_json_member(&root, "messages", &v) == OK_OK && ok_json_index(&v, 1, &m) == OK_OK);
    assert(ok_json_member(&m, "content", &v) == OK_OK && ok_json_string(&v, text, sizeof text, &n) == OK_OK);
    assert(ok_json_parse(text, n, &root) == OK_OK && strstr(text, "\"city\":\"Mumbai\""));
    assert(strstr(text, "\"known\":[\"The player is called Victor\",\"Victor likes mangoes\"]"));
    assert(strstr(text, "\"waiting_for_answer_to\":\"What is your name?\""));
    obs.known_count = 0;
    obs.asked = obs.city = NULL;
    obs.heard = obs.saw = obs.player_did = NULL;
    obs.recent_count = 0;
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_OK && !strstr(out, "\\\"player_said\\\":"));
    assert(ok_mind_request("glm-5.3", &obs, out, 100, &n) == OK_RANGE);
    { /* the fullest observation still fits the decision's request buffer (OK_THINK_REQUEST) */
        static char big[12288];
        static char facts[OK_MIND_KNOWN][OK_MIND_NOTE + 1], said[OK_MIND_LINES][OK_MIND_LINE + 1], mem[OK_MIND_RECENT][OK_MIND_MEMORY];
        static char heard[OK_MIND_TEXT + 1];
        int k;
        memset(heard, 'a', OK_MIND_TEXT);
        obs.heard = obs.saw = heard;
        obs.player_did = "said your Pictionary guess 'a very long guess here' was right";
        obs.asked = obs.goal = obs.plan = "a question or a goal of sixty characters, more or less, ok?";
        obs.city = "Mumbai";
        for (k = 0; k < OK_MIND_KNOWN; k++) memset(facts[k], 'f', OK_MIND_NOTE), obs.known[k] = facts[k];
        obs.known_count = OK_MIND_KNOWN;
        for (k = 0; k < OK_MIND_LINES; k++) memset(said[k], '"', OK_MIND_LINE), obs.lines[k] = said[k];
        obs.line_count = OK_MIND_LINES;
        for (k = 0; k < OK_MIND_RECENT; k++) memset(mem[k], 'm', OK_MIND_MEMORY - 1), obs.recent[k] = mem[k];
        obs.recent_count = OK_MIND_RECENT;
        assert(ok_mind_request("glm-5.3", &obs, big, sizeof big, &n) == OK_OK && ok_json_parse(big, n, &root) == OK_OK);
        memset(&obs, 0, sizeof obs);
        obs.pet = &p;
    }
    obs.pet = NULL;
    assert(ok_mind_request("glm-5.3", &obs, out, sizeof out, &n) == OK_ARGUMENT);

    /* Plain, fenced and chatty answers are read; only the closed sets count. */
    assert(parse("{\\\"activity\\\":\\\"wave\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"Nice lamp!\\\"}", &d) == OK_OK);
    assert(d.activity == OK_MIND_WAVE && d.mood == OK_FEEL_HAPPY && !strcmp(d.line, "Nice lamp!"));
    ok_mind_memory(&d, memory);
    assert(!strcmp(memory, "wave: Nice lamp!"));
    assert(!d.thought[0] && !d.ask[0] && !d.remember[0]); /* optional notes, absent */
    assert(parse("{\\\"activity\\\":\\\"look\\\",\\\"mood\\\":\\\"curious\\\",\\\"line\\\":\\\"\\\","
                 "\\\"thought\\\":\\\"Who is this person?\\\",\\\"ask\\\":\\\"What is your name?\\\","
                 "\\\"remember\\\":\\\"The player is called Victor\\\"}", &d) == OK_OK);
    assert(!strcmp(d.thought, "Who is this person?") && !strcmp(d.ask, "What is your name?"));
    assert(!strcmp(d.remember, "The player is called Victor"));
    ok_mind_memory(&d, memory);
    assert(!strcmp(memory, "look (asked: What is your name?) (thought: Who is this person?)")); /* so it does not ask or muse again */
    assert(d.x == -1 && d.z == -1 && d.gaze == -1 && d.energy == 50); /* no body given: instinct */
    assert(parse("{\\\"activity\\\":\\\"wander\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"\\\","
                 "\\\"x\\\":80,\\\"z\\\":250,\\\"gaze\\\":\\\"window\\\",\\\"energy\\\":72.5}", &d) == OK_OK);
    assert(d.x == 80 && d.z == 100 && d.gaze == OK_LOOK_WINDOW && d.energy == 72);
    assert(parse("{\\\"activity\\\":\\\"wander\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"\\\","
                 "\\\"x\\\":-1,\\\"gaze\\\":\\\"moon\\\"}", &d) == OK_OK);
    assert(d.x == -1 && d.gaze == -1 && !strcmp(ok_mind_gaze_name(d.gaze), "-"));
    assert(!d.make[0] && !d.draw[0]);
    assert(parse("{\\\"activity\\\":\\\"play\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"\\\","
                 "\\\"make\\\":\\\"a paper boat\\\",\\\"draw\\\":\\\"the moon\\\"}", &d) == OK_OK);
    assert(!strcmp(d.make, "a paper boat") && !strcmp(d.draw, "the moon"));
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

    /* A line it said lately is blanked, never replaced; the rest of the decision stands. */
    {
        const char *lines[3] = {"You touched me!", NULL, "Hello, Victor! Nice to see you again"};
        size_t used;
        assert(parse("{\\\"activity\\\":\\\"peek\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"you touched ME.\\\"}", &d) == OK_OK);
        assert(ok_mind_dedupe(&d, lines, 3) && !d.line[0] && d.activity == OK_MIND_PEEK);
        assert(parse("{\\\"activity\\\":\\\"wave\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"Hello, Victor!\\\"}", &d) == OK_OK);
        assert(ok_mind_dedupe(&d, lines, 3) && !d.line[0]);
        assert(parse("{\\\"activity\\\":\\\"wave\\\",\\\"mood\\\":\\\"happy\\\",\\\"line\\\":\\\"A mango? Yum!\\\"}", &d) == OK_OK);
        assert(!ok_mind_dedupe(&d, lines, 3) && !strcmp(d.line, "A mango? Yum!"));
        assert(ok_mind_ascii("\xc3\xb1", 2, &used) == 'n' && used == 2);
        assert(ok_mind_ascii("\xc3\x89", 2, &used) == 'E' && used == 2);
        assert(ok_mind_ascii("\xc3\xbf", 2, &used) == 'y' && used == 2);
        assert(ok_mind_ascii("\xc2\xbf", 2, &used) == -1 && used == 2);
        assert(ok_mind_ascii("\xe2\x80\x9c", 3, &used) == -1 && used == 3);
        assert(ok_mind_ascii("\xe2", 1, &used) == -1 && used == 1);
    }
    assert(ok_mind_parse("{\"error\":\"rate limited\"}", 25, &d) == OK_FORMAT);
    puts("mind: request carries the observation; answers outside the closed sets are refused");
    return 0;
}
