#ifndef OKUPA_JSONRPC_H
#define OKUPA_JSONRPC_H

#include "okupa.h"
#include "okupa_json.h"
#include "okupa_think.h"

/* One JSON-RPC call over the platform's HTTPS round trip (keep-alive): body
 * POSTed to host "/", the answer read into buf (cap bytes), *result its
 * "result" member. 0 and a reason in error if it failed or the node
 * answered with an error object. Reading only: nothing is ever sent this way. */
int ok_jsonrpc_call(ok_think_fetch_fn fetch, void *ctx, const char *host, const char *body, size_t body_len, char *buf,
                    size_t cap, ok_json *result, char *error, size_t error_cap);

#endif
