#include "okupa_friend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char out[OK_FRIEND_OUT];

static size_t ask(ok_friend *f, const char *method, const char *target, const char *body)
{
    static char req[4096];
    size_t n = (size_t)snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: 192.168.1.9:8080\r\nContent-Length: %lu\r\n\r\n%s",
                                method, target, (unsigned long)strlen(body), body);
    size_t r = ok_friend_http(f, req, n, 100, out, sizeof out);
    out[r < sizeof out ? r : sizeof out - 1] = 0;
    return r;
}

static int status(int code)
{
    char want[16];
    snprintf(want, sizeof want, "HTTP/1.1 %d", code);
    return !strncmp(out, want, strlen(want));
}

int main(void)
{
    static ok_friend f;
    static char hex[1025];
    char path[64];
    int i;

    ok_friend_start(&f, 51234);
    assert(f.code >= 1000 && f.code <= 9999 && f.mode == OK_FRIEND_WAITING && !ok_friend_here(&f, 100));

    /* The page, whole, for anyone on the Wi-Fi; everything else needs the code. */
    assert(ask(&f, "GET", "/", "") > 2000 && status(200) && strstr(out, "text/html") && strstr(out, "<canvas id=\"pad\""));
    assert(ask(&f, "GET", "/s?c=0000", "") && status(403));
    assert(ask(&f, "GET", "/s", "") && status(403));

    /* A request still arriving is not answered yet: the headers, then the body. */
    assert(ok_friend_http(&f, "GET /s?c=1 HTTP/1.1\r\nHost: x", 29, 100, out, sizeof out) == 0);
    {
        char partial[128];
        size_t n = (size_t)snprintf(partial, sizeof partial, "POST /n?c=%u HTTP/1.1\r\nContent-Length: 5\r\n\r\nAl", f.code);
        assert(ok_friend_http(&f, partial, n, 100, out, sizeof out) == 0);
    }

    /* They join with a name: the DS draws first; they see its drawing as it grows and guess. */
    snprintf(path, sizeof path, "/n?c=%u", f.code);
    assert(ask(&f, "POST", path, "Alex") && status(200) && !strcmp(f.name, "Alex") && f.mode == OK_FRIEND_DS_DRAWS);
    assert(ok_friend_here(&f, 105) && !ok_friend_here(&f, 200));
    f.ds_drawing[0] = 0x81;
    snprintf(path, sizeof path, "/s?c=%u", f.code);
    assert(ask(&f, "GET", path, "") && status(200) && strstr(out, "\"mode\":2") && strstr(out, "\"drawing\":\"8100"));
    assert(strstr(out, "\"name\":\"Alex\""));
    snprintf(path, sizeof path, "/g?c=%u", f.code);
    assert(ask(&f, "POST", path, "a \"cat\"") && status(200) && f.guess_count == 1 && f.guesses_new);
    assert(ask(&f, "POST", path, "dog") && !strcmp(f.guesses[0], "dog") && !strcmp(f.guesses[1], "a \"cat\""));
    snprintf(path, sizeof path, "/s?c=%u", f.code);
    assert(ask(&f, "GET", path, "") && strstr(out, "\"guesses\":[\"dog\",\"a \\\"cat\\\"\"]")); /* escaped */
    for (i = 0; i < 6; i++) {
        snprintf(path, sizeof path, "/g?c=%u", f.code);
        ask(&f, "POST", path, "more");
    }
    assert(f.guess_count == OK_FRIEND_GUESSES);

    /* The DS player says "dog" was right: a point each, and their turn to draw. */
    ok_friend_right(&f, 1);
    assert(f.ds_points == 1 && f.friend_points == 1 && f.mode == OK_FRIEND_THEY_DRAW && !f.guess_count);
    snprintf(path, sizeof path, "/g?c=%u", f.code);
    assert(ask(&f, "POST", path, "late") && status(400)); /* not their guessing turn */

    /* They draw (1024 hex characters), the DS guesses, they judge. */
    for (i = 0; i < 512; i++) snprintf(hex + 2 * i, 3, "%02x", i & 0xff);
    snprintf(path, sizeof path, "/d?c=%u", f.code);
    assert(ask(&f, "POST", path, hex) && status(200) && f.their_new && f.their_drawing[3] == 3);
    assert(ask(&f, "POST", path, "abc") && status(400)); /* not a drawing */
    snprintf(path, sizeof path, "/j?c=%u&ok=1", f.code);
    assert(ask(&f, "POST", path, "") && status(400)); /* nothing to judge yet */
    ok_friend_guess(&f, "  a sun  ");
    assert(!strcmp(f.ds_guess, "a sun") && f.ds_guess_state == OK_GUESS_ASKED);
    snprintf(path, sizeof path, "/s?c=%u", f.code);
    assert(ask(&f, "GET", path, "") && strstr(out, "\"mode\":3") && strstr(out, "\"guess\":\"a sun\",\"judged\":1"));
    snprintf(path, sizeof path, "/j?c=%u&ok=1", f.code);
    assert(ask(&f, "POST", path, "") && f.ds_guess_state == OK_GUESS_RIGHT && f.ds_points == 2 && f.judged_new);

    /* A drawing given to the okupa waits for the DS player. */
    snprintf(path, sizeof path, "/h?c=%u", f.code);
    assert(ask(&f, "POST", path, hex) && f.gift_new && f.gift[511] == 0xff);

    /* Unknown paths and methods, and a stopped game. */
    snprintf(path, sizeof path, "/x?c=%u", f.code);
    assert(ask(&f, "GET", path, "") && status(404));
    assert(ask(&f, "PUT", "/", "") && status(400));
    ok_friend_stop(&f);
    snprintf(path, sizeof path, "/s?c=%u", f.code);
    assert(ask(&f, "GET", path, "") && status(403));
    puts("friend: the page for any browser on the Wi-Fi; join with the code, draw, guess, judge, give; partial requests wait");
    return 0;
}
