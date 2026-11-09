#ifndef OKUPA_UI_H
#define OKUPA_UI_H

#include "okupa_house.h"
#include "okupa_music.h"
#include "okupa_pet.h"
#include <stdint.h>

/* The two screens, drawn into 256x192 frames of DS colours (BGR555 with bit 15
 * set) by plain C, so the DSi, the host preview and later a phone share it. */
#define OK_UI_W 256
#define OK_UI_H 192
enum { OK_UI_HOME, OK_UI_SKETCH, OK_UI_DEV, OK_UI_WIFI, OK_UI_PASSWORD, OK_UI_WALLET, OK_UI_CLOSET, OK_UI_SPLASH, OK_UI_FILM,
       OK_UI_GALLERY };
enum { OK_UI_NOTHING, OK_UI_CHANGED, OK_UI_RUN_CHECKS, OK_UI_RUN_NETWORK, OK_UI_WIFI_SCAN, OK_UI_WIFI_CONNECT, OK_UI_WALLET_OPEN,
       OK_UI_WALLET_SCRIBBLE, OK_UI_WALLET_SPEED, OK_UI_SEND_DRAWING, OK_UI_WALLET_BALANCE,
       /* Buttons the platform may act on (the okupa has already said a hint):
        * look through the camera, listen through the microphone, a game
        * (it guesses the sketchbook's drawing), a friend. */
       OK_UI_PHOTO, OK_UI_TALK, OK_UI_GAMES, OK_UI_FRIEND,
       /* Pictionary's Guess! (the doodle is in ok_ui.doodle); the sketchbook
        * closed with ink in it; a hung picture tapped in the gallery
        * (ok_ui.gallery_pick): the platform may ask 0G about each. */
       OK_UI_GUESS, OK_UI_DRAWING_DONE, OK_UI_GALLERY_LOOK,
       /* Dream, where the mind dreams (ok_facts og_features 16): a new dream
        * asked of 0G (the last one is shown meanwhile, if there is one). */
       OK_UI_DREAM };
/* What the platform really handles (ok_ui.wired): a button for any other
 * of these is hidden, never shown dead. */
#define OK_UI_HAS_PHOTO 1
#define OK_UI_HAS_TALK 2
#define OK_UI_HAS_GAMES 4
#define OK_UI_HAS_FRIEND 16
#define OK_UI_DEV_LINES 14
#define OK_UI_APS 5
#define OK_UI_WALL 6 /* framed drawings, each hung only once seen in a block */
#define OK_UI_CARD_TEXT 160

/* A Wi-Fi network found by the platform's scan. */
typedef struct {
    char ssid[33];
    int secure;   /* needs a password */
    int strength; /* 0..3 bars */
} ok_ui_ap;

typedef struct {
    int scene;
    const char *say;    /* the okupa's line, NULL = its mood line */
    int say_ticks;      /* frames left before it goes back to its mood line */
    uint32_t ticks;     /* frames since start, for small animations */
    uint8_t drawing[64 * 64 / 8];
    int eraser;
    int last_x, last_y; /* previous canvas point while the stylus is down */
    char dev[OK_UI_DEV_LINES][33];
    /* Wi-Fi set-up: the platform fills aps and status; the player picks one
     * and types its password, then the platform gets OK_UI_WIFI_CONNECT. */
    ok_ui_ap aps[OK_UI_APS];
    int ap_count, ap_selected;
    char password[64];
    int password_len, keys; /* keys: 0 lower case, 1 upper case, 2 symbols */
    char wifi_status[33];
    char net_status[33]; /* the platform's one-line network state, shown at home */
    /* The okupa on the top screen: how it moves, what it wears. */
    ok_monkey_anim anim;
    ok_look look;
    int ready, happy_ticks;
    int pressed, pressed_ticks; /* the button just tapped (1..12, 100 Wear, 101 Out, 102.. a tab), for its pressed look */
    int page;                   /* the home screen's menu: 0 care, 1 more */
    int wired;                  /* OK_UI_HAS_*: set by the platform for what it really does */
    /* Posting a drawing: it walks out with the envelope while the drawing
     * is on its way (drawing_sent), and comes back to hang it once a block
     * has it, or empty-handed after a while; 2 = back, still waiting. */
    int outing, outing_ticks;
    char unlocked[40];          /* the line for what the last picture unlocked */
    int sound;                  /* a sound to play, OK_SFX_* + 1; the platform plays it and clears it */
    int heard_bites, heard_move;
    uint32_t intro_t;           /* ticks into the splash or the opening film */
    int mind_mood, mind_ticks;  /* a mood the okupa's mind gave, while fresh (OK_POSE_* + 1) */
    char mind_line[41];         /* and its line, kept here while it is said */
    int going;                  /* walking to the door to change place: OK_PLACE_* + 1 */
    /* The console's own account: the platform creates the key from the
     * player's scribbles (wallet_progress 0..100) and fills in the address. */
    int wallet_ready, wallet_progress;
    char wallet_address[43], wallet_network[12];
    char wallet_status[33]; /* e.g. the hello on its way */
    /* Its balance, as a public JSON-RPC node reports it (display only; the
     * platform fills it): "0.004200 ETH" or "", and where it came from or
     * why there is none. wallet_qr: the QR code instead (Show QR). */
    char wallet_balance[24], wallet_balance_note[40];
    int wallet_low, wallet_qr;
    /* The wall: drawings the console saw included, oldest first. */
    uint8_t wall[OK_UI_WALL][64 * 64 / 8];
    int wall_count;
    int drawing_sent; /* the sketchbook's drawing is on its way: not editable until it lands */
    int tx_waiting;   /* a transaction (hello, letter, drawing) signed and not yet seen in a block */
    /* Pictionary: the sketchbook draws on a page of its own (doodle), so the
     * drawing to send is never touched; gallery_pick: the picture tapped. */
    int pictionary, gallery_pick;
    uint8_t doodle[64 * 64 / 8];
    /* A picture new on the wall is hung with a little show: wall_seen is
     * the wall as last shown, hang_t the ticks into hanging hang_slot. */
    uint32_t wall_seen;
    int wall_known, hang_t, hang_slot;
    /* The okupa's inner life, from its mind: a thought, a question for the
     * player, and whether it is listening to the answer. */
    char thought[61], ask[61], ask_hint[24]; /* ask_hint: how to answer ("" = hold L) */
    int thought_ticks, ask_ticks, listening;
    /* What its mind makes: the name of the thing on the floor, a drawing of
     * its own (in a gilt frame), last night's dream (BGR555, up to 128x128). */
    char thing_name[21], thing_line[48];
    uint8_t own[64 * 64 / 8];
    int own_set;
    uint16_t dream[128 * 128];
    int dream_w, dream_h, dream_ticks;
    int dream_old; /* the dream shown is the saved one, not a new one */
    const char *did; /* what the player just did to it (fed, played, petted...), for the mind; main clears it */
    /* A card on the top screen for what 0G looked at: the picture it was
     * sent (a photo, a drawing), its verified words or why there are none,
     * and a footer (how long it took). card_live: the platform keeps it
     * coming (a new photo each round) until the card is closed. */
    char card_title[24], card_text[OK_UI_CARD_TEXT + 1], card_foot[40];
    uint16_t card_px[128 * 96];
    int card_w, card_h, card_ticks, card_verified, card_live;
    char busy[40]; /* what it is busy with while listening is 2 ("Asking 0G...") */
    /* What the player said, as 0G's speech to text heard it (verified), in a
     * caption of the player's own at the foot of the top screen. */
    char heard[OK_UI_CARD_TEXT + 1];
    int heard_ticks;
} ok_ui;

void ok_ui_init(ok_ui *u);
/* Starts with the splash and the opening film (a touch skips each); the
 * platform calls it once at boot. ok_ui_init alone starts at home. */
void ok_ui_start_intro(ok_ui *u);
/* A decision from the okupa's mind: its next move (OK_MOVE_*, or
 * OK_MOVE_TO_DOOR with where = OK_PLACE_* to go out or in, walking to the
 * door first), a mood (OK_POSE_*, -1 none) shown for a while if the pet
 * itself is content, and a line (NULL or "" for none). */
void ok_ui_direct(ok_ui *u, const ok_pet *p, int move, int where, int mood, const char *line);
/* A thought bubble (a cloud, not speech) for about 6 s; up to 60 ASCII
 * characters, copied (NULL or "" clears it). */
void ok_ui_thought(ok_ui *u, const char *text);
/* A question for the player, with a hint to hold L and answer; it stays
 * until ok_ui_ask(u, NULL) or 30 s. Up to 60 ASCII characters, copied.
 * ask_hint: the same with another way to answer ("A yes  B no"). */
void ok_ui_ask(ok_ui *u, const char *question);
void ok_ui_ask_hint(ok_ui *u, const char *question, const char *hint);
/* 0 off, 1 listening (a microphone by its head while L is held; it looks
 * up), 2 thinking the answer over (a cloud of dots, with u->busy as its
 * words if set). Shown over any bubble. */
void ok_ui_listening(ok_ui *u, int state);
/* The words in the thinking cloud ("Asking 0G..."); NULL or "" for the
 * plain "Thinking...". Up to 39 ASCII characters, copied. */
void ok_ui_busy(ok_ui *u, const char *text);
/* What the player said, word for word as 0G heard it, for about 10 s (NULL
 * clears it). Up to OK_UI_CARD_TEXT ASCII characters, copied. */
void ok_ui_heard(ok_ui *u, const char *text);
/* The card for what 0G looked at (see ok_ui.card_*): px a picture of DS
 * colours up to 128x96 (NULL: none), verified 1 for words 0G's TEE signed,
 * 0 for a failure told as it is. Shown for about 10 s or until a tap.
 * card_drawing: the same with a 64x64 drawing (shown at 96x96). */
void ok_ui_card(ok_ui *u, const char *title, const char *text, const uint16_t *px, int w, int h, int verified,
                const char *foot);
void ok_ui_card_drawing(ok_ui *u, const char *title, const char *text, const uint8_t pixels[512], int verified,
                        const char *foot);
/* The okupa's mind steers its body: walk to x, z (0..100 across the floor
 * of where it is, left to right and back to front as seen on screen; -1
 * stays) at the next break, look at a thing (OK_GAZE_* from okupa_monkey.h,
 * or -1 for its own glances) and move with an energy 0..100 (50 as ever,
 * -1 too) until the next call. */
void ok_ui_body(ok_ui *u, int x, int z, int gaze, int energy);
/* Something it made: up to 16 boxes {x, y, z, w, h, d, colour} on a 16-cube
 * grid (y up from the floor, colour 0..15), set down by it on the floor,
 * with a name (up to 20 characters). A new one replaces the old; n 0 clears. */
void ok_ui_drop_object(ok_ui *u, const uint8_t boxes[][7], int n, const char *name);
/* A 64x64 drawing it made itself (the ok_drawing_get layout): hung in the
 * next free frame on the attic wall in a gilt frame, or on the terrace's
 * last easel when the wall is full. The player's sketchbook is untouched. */
void ok_ui_okupa_drawing(ok_ui *u, const uint8_t pixels[512]);
/* Last night's dream: a picture of DS colours (w, h up to 128), shown on the
 * top screen in a dream cloud for about 20 s or until the player taps. */
void ok_ui_dream(ok_ui *u, const uint16_t *rgb15, int w, int h);
/* The same, kept for the Dream button without showing it now. */
void ok_ui_dream_keep(ok_ui *u, const uint16_t *rgb15, int w, int h);
/* One frame (1/60 s) passed; returns OK_UI_CHANGED when the screens need
 * redrawing (30 times a second: the okupa is always moving a little). */
int ok_ui_tick(ok_ui *u, const ok_pet *p);
/* Stylus at x, y on the bottom screen; held = it was already down last frame. */
int ok_ui_touch(ok_ui *u, ok_pet *p, int x, int y, int held, int64_t now);
void ok_ui_release(ok_ui *u);
/* font: 128 glyphs of 8 rows, bit 0 = leftmost pixel. */
void ok_ui_render(const ok_ui *u, const ok_pet *p, int hour, int minute, const uint8_t (*font)[8],
                  uint16_t top[OK_UI_W * OK_UI_H], uint16_t bottom[OK_UI_W * OK_UI_H]);

#endif
