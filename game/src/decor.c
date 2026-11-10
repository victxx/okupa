#include "okupa_decor.h"
#include <string.h>

static const ok_piece_kind kinds[OK_PIECES] = {
    {"Armchair", 6, 2, 2, 0},  {"Round rug", 4, 3, 3, 1}, {"Lamp", 5, 1, 1, 0},    {"Bookshelf", 8, 2, 1, 0},
    {"Big plant", 3, 1, 1, 0}, {"Beanbag", 5, 2, 2, 0},   {"Old TV", 10, 2, 1, 0}, {"Guitar", 7, 1, 1, 0},
    {"Fish bowl", 9, 1, 1, 0}, {"Toy chest", 6, 2, 1, 0}, {"Records", 8, 1, 1, 0}, {"Teddy", 4, 1, 1, 0},
    {"Palm tree", 12, 1, 1, 0},
};

const ok_piece_kind *ok_decor_kind(int kind) { return kind >= 0 && kind < OK_PIECES ? &kinds[kind] : NULL; }

void ok_decor_init(ok_decor *d)
{
    memset(d, 0, sizeof *d);
    d->bananas = OK_DECOR_START;
}

void ok_decor_size(const ok_piece *p, int *w, int *d)
{
    const ok_piece_kind *k = ok_decor_kind(p->kind);
    int a = k ? k->w : 1, b = k ? k->d : 1;
    *w = p->turn & 1 ? b : a;
    *d = p->turn & 1 ? a : b;
}

/* The attic's own things, in cells (see room() in game/src/house.c). */
int ok_decor_fixed(int col, int row)
{
    static const uint8_t taken[][4] = { /* col, row, end col, end row */
        {0, 0, 6, 4},    /* the bed on its pallet */
        {5, 0, 8, 2},    /* the crate table */
        {10, 0, 14, 3},  /* the cardboard boxes and the radio */
        {0, 7, 2, 10},   /* the plant in the tin */
        {0, 6, 2, 8},    /* the books */
        {12, 8, 14, 10}, /* the plant in the clay pot */
        {10, 8, 13, 11}, /* the floor cushion */
        {12, 3, 14, 7},  /* the way to the door */
    };
    unsigned i;
    for (i = 0; i < sizeof taken / sizeof taken[0]; i++)
        if (col >= taken[i][0] && col < taken[i][2] && row >= taken[i][1] && row < taken[i][3]) return 1;
    return 0;
}

static int overlaps(const ok_piece *a, const ok_piece *b)
{
    int aw, ad, bw, bd;
    ok_decor_size(a, &aw, &ad);
    ok_decor_size(b, &bw, &bd);
    return a->col < b->col + bw && b->col < a->col + aw && a->row < b->row + bd && b->row < a->row + ad;
}

int ok_decor_fits(const ok_decor *d, const ok_piece *p, int skip)
{
    const ok_piece_kind *k = ok_decor_kind(p->kind);
    int w, dd, c, r, i;
    if (!k || p->turn > 3) return 0;
    ok_decor_size(p, &w, &dd);
    if (p->col + w > OK_DECOR_COLS || p->row + dd > OK_DECOR_ROWS) return 0;
    for (c = p->col; c < p->col + w; c++)
        for (r = p->row; r < p->row + dd; r++)
            if (ok_decor_fixed(c, r)) return 0;
    for (i = 0; i < d->count; i++) {
        const ok_piece_kind *o = ok_decor_kind(d->piece[i].kind);
        if (i == skip || !o) continue;
        if (k->flat != o->flat) continue; /* a rug under an armchair is fine */
        if (overlaps(p, &d->piece[i])) return 0;
    }
    return 1;
}

int ok_decor_buy(ok_decor *d, int kind, int col, int row)
{
    const ok_piece_kind *k = ok_decor_kind(kind);
    ok_piece p;
    if (!k || d->count >= OK_DECOR_MAX || d->bananas < k->price || col < 0 || row < 0) return -1;
    p.kind = (uint8_t)kind, p.col = (uint8_t)col, p.row = (uint8_t)row, p.turn = 0;
    if (!ok_decor_fits(d, &p, -1)) return -1;
    d->bananas = (uint16_t)(d->bananas - k->price);
    d->piece[d->count] = p;
    return d->count++;
}

int ok_decor_move(ok_decor *d, int i, int col, int row)
{
    ok_piece p;
    if (i < 0 || i >= d->count || col < 0 || row < 0) return 0;
    p = d->piece[i];
    p.col = (uint8_t)col, p.row = (uint8_t)row;
    if (!ok_decor_fits(d, &p, i)) return 0;
    d->piece[i] = p;
    return 1;
}

int ok_decor_turn(ok_decor *d, int i)
{
    ok_piece p;
    if (i < 0 || i >= d->count) return 0;
    p = d->piece[i];
    p.turn = (uint8_t)((p.turn + 1) & 3);
    if (!ok_decor_fits(d, &p, i)) return 0;
    d->piece[i] = p;
    return 1;
}

void ok_decor_sell(ok_decor *d, int i)
{
    const ok_piece_kind *k;
    if (i < 0 || i >= d->count) return;
    k = ok_decor_kind(d->piece[i].kind);
    if (k) ok_decor_earn(d, k->price / 2);
    memmove(&d->piece[i], &d->piece[i + 1], sizeof d->piece[0] * (size_t)(d->count - i - 1));
    d->count--;
}

int ok_decor_at(const ok_decor *d, int col, int row)
{
    int i, flat = -1;
    for (i = d->count - 1; i >= 0; i--) {
        int w, dd;
        ok_decor_size(&d->piece[i], &w, &dd);
        if (col < d->piece[i].col || col >= d->piece[i].col + w || row < d->piece[i].row || row >= d->piece[i].row + dd)
            continue;
        if (!kinds[d->piece[i].kind].flat) return i;
        if (flat < 0) flat = i;
    }
    return flat;
}

int ok_decor_paint(ok_decor *d, int wallpaper, int floor)
{
    if (wallpaper < 0 || wallpaper >= OK_DECOR_PAINTS || floor < 0 || floor >= OK_DECOR_PAINTS) return 0;
    if (wallpaper == d->wallpaper && floor == d->floor) return 1;
    if (d->bananas < OK_DECOR_PAINT_PRICE) return 0;
    d->bananas -= OK_DECOR_PAINT_PRICE;
    d->wallpaper = (uint8_t)wallpaper;
    d->floor = (uint8_t)floor;
    return 1;
}

void ok_decor_earn(ok_decor *d, int bananas)
{
    int b = (int)d->bananas + (bananas > 0 ? bananas : 0);
    d->bananas = (uint16_t)(b > 999 ? 999 : b);
}

uint32_t ok_decor_sum(const ok_decor *d)
{
    uint32_t s = 2166136261u;
    int i;
    for (i = 0; i < d->count; i++) {
        const uint8_t *b = &d->piece[i].kind;
        s = (s ^ b[0]) * 16777619u, s = (s ^ b[1]) * 16777619u, s = (s ^ b[2]) * 16777619u, s = (s ^ b[3]) * 16777619u;
    }
    s = (s ^ d->wallpaper) * 16777619u;
    return (s ^ (uint32_t)(d->floor + 1)) * 16777619u;
}
