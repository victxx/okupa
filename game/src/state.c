#define _POSIX_C_SOURCE 200809L /* fileno, fsync */
#include "okupa_state.h"
#include "okupa_keccak.h"
#include "okupa_save.h"
#include <string.h>
#include <unistd.h>

static void put(uint8_t *p, uint64_t v, int n)
{
    int i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t get(const uint8_t *p, int n)
{
    uint64_t v = 0;
    while (n--) v = v << 8 | p[n];
    return v;
}

size_t ok_state_picture_size(int kind)
{
    return kind == OK_PIC_DRAWING ? 512 : kind == OK_PIC_TONES ? 1024 : 0;
}

ok_result ok_state_encode(const ok_state *s, uint64_t generation, uint8_t *out, size_t cap, size_t *len)
{
    uint8_t *q = out;
    int i;
    size_t need = 8 + 8 + 29 + 512 + 1 + 8 + 8 + 1 + 2 + 32 + 1 + 8 + 32, pic;
    if (!s || !out || !len) return OK_ARGUMENT;
    if (s->wall_count < 0 || s->wall_count > OK_STATE_WALL || s->tx_len > OK_STATE_TX_MAX || !generation)
        return OK_ARGUMENT;
    for (i = 0; i < s->wall_count; i++) {
        if (!(pic = ok_state_picture_size(s->wall_kind[i]))) return OK_ARGUMENT;
        need += 1 + pic;
    }
    if (s->tx_kind != OK_TX_NONE && !s->tx_len) return OK_ARGUMENT;
    if (s->tx_pic_kind && !ok_state_picture_size(s->tx_pic_kind)) return OK_ARGUMENT;
    need += s->tx_len + ok_state_picture_size(s->tx_pic_kind);
    if (need > cap) return OK_RANGE;
    memcpy(q, "OKST", 4);
    q[4] = OK_STATE_VERSION;
    q[5] = q[6] = q[7] = 0;
    q += 8;
    put(q, generation, 8), q += 8;
    memcpy(q, s->pet.need, OK_NEEDS), q += OK_NEEDS;
    *q++ = s->pet.asleep;
    put(q, (uint64_t)s->pet.updated, 8), q += 8;
    for (i = 0; i < OK_NEEDS; i++, q += 4) put(q, s->pet.carry[i], 4);
    memcpy(q, s->page, 512), q += 512;
    *q++ = (uint8_t)s->wall_count;
    for (i = 0; i < s->wall_count; i++) {
        pic = ok_state_picture_size(s->wall_kind[i]);
        *q++ = s->wall_kind[i];
        memcpy(q, s->wall[i], pic), q += pic;
    }
    put(q, s->chain_id, 8), q += 8;
    put(q, s->next_nonce, 8), q += 8;
    *q++ = s->tx_kind;
    put(q, s->tx_len, 2), q += 2;
    memcpy(q, s->tx, s->tx_len), q += s->tx_len;
    memcpy(q, s->tx_hash, 32), q += 32;
    *q++ = s->tx_pic_kind;
    pic = ok_state_picture_size(s->tx_pic_kind);
    memcpy(q, s->tx_pic, pic), q += pic;
    put(q, s->watch_from, 8), q += 8;
    ok_keccak256(out, (size_t)(q - out), q);
    q += 32;
    *len = (size_t)(q - out);
    return OK_OK;
}

ok_result ok_state_decode(const uint8_t *in, size_t n, ok_state *s, uint64_t *generation)
{
    static ok_state t; /* large: kept off the stack */
    const uint8_t *q = in, *end;
    uint8_t hash[32];
    int i;
    size_t pic;
    if (!in || !s || !generation) return OK_ARGUMENT;
    if (n < 8 + 8 + 32 || n > OK_STATE_WIRE_MAX || memcmp(in, "OKST", 4) || in[4] != OK_STATE_VERSION || in[5] || in[6] ||
        in[7])
        return OK_FORMAT;
    ok_keccak256(in, n - 32, hash);
    if (memcmp(hash, in + n - 32, 32)) return OK_FORMAT;
    end = in + n - 32;
    memset(&t, 0, sizeof t);
#define NEED(k)                                                                                                        \
    do {                                                                                                               \
        if ((size_t)(end - q) < (size_t)(k)) return OK_FORMAT;                                                         \
    } while (0)
    q += 8;
    NEED(8 + 29 + 512 + 1);
    *generation = get(q, 8), q += 8;
    if (!*generation) return OK_FORMAT;
    memcpy(t.pet.need, q, OK_NEEDS), q += OK_NEEDS;
    t.pet.asleep = *q++;
    t.pet.updated = (int64_t)get(q, 8), q += 8;
    for (i = 0; i < OK_NEEDS; i++, q += 4) t.pet.carry[i] = (uint32_t)get(q, 4);
    for (i = 0; i < OK_NEEDS; i++)
        if (t.pet.need[i] > OK_NEED_MAX) return OK_FORMAT;
    if (t.pet.asleep > 1) return OK_FORMAT;
    memcpy(t.page, q, 512), q += 512;
    t.wall_count = *q++;
    if (t.wall_count > OK_STATE_WALL) return OK_FORMAT;
    for (i = 0; i < t.wall_count; i++) {
        NEED(1);
        t.wall_kind[i] = *q++;
        if (!(pic = ok_state_picture_size(t.wall_kind[i]))) return OK_FORMAT;
        NEED(pic);
        memcpy(t.wall[i], q, pic), q += pic;
    }
    NEED(8 + 8 + 1 + 2);
    t.chain_id = get(q, 8), q += 8;
    t.next_nonce = get(q, 8), q += 8;
    t.tx_kind = *q++;
    t.tx_len = (uint16_t)get(q, 2), q += 2;
    if (t.tx_kind > OK_TX_PICTURE || t.tx_len > OK_STATE_TX_MAX || (t.tx_kind == OK_TX_NONE) != (t.tx_len == 0))
        return OK_FORMAT;
    NEED(t.tx_len + 32 + 1);
    memcpy(t.tx, q, t.tx_len), q += t.tx_len;
    memcpy(t.tx_hash, q, 32), q += 32;
    t.tx_pic_kind = *q++;
    pic = ok_state_picture_size(t.tx_pic_kind);
    if (t.tx_pic_kind && !pic) return OK_FORMAT;
    NEED(pic + 8);
    memcpy(t.tx_pic, q, pic), q += pic;
    t.watch_from = get(q, 8), q += 8;
    if (q != end) return OK_FORMAT;
#undef NEED
    *s = t;
    return OK_OK;
}

/* ---- The two files ------------------------------------------------------- */

static void name(const ok_state_store *st, int slot, char out[128])
{
    snprintf(out, 128, "%sstate-%c.bin", st->dir, slot ? 'b' : 'a');
}

/* 1 read into wire (*n bytes), 0 no file, -1 unreadable or too big. */
static int read_slot(ok_state_store *st, int slot, size_t *n)
{
    char path[128];
    FILE *f;
    int ok;
    name(st, slot, path);
    if (!(f = fopen(path, "rb"))) return 0;
    *n = fread(st->wire, 1, sizeof st->wire, f);
    ok = fgetc(f) == EOF && !ferror(f);
    fclose(f);
    return ok ? 1 : -1;
}

int ok_state_open(ok_state_store *st, const char *dir, ok_state *s)
{
    static ok_state found[2];
    uint64_t gen[2] = {0, 0};
    int slot, have[2] = {0, 0}, seen[2] = {0, 0};
    memset(st, 0, sizeof *st);
    st->active = -1;
    if (!dir || strlen(dir) >= sizeof st->dir) {
        st->blocked = 1;
        return -1;
    }
    strcpy(st->dir, dir);
    for (slot = 0; slot < 2; slot++) {
        size_t n = 0;
        int r = read_slot(st, slot, &n);
        seen[slot] = r != 0;
        if (r == 1 && ok_state_decode(st->wire, n, &found[slot], &gen[slot]) == OK_OK) have[slot] = 1;
    }
    if (!seen[0] && !seen[1]) return 0;
    if (!have[0] && !have[1]) { /* files there, none good: touch nothing */
        st->blocked = 1;
        return -1;
    }
    slot = have[0] && have[1] ? (gen[1] > gen[0]) : have[1];
    st->fell_back = seen[!slot] && (!have[!slot] || gen[!slot] > gen[slot]);
    if (have[!slot] && gen[!slot] == gen[slot]) { /* two different saves claim the same turn: ambiguous */
        st->blocked = 1;
        return -1;
    }
    *s = found[slot];
    st->generation = gen[slot];
    st->active = slot;
    return 1;
}

int ok_state_save(ok_state_store *st, const ok_state *s)
{
    static uint8_t back[512];
    char path[128];
    size_t n, off;
    FILE *f;
    int slot, ok;
    st->fail_stage = 1;
    if (st->blocked) return 0;
    st->fail_stage = 2;
    if (ok_state_encode(s, st->generation + 1, st->wire, sizeof st->wire, &n) != OK_OK) return 0;
    slot = st->active == 0 ? 1 : 0;
    name(st, slot, path);
    st->fail_stage = 3;
    if (!(f = fopen(path, "wb"))) return 0;
    st->fail_stage = 4;
    ok = fwrite(st->wire, 1, n, f) == n;
    if (ok) {
        st->fail_stage = 5;
        ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    }
    if (fclose(f) != 0 && ok) {
        st->fail_stage = 6;
        ok = 0;
    }
    if (!ok) return 0;
    st->fail_stage = 7;
    if (!(f = fopen(path, "rb"))) return 0;
    st->fail_stage = 8;
    for (off = 0; off < n;) {
        size_t k = n - off < sizeof back ? n - off : sizeof back;
        if (fread(back, 1, k, f) != k || memcmp(back, st->wire + off, k)) {
            fclose(f);
            return 0;
        }
        off += k;
    }
    ok = fgetc(f) == EOF;
    fclose(f);
    if (!ok) return 0;
    st->generation++;
    st->active = slot;
    st->fell_back = 0;
    st->fail_stage = 0;
    return 1;
}

/* ---- Files of earlier versions ----------------------------------------- */

static FILE *open_in(const char *dir, const char *file, const char *mode)
{
    char path[160];
    snprintf(path, sizeof path, "%s%s", dir, file);
    return fopen(path, mode);
}

int ok_state_import(const char *dir, const char *net, ok_state *s)
{
    char file[48];
    FILE *f;
    int found = 0;
    {
        uint8_t buf[OK_SAVE_SIZE + 1];
        size_t n;
        if ((f = open_in(dir, "save.bin", "rb")) || (f = open_in(dir, "save.tmp", "rb"))) {
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
            if (ok_save_read(buf, n, &s->pet, s->page) == OK_OK) found++;
        }
    }
    if ((f = open_in(dir, "wall.bin", "rb"))) {
        uint8_t count = 0;
        int i;
        if (fread(&count, 1, 1, f) == 1 && count <= OK_STATE_WALL) {
            for (i = 0; i < count && fread(s->wall[i], 512, 1, f) == 1; i++) s->wall_kind[i] = OK_PIC_DRAWING;
            s->wall_count = i;
            found++;
        }
        fclose(f);
    }
    snprintf(file, sizeof file, "nonce-%.20s.txt", net);
    if ((f = open_in(dir, file, "r"))) {
        unsigned long long v;
        if (fscanf(f, "%llu", &v) == 1) s->next_nonce = v, found++;
        fclose(f);
    }
    snprintf(file, sizeof file, "pending-%.20s.hex", net);
    if ((f = open_in(dir, file, "r"))) {
        static char hex[2 * OK_STATE_TX_MAX + 4];
        size_t n = 0;
        unsigned v;
        if (fgets(hex, sizeof hex, f))
            while (n < OK_STATE_TX_MAX && sscanf(hex + 2 * n, "%2x", &v) == 1) s->tx[n++] = (uint8_t)v;
        fclose(f);
        if (n > 1) {
            s->tx_len = (uint16_t)n;
            s->tx_kind = OK_TX_OLD;
            ok_keccak256(s->tx, n, s->tx_hash);
            found++;
        }
    }
    if (s->tx_kind && (f = open_in(dir, "drawing-sent.bin", "rb"))) { /* the drawing that transaction carries */
        uint8_t hash[32];
        if (fread(hash, 1, 32, f) == 32 && !memcmp(hash, s->tx_hash, 32) && fread(s->tx_pic, 1, 512, f) == 512) {
            s->tx_kind = OK_TX_PICTURE;
            s->tx_pic_kind = OK_PIC_DRAWING;
        }
        fclose(f);
    }
    snprintf(file, sizeof file, "watch-%.20s.txt", net);
    if (s->tx_kind && (f = open_in(dir, file, "r"))) {
        unsigned long long v;
        if (fscanf(f, "%llu", &v) == 1) s->watch_from = v;
        fclose(f);
    }
    return found;
}
