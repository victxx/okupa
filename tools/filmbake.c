/* Bakes the opening film (okupa_film.h) with the game's own code: each frame
 * drawn as the console would draw it live (the sky in software too), without
 * the fades, cut between the cinema bars, reduced to one 256-colour palette
 * (median cut over every frame) and packed.
 *   build/okupa-filmbake <out.okf> [preview dir]
 * With a preview dir, a few frames are written back as PNGs, decoded from the
 * file, to look at what the console will show. */
#include "okupa_film.h"
#include "okupa_intro.h"
#include "png.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define FIRST 20
#define ROWS 152
#define TICKS 2

static uint16_t top[W * 192], bottom[W * 192];
static uint32_t hist[32768];
static uint8_t lut[32768];

typedef struct {
    int lo[3], hi[3];
    int first, count; /* into colours[] */
    uint64_t weight;
    double error; /* sum of weight x squared distance to the box's mean */
} box;
static uint16_t colours[32768];
static int ncolours, axis;

static int chan(uint16_t c, int a) { return (c >> (5 * a)) & 31; }
static int by_axis(const void *a, const void *b) { return chan(*(const uint16_t *)a, axis) - chan(*(const uint16_t *)b, axis); }

static void fit(box *b)
{
    double sum[3] = {0, 0, 0}, sq = 0;
    int a, i;
    b->weight = 0;
    for (a = 0; a < 3; a++) b->lo[a] = 31, b->hi[a] = 0;
    for (i = b->first; i < b->first + b->count; i++) {
        double w = sqrt((double)hist[colours[i]]); /* tempered: a huge flat area must not take every entry */
        for (a = 0; a < 3; a++) {
            int v = chan(colours[i], a);
            if (v < b->lo[a]) b->lo[a] = v;
            if (v > b->hi[a]) b->hi[a] = v;
            sum[a] += w * v;
            sq += w * v * v;
        }
        b->weight += hist[colours[i]];
    }
    {
        double tw = 0;
        for (i = b->first; i < b->first + b->count; i++) tw += sqrt((double)hist[colours[i]]);
        b->error = tw > 0 ? sq - (sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]) / tw : 0;
    }
}

/* Median cut: the box with the largest squared error is split at its weighted
 * median along its widest side, until there are 256. */
static int median_cut(uint16_t palette[256])
{
    static box boxes[256];
    int n = 1, i, k;
    boxes[0].first = 0, boxes[0].count = ncolours;
    fit(&boxes[0]);
    while (n < 256) {
        int best = -1, a, w, wa = 0;
        uint64_t half, run = 0;
        double score = 0;
        for (i = 0; i < n; i++)
            if (boxes[i].count >= 2 && boxes[i].error > score) score = boxes[i].error, best = i;
        if (best < 0) break;
        for (a = 0, w = -1; a < 3; a++)
            if (boxes[best].hi[a] - boxes[best].lo[a] > w) w = boxes[best].hi[a] - boxes[best].lo[a], wa = a;
        axis = wa;
        qsort(colours + boxes[best].first, (size_t)boxes[best].count, sizeof colours[0], by_axis);
        half = boxes[best].weight / 2;
        for (k = 0; k < boxes[best].count - 1; k++) {
            run += hist[colours[boxes[best].first + k]];
            if (run >= half) break;
        }
        if (k > boxes[best].count - 2) k = boxes[best].count - 2; /* never an empty half */
        boxes[n].first = boxes[best].first + k + 1;
        boxes[n].count = boxes[best].count - k - 1;
        boxes[best].count = k + 1;
        fit(&boxes[best]);
        fit(&boxes[n]);
        n++;
    }
    if (getenv("FILMBAKE_DEBUG"))
        for (i = 0; i < 12; i++)
            fprintf(stderr, "box %d first %d count %d weight %llu error %.0f lo %d,%d,%d hi %d,%d,%d\n", i, boxes[i].first,
                    boxes[i].count, (unsigned long long)boxes[i].weight, boxes[i].error, boxes[i].lo[0], boxes[i].lo[1],
                    boxes[i].lo[2], boxes[i].hi[0], boxes[i].hi[1], boxes[i].hi[2]);
    for (i = 0; i < 256; i++) {
        uint64_t s[3] = {0, 0, 0}, wsum = 0;
        int a;
        if (i >= n) {
            palette[i] = 0x8000;
            continue;
        }
        for (k = boxes[i].first; k < boxes[i].first + boxes[i].count; k++)
            for (a = 0, wsum += hist[colours[k]]; a < 3; a++) s[a] += (uint64_t)chan(colours[k], a) * hist[colours[k]];
        palette[i] = (uint16_t)(0x8000 | (s[0] / wsum) | (s[1] / wsum) << 5 | (s[2] / wsum) << 10);
    }
    return n;
}

static void put16(FILE *f, unsigned v) { fputc(v & 255, f), fputc(v >> 8 & 255, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xffff), put16(f, v >> 16); }

int main(int argc, char **argv)
{
    static uint8_t (*frames)[W * ROWS];
    static uint8_t packed[2][W * ROWS * 2], diff[W * ROWS], *data;
    static uint32_t offsets[1024];
    static uint16_t palette[256];
    int nframes = OK_FILM_TICKS / TICKS, f, i, k, used;
    size_t at = 0;
    FILE *out;
    if (argc < 2) return fprintf(stderr, "usage: okupa-filmbake <out.okf> [preview dir]\n"), 2;
    frames = calloc((size_t)nframes, sizeof *frames);
    data = malloc((size_t)nframes * W * ROWS);
    if (!frames || !data) return 1;
    /* Every frame, as the console draws it (counting the colours). */
    {
        static uint16_t (*raw)[W * ROWS];
        raw = calloc((size_t)nframes, sizeof *raw);
        if (!raw) return 1;
        for (f = 0; f < nframes; f++) {
            ok_intro_film_scene(top, (uint32_t)(f * TICKS));
            for (i = 0; i < W * ROWS; i++) {
                uint16_t c = top[FIRST * W + i] & 0x7fff;
                raw[f][i] = c;
                hist[c]++;
            }
        }
        for (i = 0; i < 32768; i++)
            if (hist[i]) colours[ncolours++] = (uint16_t)i;
        used = median_cut(palette);
        for (i = 0; i < 32768; i++) { /* every colour to its nearest in the palette */
            int best = 0, bd = 1 << 30;
            for (k = 0; k < used; k++) {
                int dr = chan((uint16_t)i, 0) - chan(palette[k], 0), dg = chan((uint16_t)i, 1) - chan(palette[k], 1),
                    db = chan((uint16_t)i, 2) - chan(palette[k], 2), d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
                if (d < bd) bd = d, best = k;
            }
            lut[i] = (uint8_t)best;
        }
        for (f = 0; f < nframes; f++)
            for (i = 0; i < W * ROWS; i++) frames[f][i] = lut[raw[f][i]];
        free(raw);
    }
    /* Each frame whole or as a difference, whichever packs smaller. */
    for (f = 0; f < nframes; f++) {
        size_t whole = ok_film_lz_pack(frames[f], W * ROWS, packed[0], sizeof packed[0]), delta = 0;
        if (f) {
            for (i = 0; i < W * ROWS; i++) diff[i] = frames[f][i] ^ frames[f - 1][i];
            delta = ok_film_lz_pack(diff, W * ROWS, packed[1], sizeof packed[1]);
        }
        offsets[f] = (uint32_t)at;
        if (!whole) return fprintf(stderr, "frame %d did not pack\n", f), 1;
        if (f && delta && delta < whole) {
            data[at++] = 1;
            memcpy(data + at, packed[1], delta), at += delta;
        } else {
            data[at++] = 0;
            memcpy(data + at, packed[0], whole), at += whole;
        }
    }
    offsets[nframes] = (uint32_t)at;
    if (!(out = fopen(argv[1], "wb"))) return perror(argv[1]), 1;
    fwrite("OKFM\1", 1, 5, out);
    put16(out, W), put16(out, ROWS), put16(out, (unsigned)nframes);
    fputc(TICKS, out), fputc(FIRST, out);
    for (i = 0; i < 256; i++) put16(out, palette[i]);
    for (f = 0; f <= nframes; f++) put32(out, offsets[f]);
    fwrite(data, 1, at, out);
    fclose(out);
    printf("okupa-filmbake: %d frames, %d colours from %d, %lu KB\n", nframes, used, ncolours,
           (unsigned long)(OK_FILM_HEADER + 4 * (nframes + 1) + at) / 1024);
    /* Decoded back from the file, for a look. */
    if (argc > 2) {
        static uint8_t file[8 << 20], px[W * ROWS];
        static uint16_t shown[W * ROWS], pal[256];
        ok_film film;
        size_t n;
        char path[512];
        if (!(out = fopen(argv[1], "rb"))) return 1;
        n = fread(file, 1, sizeof file, out);
        fclose(out);
        if (ok_film_open(&film, file, n) != OK_OK) return fprintf(stderr, "the file does not open\n"), 1;
        for (f = 0; f < film.frames; f++) {
            if (ok_film_frame(&film, f, px) != OK_OK || memcmp(px, frames[f], sizeof px))
                return fprintf(stderr, "frame %d does not decode back\n", f), 1;
            if (f % 60 == 30) {
                ok_film_palette(&film, 0x8000, 0, pal);
                for (i = 0; i < W * ROWS; i++) shown[i] = pal[px[i]];
                snprintf(path, sizeof path, "%s/film-%03d.png", argv[2], f);
                png_write(path, shown, W, ROWS, 1);
            }
        }
        printf("okupa-filmbake: every frame decodes back exactly\n");
    }
    (void)bottom;
    return 0;
}
