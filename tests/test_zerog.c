#include "okupa_zerog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
}

/* Signed by eth-account 0.13.7 (sign_message / encode_defunct), key 0x5a * 32,
 * over the text a 0G TEE provider signs for this request and response. */
static const char REQ[] = "{\"model\":\"glm-5.3\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}";
static const char RESP[] = "{\"id\":\"chat-1\",\"choices\":[{\"message\":{\"content\":\"{\\\"activity\\\":\\\"wave\\\"}\"}}]}";
static const char TEXT[] =
    "d2338087664513ccfaa68fae147cd0b01767b3b9ac1e243cb4ec2a6562834557:af009fb4e8ae2f50c6e571598fe055a2bab6f14137d17c4a70778fc0bc8a4535";
static const char SIG[] =
    "0x0de6f23a4cea62ab3818f2c42af070e09c3bf0d45627942315b846ca03974409713c1f62e3f7398820ba4ca3081002aa390751d1fe9f41dcfe243120c99a79741b";

int main(void)
{
    const uint8_t *req = (const uint8_t *)REQ, *resp = (const uint8_t *)RESP;
    uint8_t signer[20], other[20], sig[65], who[20];
    char text[OK_ZEROG_TEXT + 1], bad[sizeof SIG];

    hex("a8049bB68181799124F98E467DD749e120abFA64", signer, 20);
    ok_zerog_text(req, strlen(REQ), resp, strlen(RESP), text);
    assert(!strcmp(text, TEXT));

    hex(SIG + 2, sig, 65);
    assert(ok_personal_recover(TEXT, strlen(TEXT), sig, who) == OK_OK && !memcmp(who, signer, 20));
    sig[64] -= 27; /* v as 0/1 works too */
    assert(ok_personal_recover(TEXT, strlen(TEXT), sig, who) == OK_OK && !memcmp(who, signer, 20));

    assert(ok_zerog_verify(req, strlen(REQ), resp, strlen(RESP), TEXT, strlen(TEXT), SIG, signer) == OK_OK);
    assert(ok_zerog_verify(req, strlen(REQ), resp, strlen(RESP), TEXT, strlen(TEXT), SIG + 2, signer) == OK_OK);

    /* A different signer, a different answer, or a signature over another text. */
    memcpy(other, signer, 20);
    other[0] ^= 1;
    assert(ok_zerog_verify(req, strlen(REQ), resp, strlen(RESP), TEXT, strlen(TEXT), SIG, other) == OK_CRYPTO);
    assert(ok_zerog_verify(req, strlen(REQ), resp, strlen(RESP) - 1, TEXT, strlen(TEXT), SIG, signer) == OK_FORMAT);
    memcpy(bad, SIG, sizeof bad);
    bad[10] = bad[10] == '0' ? '1' : '0';
    assert(ok_zerog_verify(req, strlen(REQ), resp, strlen(RESP), TEXT, strlen(TEXT), bad, signer) != OK_OK);
    assert(ok_zerog_verify(req, strlen(REQ), resp, strlen(RESP), TEXT, strlen(TEXT), "0x1234", signer) == OK_FORMAT);
    puts("zerog: TEE response text rebuilt from our bytes; EIP-191 signer matches eth-account");
    return 0;
}
