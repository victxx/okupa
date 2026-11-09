#include "okupa_balance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A JSON-RPC node in a box: answers by the method in the request. */
static int calls, fail_net, chunked, error_object;
static int fetch(void *ctx, const char *host, const char *head, size_t head_len, const char *body, size_t body_len,
                 char *resp, size_t cap, size_t *got)
{
    const char *answer;
    (void)ctx, (void)head_len, (void)body_len;
    calls++;
    assert(!strcmp(host, "ethereum-rpc.publicnode.com") && !strncmp(head, "POST / HTTP/1.1", 15));
    if (fail_net) return -3;
    if (strstr(body, "eth_blockNumber"))
        answer = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x1474f5e\"}";
    else if (strstr(body, "eth_getTransactionCount"))
        answer = "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":\"0x1\"}";
    else if (error_object)
        answer = "{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}";
    else {
        assert(strstr(body, "\"params\":[\"0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F\",\"0x1474f5e\"]"));
        answer = "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":\"0x17d43b2e0e00\"}"; /* 0.0000262 ETH */
    }
    if (chunked)
        *got = (size_t)snprintf(resp, cap, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n%s\r\n0\r\n\r\n",
                                strlen(answer), answer);
    else
        *got = (size_t)snprintf(resp, cap, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s", strlen(answer), answer);
    return 0;
}

int main(void)
{
    static char buf[4096];
    char eth[32];
    ok_balance b;
    const char *addr = "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F", *host = "ethereum-rpc.publicnode.com";

    /* Wei to ether: six decimals, always rounded down. */
    assert(ok_wei_to_eth("0x0", eth, sizeof eth) == OK_OK && !strcmp(eth, "0.000000 ETH"));
    assert(ok_wei_to_eth("0xde0b6b3a7640000", eth, sizeof eth) == OK_OK && !strcmp(eth, "1.000000 ETH"));
    assert(ok_wei_to_eth("0x2ff70ee5de97c9ab42b", eth, sizeof eth) == OK_OK && !strcmp(eth, "14156.790249 ETH"));
    assert(ok_wei_to_eth("0xDE0B6B3A763FFFF", eth, sizeof eth) == OK_OK && !strcmp(eth, "0.999999 ETH")); /* 1 ETH - 1 wei */
    assert(ok_wei_to_eth("0x3b9aca00", eth, sizeof eth) == OK_OK && !strcmp(eth, "0.000000 ETH")); /* 1 gwei */
    assert(ok_wei_to_eth("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", eth, sizeof eth) == OK_RANGE);
    assert(ok_wei_to_eth("0x", eth, sizeof eth) == OK_FORMAT && ok_wei_to_eth("12", eth, sizeof eth) == OK_FORMAT);
    assert(ok_wei_to_eth("0x12g", eth, sizeof eth) == OK_FORMAT);
    assert(ok_wei_to_eth("0x10000000000000000000000000000000000000000000000000000000000000000", eth, sizeof eth) == OK_FORMAT);

    /* The block number, then the balance at that block, from the node. */
    assert(ok_balance_ask(host, addr, fetch, NULL, buf, sizeof buf, &b) == OK_OK && calls == 3);
    assert(!b.error[0] && b.block == 0x1474f5e && !strcmp(b.eth, "0.000026 ETH") && b.low);
    assert(b.have_nonce && b.nonce == 1); /* and how many it has sent */
    chunked = 1;
    assert(ok_balance_ask(host, addr, fetch, NULL, buf, sizeof buf, &b) == OK_OK && !strcmp(b.eth, "0.000026 ETH"));
    chunked = 0;
    /* A node that errs, or no network: no balance, and why. */
    error_object = 1;
    assert(ok_balance_ask(host, addr, fetch, NULL, buf, sizeof buf, &b) == OK_OK && !b.eth[0] &&
           !strcmp(b.error, "Internal error"));
    error_object = 0;
    fail_net = 1;
    assert(ok_balance_ask(host, addr, fetch, NULL, buf, sizeof buf, &b) == OK_OK && !b.eth[0] && strstr(b.error, "net -3"));
    fail_net = 0;
    assert(ok_balance_ask(host, "0x1234", fetch, NULL, buf, sizeof buf, &b) == OK_ARGUMENT && b.error[0]);
    assert(ok_balance_ask(host, "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4G", fetch, NULL, buf, sizeof buf, &b) == OK_ARGUMENT);
    puts("balance: wei to ether rounded down; block then balance from a JSON-RPC node; errors told");
    return 0;
}
