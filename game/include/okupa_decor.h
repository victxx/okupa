#ifndef OKUPA_DECOR_H
#define OKUPA_DECOR_H

#include <stdint.h>

/* Doing up the attic: furniture bought with bananas (earned in the games,
 * with friends and by hanging pictures), placed on a grid over the floor,
 * turned, moved or sold back; and paint for the wallpaper and the floor.
 *
 * The floor is a grid of OK_DECOR_COLS x OK_DECOR_ROWS cells of 200 units
 * (x from -1400, z from -1100, as in game/src/house.c). The attic's own
 * things (the bed, the crate table, the boxes, the plants, the books, the
 * cushion, the way to the door) take cells nothing can go on. */
#define OK_DECOR_COLS 14
#define OK_DECOR_ROWS 12
#define OK_DECOR_CELL 200
#define OK_DECOR_MAX 12      /* pieces in the attic at once */
#define OK_DECOR_PAINTS 4    /* wallpaper and floor colours to choose from */
#define OK_DECOR_START 10    /* bananas a new okupa starts with */

enum {
    OK_PIECE_ARMCHAIR, OK_PIECE_RUG, OK_PIECE_LAMP, OK_PIECE_SHELF, OK_PIECE_PLANT, OK_PIECE_BEANBAG, OK_PIECE_TV,
    OK_PIECE_GUITAR, OK_PIECE_FISHBOWL, OK_PIECE_CHEST, OK_PIECE_RECORDS, OK_PIECE_TEDDY, OK_PIECE_BANANA_TREE, OK_PIECES
};

typedef struct {
    const char *name;
    int price;  /* bananas */
    int w, d;   /* cells, unturned */
    int flat;   /* lies on the floor: other pieces may stand on it */
} ok_piece_kind;

typedef struct {
    uint8_t kind, col, row, turn; /* turn: quarter turns, 0..3; col/row: the piece's corner cell */
} ok_piece;

typedef struct {
    ok_piece piece[OK_DECOR_MAX];
    int count;
    uint8_t wallpaper, floor; /* 0..OK_DECOR_PAINTS - 1 */
    uint16_t bananas;
} ok_decor;

const ok_piece_kind *ok_decor_kind(int kind);
void ok_decor_init(ok_decor *d);
/* The cells a piece takes, turned: w and d swap on a quarter turn. */
void ok_decor_size(const ok_piece *p, int *w, int *d);
/* 1 if the piece fits there: inside the floor, off the attic's own things
 * and off other standing pieces (skip: one piece not to count, -1 none). */
int ok_decor_fits(const ok_decor *d, const ok_piece *p, int skip);
/* Buys a piece and puts it at (col, row): its index, or -1 (no room, no
 * bananas, no place). */
int ok_decor_buy(ok_decor *d, int kind, int col, int row);
/* Moves or turns piece i (1 done, 0 it would not fit: left as it was). */
int ok_decor_move(ok_decor *d, int i, int col, int row);
int ok_decor_turn(ok_decor *d, int i);
/* Sells piece i back for half its price. */
void ok_decor_sell(ok_decor *d, int i);
/* The piece standing on cell (col, row), or -1 (a flat one only if nothing stands on it). */
int ok_decor_at(const ok_decor *d, int col, int row);
/* Paint: 1 if bought (OK_DECOR_PAINT_PRICE bananas). */
#define OK_DECOR_PAINT_PRICE 3
int ok_decor_paint(ok_decor *d, int wallpaper, int floor);
/* 1 if the attic itself has something on that cell. */
int ok_decor_fixed(int col, int row);
/* Bananas earned, capped so the count stays a small number. */
void ok_decor_earn(ok_decor *d, int bananas);
/* A number that changes whenever anything visible does (for the kept frame). */
uint32_t ok_decor_sum(const ok_decor *d);

#endif
