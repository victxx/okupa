#include "okupa_decor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ok_decor d, e;
    int a, b, c, w, dd;
    uint32_t sum;

    ok_decor_init(&d);
    assert(d.bananas == OK_DECOR_START && !d.count && !strcmp(ok_decor_kind(OK_PIECE_TV)->name, "Old TV"));
    sum = ok_decor_sum(&d);

    /* Nothing on the attic's own things, off the floor, or without the bananas for it. */
    assert(ok_decor_fixed(1, 1) && !ok_decor_fixed(7, 5)); /* the bed; the middle of the floor is free */
    assert(ok_decor_buy(&d, OK_PIECE_ARMCHAIR, 1, 1) == -1);
    assert(ok_decor_buy(&d, OK_PIECE_ARMCHAIR, 13, 11) == -1); /* 2x2 past the edge */
    assert(ok_decor_buy(&d, OK_PIECE_BANANA_TREE, 7, 5) == -1 && d.bananas == 10); /* 12 bananas: not yet */

    /* An armchair, then a rug under it (flat things go under), but not two chairs in one place. */
    a = ok_decor_buy(&d, OK_PIECE_ARMCHAIR, 6, 5);
    assert(a == 0 && d.bananas == 4 && ok_decor_sum(&d) != sum);
    b = ok_decor_buy(&d, OK_PIECE_RUG, 5, 4);
    assert(b == 1 && d.bananas == 0);
    assert(ok_decor_at(&d, 6, 5) == a && ok_decor_at(&d, 5, 4) == b && ok_decor_at(&d, 9, 9) == -1);
    ok_decor_earn(&d, 20);
    assert(ok_decor_buy(&d, OK_PIECE_BEANBAG, 7, 6) == -1); /* on the armchair */
    c = ok_decor_buy(&d, OK_PIECE_SHELF, 8, 4);
    assert(c == 2 && d.bananas == 12);

    /* Turned, the shelf is 1x2; moved where it fits, not where it does not. */
    ok_decor_size(&d.piece[c], &w, &dd);
    assert(w == 2 && dd == 1);
    assert(ok_decor_turn(&d, c) && d.piece[c].turn == 1);
    ok_decor_size(&d.piece[c], &w, &dd);
    assert(w == 1 && dd == 2);
    assert(!ok_decor_move(&d, c, 6, 5) && d.piece[c].col == 8); /* onto the armchair: stays */
    assert(ok_decor_move(&d, c, 9, 9) && d.piece[c].col == 9 && d.piece[c].row == 9);
    assert(!ok_decor_move(&d, c, 13, 11)); /* off the floor */

    /* Paint, and selling back for half. */
    assert(ok_decor_paint(&d, 2, 1) && d.wallpaper == 2 && d.floor == 1 && d.bananas == 9);
    assert(!ok_decor_paint(&d, 4, 0));
    e = d;
    ok_decor_sell(&d, a);
    assert(d.count == 2 && d.bananas == 12 && d.piece[0].kind == OK_PIECE_RUG);
    assert(ok_decor_sum(&d) != ok_decor_sum(&e));

    /* At most OK_DECOR_MAX pieces, and the count stays under 1000. */
    ok_decor_earn(&d, 5000);
    assert(d.bananas == 999);
    for (a = 0; a < 20; a++) ok_decor_buy(&d, OK_PIECE_TEDDY, (a % 7) * 2, 4 + (a / 7) * 2);
    assert(d.count <= OK_DECOR_MAX);
    puts("decor: furniture bought with bananas, on free floor only, rugs under things, turned, moved, sold; paint");
    return 0;
}
