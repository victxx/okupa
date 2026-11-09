/* The friend game's server on a computer, with the console's own code
 * (game/src/friend.c) and a pretend DS player, to try the page in a browser:
 *   build/okupa-friend [port]      then open http://localhost:<port>
 * The pretend player draws a sun, slowly; a guess with "sun" in it is right.
 * When it is the browser's turn to draw, it guesses after the drawing has been
 * still for 4 s. Only the socket and the pretend player live here. */
#define _POSIX_C_SOURCE 200809L
#include "okupa_act.h"
#include "okupa_friend.h"
#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static ok_friend f;

/* The sun, drawn a little more each second. */
static void draw_sun(uint32_t step)
{
    static const int rays[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    int a, r, k;
    for (a = 0; a < (int)(step * 40) && a < 360; a++)
        for (r = 11; r <= 12; r++) {
            double t = a * 3.14159265 / 180.0;
            ok_drawing_set(f.ds_drawing, (unsigned)(32 + r * cos(t)), (unsigned)(32 + r * sin(t)), 1);
        }
    for (k = 0; k < 8 && step > 9 + (uint32_t)k; k++)
        for (r = 15; r < 22; r++) ok_drawing_set(f.ds_drawing, (unsigned)(32 + rays[k][0] * r), (unsigned)(32 + rays[k][1] * r), 1);
}

int main(int argc, char **argv)
{
    static char req[8192], out[OK_FRIEND_OUT];
    struct sockaddr_in a;
    int srv, one = 1, port = argc > 1 ? atoi(argv[1]) : OK_FRIEND_PORT;
    uint32_t started = 0, still_since = 0, round = 0;
    uint8_t last[512];
    ok_friend_start(&f, (uint16_t)time(NULL));
    if ((srv = socket(AF_INET, SOCK_STREAM, 0)) < 0) return perror("socket"), 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) || listen(srv, 4)) return perror("bind"), 1;
    printf("okupa-friend: http://localhost:%d  code %u\n", port, f.code);
    fflush(stdout);
    for (;;) {
        struct timeval tv = {1, 0};
        fd_set fds;
        uint32_t now = (uint32_t)time(NULL);
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        /* The pretend DS player. */
        if (f.mode == OK_FRIEND_DS_DRAWS) {
            if (round != f.round) round = f.round, started = now;
            draw_sun(now - started);
            if (f.guesses_new) {
                f.guesses_new = 0;
                printf("guess: %s\n", f.guesses[0]);
                if (strstr(f.guesses[0], "sun")) ok_friend_right(&f, 0);
            }
        } else if (f.mode == OK_FRIEND_THEY_DRAW) {
            if (round != f.round) round = f.round, still_since = now, memset(last, 0, sizeof last);
            if (memcmp(last, f.their_drawing, 512)) memcpy(last, f.their_drawing, 512), still_since = now;
            if (f.ds_guess_state == OK_GUESS_NONE && now - still_since > 4 && memcmp(f.their_drawing, (uint8_t[512]){0}, 512))
                ok_friend_guess(&f, "a house?");
            if (f.judged_new) {
                f.judged_new = 0;
                printf("judged: %s\n", f.ds_guess_state == OK_GUESS_RIGHT ? "right" : "wrong");
                if (f.ds_guess_state == OK_GUESS_RIGHT) ok_friend_round(&f, OK_FRIEND_DS_DRAWS);
                else f.ds_guess_state = OK_GUESS_NONE, still_since = now;
            }
        }
        if (f.gift_new) f.gift_new = 0, printf("a drawing was given to the okupa\n");
        fflush(stdout);
        if (select(srv + 1, &fds, NULL, NULL, &tv) > 0) {
            int c = accept(srv, NULL, NULL);
            size_t n = 0, r = 0;
            struct timeval rt = {2, 0};
            if (c < 0) continue;
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
            while (n < sizeof req) {
                ssize_t k = recv(c, req + n, sizeof req - n, 0);
                if (k <= 0) break;
                n += (size_t)k;
                if ((r = ok_friend_http(&f, req, n, now, out, sizeof out))) break;
            }
            if (r) {
                size_t sent = 0;
                while (sent < r) {
                    ssize_t k = send(c, out + sent, r - sent, 0);
                    if (k <= 0) break;
                    sent += (size_t)k;
                }
            }
            close(c);
        }
    }
}
