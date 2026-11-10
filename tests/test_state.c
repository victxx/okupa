#define _POSIX_C_SOURCE 200809L
#include "okupa_act.h"
#include "okupa_keccak.h"
#include "okupa_save.h"
#include "okupa_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DIR "build/test-state.d/"

static void put_file(const char *file, const void *data, size_t n)
{
    char path[160];
    FILE *f;
    snprintf(path, sizeof path, DIR "%s", file);
    assert((f = fopen(path, "wb")) && fwrite(data, 1, n, f) == n && !fclose(f));
}

static size_t get_file(const char *file, uint8_t *out, size_t cap)
{
    char path[160];
    FILE *f;
    size_t n;
    snprintf(path, sizeof path, DIR "%s", file);
    if (!(f = fopen(path, "rb"))) return 0;
    n = fread(out, 1, cap, f);
    fclose(f);
    return n;
}

static void wipe_dir(void)
{
    static const char *files[] = {"state-a.bin", "state-b.bin", "save.bin", "save.tmp", "wall.bin", "drawing-sent.bin",
                                  "nonce-mainnet.txt", "pending-mainnet.hex", "watch-mainnet.txt"};
    char path[160];
    unsigned i;
    mkdir("build", 0700);
    mkdir(DIR, 0700);
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, DIR "%s", files[i]);
        remove(path);
    }
}

int main(void)
{
    static ok_state s, r;
    static ok_state_store st;
    static uint8_t wire[OK_STATE_WIRE_MAX], copy[OK_STATE_WIRE_MAX];
    uint64_t gen;
    size_t n, i;

    /* A full snapshot round-trips: pet, page, a drawing and a photo on the wall, the transaction as signed. */
    memset(&s, 0, sizeof s);
    ok_pet_init(&s.pet, 1800000000);
    ok_pet_update(&s.pet, 1800001234);
    ok_drawing_set(s.page, 1, 2, 1);
    s.wall_count = 2;
    s.wall_kind[0] = OK_PIC_DRAWING;
    memset(s.wall[0], 0xa5, 512);
    s.wall_kind[1] = OK_PIC_TONES;
    for (i = 0; i < 1024; i++) s.wall[1][i] = (uint8_t)i;
    s.chain_id = 1;
    s.next_nonce = 7;
    s.tx_kind = OK_TX_PICTURE;
    s.tx_len = 600;
    for (i = 0; i < 600; i++) s.tx[i] = (uint8_t)(i * 3);
    ok_keccak256(s.tx, 600, s.tx_hash);
    s.tx_pic_kind = OK_PIC_DRAWING;
    memset(s.tx_pic, 0x3c, 512);
    s.watch_from = 26360000;
    s.tx_fee_level = 2;
    ok_decor_init(&s.decor);
    ok_decor_earn(&s.decor, 20);
    assert(ok_decor_buy(&s.decor, OK_PIECE_ARMCHAIR, 6, 5) == 0 && ok_decor_buy(&s.decor, OK_PIECE_RUG, 5, 4) == 1);
    ok_decor_turn(&s.decor, 0);
    ok_decor_paint(&s.decor, 3, 2);
    s.tx_page = 1;
    s.tx_older = 2;
    memset(s.tx_older_hash[0], 0x61, 32);
    memset(s.tx_older_hash[1], 0x62, 32);
    assert(ok_state_encode(&s, 5, wire, sizeof wire, &n) == OK_OK && !memcmp(wire, "OKST\x01\0\0\0", 8));
    assert(ok_state_decode(wire, n, &r, &gen) == OK_OK && gen == 5);
    assert(!memcmp(&r.pet, &s.pet, sizeof s.pet) && !memcmp(r.page, s.page, 512) && r.wall_count == 2);
    assert(!memcmp(r.wall[0], s.wall[0], 512) && !memcmp(r.wall[1], s.wall[1], 1024) && r.wall_kind[1] == OK_PIC_TONES);
    assert(r.next_nonce == 7 && r.tx_len == 600 && !memcmp(r.tx, s.tx, 600) && !memcmp(r.tx_hash, s.tx_hash, 32));
    assert(r.tx_pic_kind == OK_PIC_DRAWING && !memcmp(r.tx_pic, s.tx_pic, 512) && r.watch_from == 26360000);
    assert(r.decor.count == 2 && r.decor.piece[0].kind == OK_PIECE_ARMCHAIR && r.decor.piece[0].turn == 1);
    assert(r.decor.piece[1].kind == OK_PIECE_RUG && r.decor.wallpaper == 3 && r.decor.floor == 2 && r.decor.bananas == 17);
    assert(r.tx_page == 1 && r.tx_fee_level == 2 && r.tx_older == 2 && r.tx_older_hash[1][31] == 0x62); /* the versions a raised fee left */

    /* Any damage, a short read or trailing bytes: refused whole. */
    for (i = 0; i < n * 8; i += 13) {
        wire[i / 8] ^= (uint8_t)(1u << (i % 8));
        assert(ok_state_decode(wire, n, &r, &gen) == OK_FORMAT);
        wire[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
    assert(ok_state_decode(wire, n - 1, &r, &gen) == OK_FORMAT);
    assert(ok_state_decode(wire, n + 1, &r, &gen) == OK_FORMAT);
    /* Shapes that make no sense are not written. */
    s.wall_kind[1] = 9;
    assert(ok_state_encode(&s, 1, wire, sizeof wire, &n) == OK_ARGUMENT);
    s.wall_kind[1] = OK_PIC_TONES;
    assert(ok_state_encode(&s, 0, wire, sizeof wire, &n) == OK_ARGUMENT); /* generation 0 is never a save */

    /* The store: nothing yet, then two files taking turns. */
    wipe_dir();
    assert(ok_state_open(&st, DIR, &r) == 0 && !st.blocked);
    assert(ok_state_save(&st, &s) && st.active == 0 && st.generation == 1);
    s.next_nonce = 8;
    assert(ok_state_save(&st, &s) && st.active == 1 && st.generation == 2);
    s.next_nonce = 9;
    assert(ok_state_save(&st, &s) && st.active == 0 && st.generation == 3);
    assert(ok_state_open(&st, DIR, &r) == 1 && r.next_nonce == 9 && st.generation == 3 && !st.fell_back);

    /* A save cut short (the newer file half written): the older one is used,
     * and the next save goes over the damaged one. */
    n = get_file("state-a.bin", copy, sizeof copy);
    put_file("state-a.bin", copy, n / 2);
    assert(ok_state_open(&st, DIR, &r) == 1 && r.next_nonce == 8 && st.generation == 2 && st.fell_back);
    assert(ok_state_save(&st, &s) && st.active == 0 && st.generation == 3);
    assert(ok_state_open(&st, DIR, &r) == 1 && r.next_nonce == 9 && !st.fell_back);

    /* Both damaged: blocked; nothing is written over them. */
    put_file("state-a.bin", copy, 10);
    put_file("state-b.bin", "junk", 4);
    assert(ok_state_open(&st, DIR, &r) == -1 && st.blocked && !ok_state_save(&st, &s) && st.fail_stage == 1);
    assert(get_file("state-b.bin", copy, sizeof copy) == 4);

    /* Two different saves with the same generation: ambiguous, blocked. */
    assert(ok_state_encode(&s, 4, wire, sizeof wire, &n) == OK_OK);
    put_file("state-a.bin", wire, n);
    s.next_nonce = 10;
    assert(ok_state_encode(&s, 4, wire, sizeof wire, &n) == OK_OK);
    put_file("state-b.bin", wire, n);
    assert(ok_state_open(&st, DIR, &r) == -1 && st.blocked);

    /* The files of earlier versions come in once: pet and page, the wall, the
     * nonce, the transaction on its way (hex) and the drawing it carries. */
    wipe_dir();
    {
        uint8_t save[OK_SAVE_SIZE], wall[1 + 2 * 512], sent[32 + 512];
        static char hex[2 * 600 + 2];
        ok_pet pet;
        memset(&r, 0, sizeof r);
        ok_pet_init(&pet, 1800000000);
        ok_save_write(&pet, s.page, save);
        put_file("save.bin", save, sizeof save);
        wall[0] = 2;
        memset(wall + 1, 0x11, 512);
        memset(wall + 513, 0x22, 512);
        put_file("wall.bin", wall, sizeof wall);
        put_file("nonce-mainnet.txt", "3\n", 2);
        for (i = 0; i < 600; i++) snprintf(hex + 2 * i, 3, "%02x", s.tx[i]);
        put_file("pending-mainnet.hex", hex, 1200);
        memcpy(sent, s.tx_hash, 32);
        memset(sent + 32, 0x77, 512);
        put_file("drawing-sent.bin", sent, sizeof sent);
        put_file("watch-mainnet.txt", "26360001\n", 9);
        assert(ok_state_import(DIR, "mainnet", &r) == 4);
        assert(!memcmp(r.pet.need, pet.need, OK_NEEDS) && !memcmp(r.page, s.page, 512));
        assert(r.wall_count == 2 && r.wall_kind[1] == OK_PIC_DRAWING && r.wall[1][0] == 0x22);
        assert(r.next_nonce == 3 && r.tx_len == 600 && !memcmp(r.tx, s.tx, 600) && !memcmp(r.tx_hash, s.tx_hash, 32));
        assert(r.tx_kind == OK_TX_PICTURE && r.tx_page && r.tx_pic_kind == OK_PIC_DRAWING && r.tx_pic[0] == 0x77);
        assert(r.watch_from == 26360001);
        assert(ok_state_open(&st, DIR, &s) == 0 && ok_state_save(&st, &r) && ok_state_open(&st, DIR, &s) == 1);
        assert(s.next_nonce == 3 && s.tx_len == 600);
    }
    wipe_dir();
    puts("state: one snapshot round-trips; two files take turns; a cut save falls back, damage blocks; old files import");
    return 0;
}
