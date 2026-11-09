#ifndef OKUPA_FRIEND_H
#define OKUPA_FRIEND_H

#include "okupa.h"

/* Playing with a friend on the same Wi-Fi. The console serves one small web
 * page; a friend opens it on a phone or a computer, types the 4-digit code the
 * console shows, and plays Pictionary with whoever holds the DS: one draws,
 * the other guesses, then they swap. A friend can also give the okupa a
 * drawing of their own, which the console may sign and hang on its wall.
 *
 * All of it is plain HTTP over the local network, answered here from bytes:
 * no server anywhere else, nothing on any chain unless the DS player hangs a
 * gift. The code keeps strangers on the same Wi-Fi out of the game; it is not
 * a secret against someone watching the network. */
#define OK_FRIEND_PORT 8080
#define OK_FRIEND_GUESSES 4
#define OK_FRIEND_TEXT 24

enum { OK_FRIEND_OFF, OK_FRIEND_WAITING, OK_FRIEND_DS_DRAWS, OK_FRIEND_THEY_DRAW };
/* The DS player's guess at the friend's drawing, as the friend judges it. */
enum { OK_GUESS_NONE, OK_GUESS_ASKED, OK_GUESS_RIGHT, OK_GUESS_WRONG };

typedef struct {
    int mode;
    uint16_t code;      /* 1000..9999 */
    uint32_t round;     /* goes up with each new drawing */
    int ds_points, friend_points;
    char name[17];      /* what the friend called themself ("Friend" until then) */
    uint32_t seen_at;   /* seconds, the friend's last request (0: never) */
    /* The DS draws: its page as it is now, and what the friend guessed. */
    uint8_t ds_drawing[512];
    char guesses[OK_FRIEND_GUESSES][OK_FRIEND_TEXT + 1]; /* newest first */
    int guess_count;
    int guesses_new;    /* for the console: something to show */
    /* The friend draws: their page, and the DS player's guess at it. */
    uint8_t their_drawing[512];
    int their_new;
    char ds_guess[OK_FRIEND_TEXT + 1];
    int ds_guess_state;
    int judged_new;     /* for the console: the friend said right or wrong */
    /* A drawing given to the okupa, waiting for the DS player to say yes. */
    uint8_t gift[512];
    int gift_new;
} ok_friend;

/* code: from the console's entropy (any 16 bits; kept within 1000..9999). */
void ok_friend_start(ok_friend *f, uint16_t code);
void ok_friend_stop(ok_friend *f);
/* A new round: who draws now (OK_FRIEND_DS_DRAWS or OK_FRIEND_THEY_DRAW). */
void ok_friend_round(ok_friend *f, int mode);
/* The DS player says the friend's guess k was right: a point each, new round. */
void ok_friend_right(ok_friend *f, int k);
/* The DS player's guess at the friend's drawing. */
void ok_friend_guess(ok_friend *f, const char *text);
int ok_friend_here(const ok_friend *f, uint32_t now); /* the friend asked something in the last 10 s */

/* One HTTP request (all of it in req, n bytes) answered into out: returns the
 * response's length, 0 if the request is not complete yet (read more), or a
 * short error response for anything malformed or with the wrong code. now:
 * seconds. The page itself is ok_friend_page. */
size_t ok_friend_http(ok_friend *f, const char *req, size_t n, uint32_t now, char *out, size_t cap);
#define OK_FRIEND_OUT 16384 /* enough for any response, the page included */

/* The page a friend's browser loads (tools/friend/page.html, embedded). */
extern const char *const ok_friend_page[];

#endif
