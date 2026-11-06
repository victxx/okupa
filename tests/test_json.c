#include "okupa_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int valid(const char *s)
{
    ok_json v;
    return ok_json_parse(s, strlen(s), &v) == OK_OK;
}

int main(void)
{
    static const char doc[] =
        " {\"id\":\"chat-9\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
        "\"content\":\"{\\\"activity\\\":\\\"wave\\\",\\\"line\\\":\\\"Hi \\u00e9 :)\\\"}\"}}],"
        "\"usage\":{\"prompt_tokens\":12,\"total_tokens\":3.5e2},\"ok\":true,\"x\":null} ";
    ok_json root, v, c;
    char out[128];
    size_t n;

    assert(ok_json_parse(doc, strlen(doc), &root) == OK_OK && root.at[0] == '{');
    assert(ok_json_member(&root, "id", &v) == OK_OK && ok_json_string(&v, out, sizeof out, &n) == OK_OK);
    assert(!strcmp(out, "chat-9") && n == 6);
    assert(ok_json_member(&root, "choices", &v) == OK_OK && ok_json_index(&v, 0, &c) == OK_OK);
    assert(ok_json_index(&v, 1, &c) == OK_RANGE);
    assert(ok_json_index(&v, 0, &c) == OK_OK && ok_json_member(&c, "message", &v) == OK_OK);
    assert(ok_json_member(&v, "content", &c) == OK_OK && ok_json_string(&c, out, sizeof out, &n) == OK_OK);
    assert(!strcmp(out, "{\"activity\":\"wave\",\"line\":\"Hi \xc3\xa9 :)\"}"));
    assert(ok_json_parse(out, n, &v) == OK_OK && ok_json_member(&v, "activity", &c) == OK_OK);
    assert(ok_json_member(&root, "missing", &v) == OK_RANGE);
    assert(ok_json_string(&c, out, 4, &n) == OK_RANGE);

    assert(valid("[]") && valid("{}") && valid("[1,-0.5,2e10,\"a\",true,false,null,{\"a\":[]}]"));
    assert(!valid("{\"a\":1,}") && !valid("[1,]") && !valid("{a:1}") && !valid("01") && !valid("1.") && !valid("\"\\x\""));
    assert(!valid("\"a") && !valid("[1] 2") && !valid("") && !valid("\"\x01\"") && !valid("tru"));
    assert(!valid("[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]")); /* deeper than 16 */
    puts("json: in-place parsing, members, indexes, unescaping; malformed and deep input refused");
    return 0;
}
