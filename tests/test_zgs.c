#include "okupa_json.h"
#include "okupa_zgs.h"
#include "zgs_vectors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const uint8_t *p, size_t n, char *out)
{
    size_t i;
    for (i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

int main(void)
{
    static uint8_t data[OK_ZGS_MAX + 1], call[2048];
    static char text[4096], body[400000];
    static const uint8_t submitter[20] = {0x80, 0x00, 0xfe, 0xb0, 0x52, 0xcf, 0x38, 0xe5, 0xba, 0x92,
                                          0x54, 0xf1, 0xd9, 0xd7, 0xe4, 0x0b, 0x50, 0x98, 0x35, 0x2d};
    ok_zgs_file f;
    ok_json root, v;
    size_t i, n;
    int k, j;

    for (i = 0; i < sizeof data; i++) data[i] = (uint8_t)((i * 31 + 7) & 255);
    /* Roots, nodes and calldata exactly as the official SDK makes them. */
    for (k = 0; k < (int)(sizeof zgs_cases / sizeof zgs_cases[0]); k++) {
        assert(ok_zgs_file_hash(data, zgs_cases[k].size, &f) == OK_OK);
        hex(f.root, 32, text);
        if (strcmp(text, zgs_cases[k].root)) printf("size %u: root %s, SDK %s\n", zgs_cases[k].size, text, zgs_cases[k].root);
        assert(!strcmp(text, zgs_cases[k].root));
        assert(f.node_count == zgs_cases[k].nodes);
        for (j = 0; j < f.node_count; j++) {
            hex(f.node_root[j], 32, text);
            assert(f.node_height[j] == zgs_cases[k].height[j] && !strcmp(text, zgs_cases[k].node_root[j]));
        }
        assert(ok_zgs_submit_calldata(&f, submitter, call, sizeof call, &n) == OK_OK);
        hex(call, n, text);
        assert(!strcmp(text, zgs_cases[k].calldata));
    }

    /* The fee: one sector a chunk of each node. 37 chunks pad to 40 (32 + 8). */
    assert(ok_zgs_file_hash(data, 37 * 256, &f) == OK_OK && f.padded_chunks == 40 && ok_zgs_fee(&f, 1000) == 40000);

    /* The upload body is JSON, with the data chunks (the last padded) and the size. */
    assert(ok_zgs_file_hash(data, 5000, &f) == OK_OK);
    assert(ok_zgs_upload_body(&f, data, 77, body, sizeof body, &n) == OK_OK);
    assert(ok_json_parse(body, n, &root) == OK_OK && ok_json_member(&root, "method", &v) == OK_OK);
    assert(strstr(body, "\"fileSize\":5000}],77]") && strstr(body, "\"index\":0,\"proof\":{\"lemma\":[\"0x"));
    assert(strstr(body, "\"data\":\"ByZFZIOi")); /* bytes 7, 38, 69, 100, 131, 162 */
    {
        const char *d = strstr(body, "\"data\":\"") + 8;
        assert(strchr(d, '"') - d == (5120 + 2) / 3 * 4); /* 20 chunks of 256 */
    }

    /* Out of range: empty, or more than one segment. */
    assert(ok_zgs_file_hash(data, 0, &f) == OK_ARGUMENT);
    assert(ok_zgs_file_hash(data, OK_ZGS_MAX + 1, &f) == OK_ARGUMENT);
    puts("zgs: roots, nodes and submit() calldata as the 0G Storage SDK makes them; fee; upload body");
    return 0;
}
