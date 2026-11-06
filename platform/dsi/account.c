#include "account.h"
#include "okupa_keccak.h"
#include "okupa_keys.h"
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SAMPLES 480 /* about eight seconds of scribbling */

static const char *key_path;
static ok_keccak pool;
static int samples, last_x = -1, last_y = -1, started;

static void show_address(const uint8_t priv[32], ok_ui *u)
{
    uint8_t pub[64], addr[20];
    if (ok_keys_public(priv, pub) != OK_OK) return;
    ok_keys_address(pub, addr);
    ok_keys_checksum(addr, u->wallet_address);
    u->wallet_ready = 1;
}

void ok_dsi_account_open(const char *path, ok_ui *u)
{
    char text[80] = {0};
    uint8_t priv[32];
    unsigned i, v;
    FILE *f;
    key_path = path;
    if (u->wallet_ready || !path || !(f = fopen(path, "r"))) return;
    if (fgets(text, sizeof text, f)) {
        for (i = 0; i < 32 && sscanf(text + 2 * i, "%2x", &v) == 1; i++) priv[i] = (uint8_t)v;
        if (i == 32) show_address(priv, u);
    }
    fclose(f);
    ok_wipe(priv, sizeof priv);
    ok_wipe(text, sizeof text);
}

/* Each sample adds where the stylus is and exactly when (a 33 MHz timer):
 * human movement is the randomness. Only moving samples count. */
void ok_dsi_account_scribble(int x, int y, ok_ui *u)
{
    uint32_t t = cpuGetTiming();
    time_t now = time(NULL);
    uint8_t priv[32], pub[64];
    uint32_t round = 0;
    FILE *f;
    int i;
    if (u->wallet_ready) return;
    if (!started) {
        ok_keccak_init(&pool);
        ok_keccak_update(&pool, "okupa account", 13);
        ok_keccak_update(&pool, &now, sizeof now);
        started = 1;
    }
    ok_keccak_update(&pool, &x, sizeof x);
    ok_keccak_update(&pool, &y, sizeof y);
    ok_keccak_update(&pool, &t, sizeof t);
    if (x != last_x || y != last_y) samples++;
    last_x = x;
    last_y = y;
    u->wallet_progress = samples * 100 / SAMPLES;
    if (samples < SAMPLES) return;
    if (!key_path) {
        snprintf(u->wallet_address, sizeof u->wallet_address, "Needs an SD card");
        return;
    }
    do {
        ok_keccak k = pool;
        ok_keccak_update(&k, &round, sizeof round);
        ok_keccak_final(&k, priv);
        round++;
    } while (ok_keys_public(priv, pub) != OK_OK);
    if (!(f = fopen(key_path, "w"))) {
        snprintf(u->wallet_address, sizeof u->wallet_address, "Could not write the SD");
        ok_wipe(priv, sizeof priv);
        return;
    }
    for (i = 0; i < 32; i++) fprintf(f, "%02x", priv[i]);
    fprintf(f, "\n");
    fclose(f);
    show_address(priv, u);
    ok_wipe(priv, sizeof priv);
    ok_wipe(&pool, sizeof pool);
}
