/* Signs an Okupa act with the console's own core (the code the DSi runs), so the web
 * app's visit view can be checked against real DSi-format bytes on a local chain.
 *   web/tools/dsi-act <keyfile> <chain_id> <nonce> hello|drawing  ->  raw tx hex on stdout
 * The drawing is a fixed test pattern (a ring). Lab tool; never part of the game. */
#include "okupa_act.h"
#include "okupa_keys.h"
#include "okupa_tx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    if (strlen(s) < 2 * n) return 0;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static uint8_t data[OK_ACT_MAX], raw[1024], px[OK_DRAWING_BYTES];
    uint8_t priv[32], pub[64], addr[20], hash[32];
    char line[128] = {0};
    size_t n, len, i;
    ok_tx tx;
    FILE *f;
    unsigned x, y;
    if (argc != 5) { fprintf(stderr, "usage: dsi-act <keyfile> <chain_id> <nonce> hello|drawing\n"); return 2; }
    if (!(f = fopen(argv[1], "r")) || !fgets(line, sizeof line, f)) { perror(argv[1]); return 1; }
    fclose(f);
    if (!unhex(line[1] == 'x' ? line + 2 : line, priv, 32)) { fprintf(stderr, "key: 64 hex digits\n"); return 1; }
    if (!strcmp(argv[4], "hello")) {
        if (ok_act_hello(data, sizeof data, &n) != OK_OK) return 1;
    } else {
        for (y = 0; y < OK_DRAWING_SIDE; y++)
            for (x = 0; x < OK_DRAWING_SIDE; x++) {
                int dx = (int)x - 32, dy = (int)y - 32, d = dx * dx + dy * dy;
                ok_drawing_set(px, x, y, d > 18 * 18 && d < 24 * 24);
            }
        if (ok_act_drawing(px, data, sizeof data, &n) != OK_OK) return 1;
    }
    if (ok_keys_public(priv, pub) != OK_OK) return 1;
    ok_keys_address(pub, addr);
    memset(&tx, 0, sizeof tx);
    tx.chain_id = strtoull(argv[2], NULL, 10);
    tx.nonce = strtoull(argv[3], NULL, 10);
    tx.max_priority_fee = 100000000ull;
    tx.max_fee = 20000000000ull;
    tx.gas_limit = 60000;
    memcpy(tx.to, addr, 20); /* acts of one's own go to one's own account */
    tx.data = data;
    tx.data_len = n;
    if (ok_tx_sign(&tx, priv, raw, sizeof raw, &len, hash) != OK_OK) return 1;
    ok_wipe(priv, sizeof priv);
    for (i = 0; i < len; i++) printf("%02x", raw[i]);
    printf("\n");
    return 0;
}
