/* The demo video's picture and sound, made by the game's own code on the
 * host: every DSi frame below comes from the same ok_ui / ok_tech / intro
 * renderers the ROM runs (software 3D here, the DSi's GPU on the console).
 * Numbers on the technical screens are from real runs, noted where filled.
 * Writes raw 1280x720 RGB24 frames at 30 fps to stdout and a 32 kHz mono
 * WAV of the game's sound effects (no music) to argv[1].
 *   build/okupa-video audio.wav | ffmpeg -f rawvideo ... */
#include "okupa_act.h"
#include "okupa_facts.h"
#include "okupa_font.h"
#include "okupa_house.h"
#include "okupa_intro.h"
#include "okupa_music.h"
#include "okupa_session.h"
#include "okupa_tech.h"
#include "okupa_text.h"
#include "okupa_tx.h"
#include "okupa_keys.h"
#include "okupa_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VW 1280
#define VH 720
#define FPS 30
#define RATE 32000

static uint8_t frame[VW * VH * 3];
static uint16_t top[OK_UI_W * OK_UI_H], bottom[OK_UI_W * OK_UI_H];
static ok_music music;
static FILE *wav;
static uint32_t samples, frames_out;
static ok_ui ui;
static ok_pet pet;
static ok_tech tech;

/* ---- drawing on the 1280x720 frame ---------------------------------------- */

static void px(int x, int y, int r, int g, int b)
{
    uint8_t *p;
    if (x < 0 || y < 0 || x >= VW || y >= VH) return;
    p = frame + (y * VW + x) * 3;
    p[0] = (uint8_t)r, p[1] = (uint8_t)g, p[2] = (uint8_t)b;
}

static void box(int x, int y, int w, int h, int r, int g, int b)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) px(i, j, r, g, b);
}

static int text_w(const ok_font_sans *f, const char *s, int scale)
{
    return ok_text_width(f, s) * scale;
}

static void text(const ok_font_sans *f, int x, int y, const char *s, int scale, int r, int g, int b)
{
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s, row, bit;
        if (ch < 32 || ch >= 127) ch = '?';
        for (row = 0; row < f->height; row++) {
            uint16_t bits = f->rows[(ch - 32) * f->height + row];
            for (bit = 0; bit < 16; bit++)
                if (bits >> bit & 1) box(x + (int)bit * scale, y + (int)row * scale, scale, scale, r, g, b);
        }
        x += f->advance[ch - 32] * scale;
    }
}

/* A DS colour (BGR555) to 8-bit channels. */
static void ds_rgb(uint16_t c, int *r, int *g, int *b)
{
    *r = (c & 31) * 255 / 31;
    *g = (c >> 5 & 31) * 255 / 31;
    *b = (c >> 10 & 31) * 255 / 31;
}

/* One DSi screen at 3/2 scale (256x192 -> 384x288), nearest pixel. */
static void screen(const uint16_t *s, int x0, int y0)
{
    int x, y;
    for (y = 0; y < 288; y++)
        for (x = 0; x < 384; x++) {
            int r, g, b;
            ds_rgb(s[(y * 2 / 3) * OK_UI_W + x * 2 / 3], &r, &g, &b);
            px(x0 + x, y0 + y, r, g, b);
        }
}

/* ---- one segment's words ---------------------------------------------------- */

typedef struct {
    const char *title;
    const char *lines[7];
    const char *subtitle[2];
} words;

static void wrap_centre(const char *s, int y)
{
    int w = text_w(&ok_sans_bold, s, 2);
    text(&ok_sans_bold, (VW - w) / 2, y, s, 2, 250, 248, 240);
}

static void compose(const words *w, int fade)
{
    int i, y = 150;
    (void)fade;
    box(0, 0, VW, VH, 22, 20, 18);
    /* the console: two screens, a hinge between them */
    box(80, 28, 416, 616, 46, 44, 42);
    screen(top, 96, 44);
    box(96, 334, 384, 14, 30, 28, 26);
    screen(bottom, 96, 350);
    /* what is happening */
    if (w->title) text(&ok_sans_bold, 548, 64, w->title, 3, 250, 200, 60);
    for (i = 0; i < 7 && w->lines[i]; i++, y += 40) text(&ok_sans_regular, 548, y, w->lines[i], 2, 225, 222, 214);
    /* subtitles */
    box(0, 648, VW, 72, 8, 8, 8);
    if (w->subtitle[0]) wrap_centre(w->subtitle[0], w->subtitle[1] ? 654 : 670);
    if (w->subtitle[1]) wrap_centre(w->subtitle[1], 684);
    text(&ok_sans_regular, 548, 600, "Frames: the game's own C code, rendered on a Mac", 1, 130, 126, 118);
    text(&ok_sans_regular, 548, 616, "(the DSi runs the same code). Data: real runs.", 1, 130, 126, 118);
}

/* ---- time ---------------------------------------------------------------------- */

static void emit(const words *w)
{
    uint32_t want;
    compose(w, 0);
    fwrite(frame, 1, sizeof frame, stdout);
    frames_out++;
    /* the sound for this frame */
    want = (uint32_t)((uint64_t)frames_out * RATE / FPS);
    while (samples < want) {
        int16_t buf[256];
        int n = (int)(want - samples > 256 ? 256 : want - samples);
        ok_music_render(&music, buf, n);
        fwrite(buf, 2, (size_t)n, wav);
        samples += (uint32_t)n;
    }
}

/* Two game ticks a video frame (60 Hz game, 30 fps video); sounds go to the mix. */
static void tick(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        ok_ui_tick(&ui, &pet);
        if (ui.sound) {
            ok_music_sfx(&music, ui.sound - 1);
            ui.sound = 0;
        }
    }
}

static void game(int hour)
{
    ok_ui_render(&ui, &pet, hour, 20, ok_font(), top, bottom);
}

static void touch(int x, int y)
{
    ok_ui_touch(&ui, &pet, x, y, 0, 1800000000);
    ok_ui_release(&ui);
    if (ui.sound) {
        ok_music_sfx(&music, ui.sound - 1);
        ui.sound = 0;
    }
}

static void le32(uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, wav);
}

static void wav_header(uint32_t n)
{
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    le32(36 + n * 2);
    fwrite("WAVEfmt ", 1, 8, wav);
    le32(16);
    le32(1 | 1u << 16);
    le32(RATE);
    le32(RATE * 2);
    le32(2 | 16u << 16);
    fwrite("data", 1, 4, wav);
    le32(n * 2);
}

/* A little house, drawn stroke by stroke (k of n). */
static void sketch_house(uint8_t *d, int k, int n)
{
    int x, y, i = 0, upto;
    static int pts[4096][2];
    int count = 0;
    for (x = 14; x <= 50; x++) pts[count][0] = x, pts[count++][1] = 55;
    for (y = 55; y >= 31; y--) pts[count][0] = 50, pts[count++][1] = y;
    for (x = 50; x >= 32; x--) pts[count][0] = x, pts[count++][1] = 31 - (50 - x);
    for (x = 32; x >= 14; x--) pts[count][0] = x, pts[count++][1] = 13 + (32 - x);
    for (y = 31; y <= 55; y++) pts[count][0] = 14, pts[count++][1] = y;
    for (y = 55; y >= 43; y--) pts[count][0] = 28, pts[count++][1] = y;
    for (x = 28; x <= 35; x++) pts[count][0] = x, pts[count++][1] = 43;
    for (y = 43; y <= 55; y++) pts[count][0] = 35, pts[count++][1] = y;
    for (i = 0; i < 9; i++) pts[count][0] = 40 + i % 3, pts[count++][1] = 36 + i / 3;
    upto = count * k / n;
    memset(d, 0, OK_DRAWING_BYTES);
    for (i = 0; i < upto; i++) ok_drawing_set(d, (unsigned)pts[i][0], (unsigned)pts[i][1], 1);
}

/* The facts as the console recorded them in real runs (see each note). */
static void real_facts(void)
{
    ok_facts *f = &ok_facts_now;
    static const uint8_t genesis[32] = {0xd4, 0xe5, 0x67, 0x40, 0xf8, 0x76, 0xae, 0xf8, 0xc0, 0x10, 0xb8, 0x6a, 0x40, 0xd5, 0xf5, 0x67,
                                        0x45, 0xa1, 0x18, 0xd0, 0x90, 0x6a, 0x34, 0xe6, 0x9a, 0xec, 0x8c, 0x0d, 0xb1, 0xcb, 0x8f, 0xa3};
    memset(f, 0, sizeof *f);
    strcpy(f->network, "mainnet");
    f->chain_id = 1;
    memcpy(f->genesis, genesis, 32);
    f->fork_hash[0] = 0x07, f->fork_hash[1] = 0xc9, f->fork_hash[2] = 0x46, f->fork_hash[3] = 0x2e;
    /* DSi net-log, 2026-11-07 (run/dsi-mind-live-20261107): DNS list 24
     * nodes; discv4 322 seen, 326 replies, 5 on mainnet; 25 dials, 18 RLPx
     * handshakes, 7 Hellos, 17 "too many peers", none reached Status. */
    strcpy(f->dns_tree, "all.mainnet.ethdisco.net");
    f->dns_verified = 1, f->dns_seq = 1, f->dns_nodes = 24;
    f->v4_received = 326, f->v4_nodes = 322;
    f->candidates = 29, f->tried = 25, f->reached_status = 0, f->full = 17;
    f->peer_count = 2;
    f->peers[0].ip[0] = 152, f->peers[0].ip[1] = 233, f->peers[0].ip[2] = 46, f->peers[0].ip[3] = 139;
    f->peers[0].port = 30303, f->peers[0].stage = OK_STAGE_HELLO, f->peers[0].disconnect = 4;
    strcpy(f->peers[0].client, "Nethermind/v2.2.1");
    f->peers[1].ip[0] = 185, f->peers[1].ip[1] = 107, f->peers[1].ip[2] = 71, f->peers[1].ip[3] = 131;
    f->peers[1].port = 30303, f->peers[1].stage = OK_STAGE_HELLO, f->peers[1].disconnect = 4;
    strcpy(f->peers[1].client, "Nethermind/v2.0.0");
    strcpy(f->address, "0xEd86Ee1156A375bB5B43C43829173243251c5115");
    f->scribble_samples = 480;
    /* DSi episodes, 2026-11-07: 6 decisions verified on the console */
    strcpy(f->og_host, "compute-network-20.integratenetwork.work");
    strcpy(f->og_model, "0GM-1.0-35B-A3B");
    strcpy(f->og_signer, "0xDdaEaac4d8525d72DfD20cc6dE0f28eb3d320Bfb");
    f->og_verified = 1;
    strcpy(f->og_decision, "peek");
    f->dsi_mode = 1;
    strcpy(f->build, "okupa");
}

int main(int argc, char **argv)
{
    int f, k;
    static uint8_t house[OK_DRAWING_BYTES];
    if (argc < 2 || !(wav = fopen(argv[1], "wb"))) {
        fprintf(stderr, "usage: okupa-video out.wav > frames.rgb\n");
        return 2;
    }
    wav_header(0);
    ok_music_init(&music, RATE);
    /* No music: the quiet arrangement plays no notes, but keep the level up
     * so the game's sound effects (taps, eating, pops, the jingle) are heard. */
    music.tune = music.next_tune = OK_TUNE_QUIET;
    music.level = music.level_goal = 65536;
    ok_pet_init(&pet, 1800000000);
    ok_ui_init(&ui);
    ok_tech_init(&tech);
    for (k = 0; k < 12; k++) ok_house_frame(k, NULL);
    real_facts();

    /* 1. The splash (6 s). */
    {
        words w = {"Okupa", {"A tiny squatter that moved", "into a Nintendo DSi (2009)", "without asking.", 0},
                   {"Okupa is a pet game written from scratch in C", "for a real Nintendo DSi."}};
        for (f = 0; f < 6 * FPS; f++) {
            ok_intro_splash(top, bottom, (uint32_t)(f * 2 < OK_SPLASH_TICKS ? f * 2 : OK_SPLASH_TICKS - 1));
            if (f == 30) ok_music_sfx(&music, OK_SFX_JINGLE);
            emit(&w);
        }
    }
    /* 2. The opening film (13 s). */
    {
        words w = {"No server", {"The console is an Ethereum", "peer itself: no RPC,", "no relayer, no backend.", "", "Its mind runs on 0G.", 0},
                   {"It looks like a pet game. Underneath, the DSi talks", "to Ethereum and to 0G by itself."}};
        for (f = 0; f < 13 * FPS; f++) {
            ok_intro_film(top, bottom, (uint32_t)(f * 2 < OK_FILM_TICKS ? f * 2 : OK_FILM_TICKS - 1));
            emit(&w);
        }
    }
    /* 3. Looking after it (16 s). */
    {
        words w = {"Daily care", {"Feed it, play, pet it,", "dress it, take it outside.", "Needs go down in real time.", 0},
                   {"Every day you look after it, like a Tamagotchi.", 0}};
        ui.scene = OK_UI_HOME;
        for (f = 0; f < 16 * FPS; f++) {
            if (f == 2 * FPS) touch(35, 60);   /* Feed */
            if (f == 6 * FPS) touch(159, 60);  /* Pet */
            if (f == 10 * FPS) touch(97, 60);  /* Play */
            tick();
            game(10);
            emit(&w);
        }
    }
    /* 4. Its mind on 0G (14 s). */
    {
        words w = {"A mind on 0G", {"Model: 0GM-1.0-35B-A3B", "on 0G Compute, in a TEE.", "The DSi checks the TEE's", "signature itself (TLS 1.2,", "secp256k1 recover) before", "it acts. 6/6 verified on", "the console."},
                   {"What it does next is decided by a model on 0G.", "The reply is signed in a TEE; the DSi verifies it."}};
        for (f = 0; f < 14 * FPS; f++) {
            if (f == 1 * FPS) ok_ui_thought(&ui, "Someone touched me! Who's there?");
            if (f == 7 * FPS) ok_ui_ask(&ui, "What did you have for lunch today?");
            tick();
            game(11);
            emit(&w);
        }
        ok_ui_ask(&ui, NULL);
    }
    /* 5. A drawing, signed on the console (12 s). */
    {
        words w = {"Draw, then send", {"A 64x64 drawing goes into", "an EIP-1559 transaction,", "signed on the ARM9 with a", "key born from your", "scribbles on the stylus.", 0},
                   {"You draw something. The DSi puts it in a transaction", "and signs it with its own key."}};
        ui.scene = OK_UI_SKETCH;
        for (f = 0; f < 12 * FPS; f++) {
            if (f < 8 * FPS) {
                sketch_house(house, f, 8 * FPS - 1);
                memcpy(ui.drawing, house, sizeof house);
            }
            if (f == 9 * FPS) {
                touch(198, 140); /* Send */
                ui.drawing_sent = 1;
                ok_facts_now.tx_handed = 0;
            }
            tick();
            game(15);
            emit(&w);
        }
    }
    /* 6. How it gets there: the walk-through on the console (30 s). */
    {
        static const int steps[5] = {1, 2, 3, 4, 5};
        static const words ws[5] = {
            {"Find the nodes", {"Signed DNS list (EIP-1459),", "then discv4 over UDP.", "On the DSi, mainnet:", "24 nodes from DNS,", "322 seen via discv4.", 0}, {"No server tells it where Ethereum is: it finds peers itself.", 0}},
            {"Shake hands", {"RLPx: ECIES auth/ack,", "AES-256-CTR frames, a", "keccak MAC. On the DSi:", "18 handshakes, 7 Hellos", "from Nethermind and", "Erigon nodes on mainnet.", 0}, {"It speaks the same encrypted protocol as every Ethereum node.", 0}},
            {"Say who we are", {"Hello, then eth/69 Status:", "chain 1, genesis, fork id", "07c9462e (EIP-2124).", "It keeps no blocks", "and says so.", 0}, {"It introduces itself as a peer on mainnet.", 0}},
            {"Sign the letter", {"Type-2 transaction, zero", "value, to itself, the 64x64", "drawing in its data.", "30 ms to sign on the DSi.", "(Hash: the same code with", "a test key.)", 0}, {"The transaction is built and signed on the console.", 0}},
            {"Hand it over", {"Public nodes are full: 17", "of 25 said 'too many peers'.", "The DSi hands its tx over", "the moment a node sends", "Status, before it hangs up.", "Next: a node that lets it in.", 0}, {"Public nodes are full; the DSi hands its transaction over the moment it can.", 0}},
        };
        for (k = 0; k < 5; k++) {
            ok_tech_init(&tech);
            tech.page = OK_TECH_HOW;
            tech.step = steps[k];
            if (k == 3) {
                /* The drawing above, really signed by the same core with the
                 * test key (0x46 x 32): real bytes and hash, not the console's
                 * own key. The time is the DSi's own measurement (30 ms, the
                 * Dev checks on the console, photo IMG_5985). */
                uint8_t priv[32], pub[64], act[OK_ACT_MAX];
                size_t act_len, len;
                ok_tx tx;
                memset(priv, 0x46, 32);
                ok_keys_public(priv, pub);
                ok_act_drawing(house, act, sizeof act, &act_len);
                memset(&tx, 0, sizeof tx);
                tx.chain_id = 1;
                tx.max_priority_fee = 100000000ull;
                tx.max_fee = 2000000000ull;
                tx.gas_limit = 60000;
                ok_keys_address(pub, tx.to);
                tx.data = act;
                tx.data_len = act_len;
                ok_tx_sign(&tx, priv, ok_facts_now.tx, sizeof ok_facts_now.tx, &len, ok_facts_now.tx_hash);
                ok_facts_now.tx_len = (uint32_t)len;
                strcpy(ok_facts_now.tx_kind, "drawing");
                ok_facts_now.tx_nonce = 0, ok_facts_now.us_sign = 30000, ok_facts_now.tx_gas = 60000;
            }
            for (f = 0; f < 6 * FPS; f++) {
                tech.ticks = (uint32_t)(f * 2);
                ok_tech_render(&tech, &ok_facts_now, ok_font(), top, bottom);
                emit(&ws[k]);
            }
        }
    }
    /* 7. Seeing it in a block (12 s). */
    {
        words w = {"In a block?", {"While a node keeps it, the", "DSi asks for new headers", "and bodies, re-hashes each", "header and rebuilds the", "transactions trie. Same C", "from the Mac on mainnet:", "5 blocks, 576 txs, roots ok."},
                   {"It checks blocks itself: the trie root must match the header.", "That proves inclusion in that header. Not finality."}};
        ok_tech_init(&tech);
        tech.page = OK_TECH_HOW;
        tech.step = 6;
        for (f = 0; f < 12 * FPS; f++) {
            tech.ticks = (uint32_t)(f * 2);
            ok_tech_render(&tech, &ok_facts_now, ok_font(), top, bottom);
            emit(&w);
        }
    }
    /* 8. On the wall (10 s). */
    {
        words w = {"On the wall", {"When its own transaction", "shows up in a block, the", "drawing is hung in the", "attic. Six frames: your", "gallery on Ethereum.", 0},
                   {"When the console sees its drawing in a block,", "the picture goes up on the wall."}};
        ui.scene = OK_UI_HOME;
        ui.drawing_sent = 0;
        ui.wall_known = 0;
        ui.wall_count = 0;
        for (f = 0; f < 10; f++) tick();
        memcpy(ui.wall[0], house, sizeof house);
        ui.wall_count = 1;
        for (f = 0; f < 10 * FPS; f++) {
            tick();
            game(11);
            emit(&w);
        }
    }
    /* 9. The machine (12 s). */
    {
        words w = {"Just a DSi", {"ARM9 at 133 MHz, 16 MB RAM.", "C99, no OS. keccak,", "secp256k1, RLPx, snappy,", "the trie, TLS 1.2: all on", "the console. SELECT shows", "every byte, live."},
                   {"Everything you saw runs on the console, in C.", "Press SELECT and it shows you every step, live."}};
        ok_tech_init(&tech);
        tech.page = OK_TECH_HOW;
        tech.step = 8;
        for (f = 0; f < 12 * FPS; f++) {
            tech.ticks = (uint32_t)(f * 2);
            ok_tech_render(&tech, &ok_facts_now, ok_font(), top, bottom);
            emit(&w);
        }
    }
    /* 10. Close (8 s). */
    {
        words w = {"Okupa", {"Ethereum: no RPC, nothing", "of ours to switch off.", "0G: a mind with a body,", "verified on the device.", "", "github.com/victxx/okupa", 0},
                   {"Okupa. Just a DSi and a Wi-Fi.", 0}};
        for (f = 0; f < 8 * FPS; f++) {
            tick();
            game(11);
            emit(&w);
        }
    }
    wav_header(samples);
    fclose(wav);
    fprintf(stderr, "okupa-video: %u frames, %u samples\n", frames_out, samples);
    return 0;
}
