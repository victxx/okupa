/* Finds Sepolia nodes the way the console will: the EIP-1459 tree in DNS,
 * checked with the core (signed root, hash-named children, signed ENRs).
 * Only the UDP socket lives here. Prints one enode:// line per Sepolia node.
 *   build/okupa-dnswalk [resolver ip] [nodes] */
#include "okupa_dns.h"
#include "okupa_dnsdisc.h"
#include "okupa_eth.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TREE "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net"
#define MAX_QUERIES 400

static int fd;
static struct sockaddr_in resolver;
static uint16_t next_id = 0x4f4b;
static unsigned queries;

/* One TXT lookup, with a retry; 0 if there is no usable answer. */
static int lookup(const char *name, char *txt, size_t cap, size_t *len)
{
    uint8_t q[OK_DNS_MAX], r[OK_DNS_MAX];
    size_t qn;
    int tries;
    for (tries = 0; tries < 2; tries++) {
        uint16_t id = next_id++;
        ssize_t n;
        if (++queries > MAX_QUERIES || ok_dns_query(name, id, q, sizeof q, &qn) != OK_OK) return 0;
        sendto(fd, q, qn, 0, (struct sockaddr *)&resolver, sizeof resolver);
        while ((n = recv(fd, r, sizeof r, 0)) > 0) {
            ok_result res = ok_dns_txt(r, (size_t)n, id, txt, cap, len);
            if (res == OK_OK) return 1;
            if (res == OK_RANGE) return 0;
        }
    }
    return 0;
}

static int lookup_cb(void *ctx, const char *name, char *txt, size_t cap, size_t *len)
{
    (void)ctx;
    return lookup(name, txt, cap, len);
}

static void found_cb(void *ctx, const uint8_t pub[64], const uint8_t ip[4], uint16_t tcp)
{
    int i;
    (void)ctx;
    printf("enode://");
    for (i = 0; i < 64; i++) printf("%02x", pub[i]);
    printf("@%u.%u.%u.%u:%u\n", ip[0], ip[1], ip[2], ip[3], tcp);
}

int main(int argc, char **argv)
{
    static ok_dnsdisc_walk w;
    uint8_t fork[4];
    uint64_t next;
    struct timeval tv = {3, 0};
    int found, limit = argc > 2 ? atoi(argv[2]) : 20;

    memset(&resolver, 0, sizeof resolver);
    resolver.sin_family = AF_INET;
    resolver.sin_port = htons(53);
    if (inet_pton(AF_INET, argc > 1 ? argv[1] : "1.1.1.1", &resolver.sin_addr) != 1) return 2;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return 2;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ok_forkid(&ok_sepolia, UINT64_MAX, (uint64_t)time(NULL), fork, &next);
    w.lookup = lookup_cb;
    w.found = found_cb;
    found = ok_dnsdisc_walk_tree(&w, TREE, fork, limit);
    fprintf(stderr, "okupa-dnswalk: root seq %llu, %d Sepolia nodes from %u records, %u queries, %u refused\n",
            (unsigned long long)w.seq, found, w.records, queries, w.refused);
    close(fd);
    return found ? 0 : 1;
}
