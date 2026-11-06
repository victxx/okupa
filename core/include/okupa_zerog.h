#ifndef OKUPA_ZEROG_H
#define OKUPA_ZEROG_H

#include "okupa.h"

/* Checking a 0G Compute answer on the console. A TEE provider signs, with
 * EIP-191 personal_sign, the text sha256hex(request):sha256hex(response). We
 * rebuild that text from the exact bytes we sent and received (so a provider
 * cannot sign something else) and recover the signer's address. */
#define OK_ZEROG_TEXT 129 /* 64 + ':' + 64 */

void ok_zerog_text(const uint8_t *req, size_t req_len, const uint8_t *resp, size_t resp_len, char text[OK_ZEROG_TEXT + 1]);
/* Signer of an EIP-191 message from a 65-byte r || s || v signature (v 0/1 or 27/28). */
ok_result ok_personal_recover(const void *msg, size_t n, const uint8_t sig[65], uint8_t address[20]);
/* OK_OK only if text is ours (matches req and resp) and was signed by signer.
 * sig_hex is the provider's "0x..." signature. */
ok_result ok_zerog_verify(const uint8_t *req, size_t req_len, const uint8_t *resp, size_t resp_len,
                          const char *text, size_t text_len, const char *sig_hex, const uint8_t signer[20]);

/* EIP-191 personal_sign: r || s || v with v = 27/28, as wallets produce it. */
ok_result ok_personal_sign(const void *msg, size_t n, const uint8_t priv[32], uint8_t sig[65]);

/* The 0G Direct bearer token, signed by the console's own key: "app-sk-" +
 * base64(token JSON + "|" + 0x signature), where the signature is personal_sign
 * over keccak256(token JSON). token_id 255 = ephemeral (expires within 24 h). */
typedef struct {
    const char *provider;     /* as the contract lists it, e.g. checksummed 0x... */
    uint64_t timestamp_ms, expires_ms;
    uint8_t nonce[16];
    uint32_t generation;      /* from the provider sub-account */
    uint8_t token_id;
} ok_zerog_session;

ok_result ok_zerog_token(const uint8_t priv[32], const ok_zerog_session *s, char *out, size_t cap, size_t *len);

#endif
