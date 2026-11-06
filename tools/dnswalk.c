/* Finds Sepolia nodes the way the console will: the EIP-1459 tree in DNS,
 * checked with the core (signed root, hash-named children, signed ENRs).
 * Only the UDP socket lives here. Prints one enode:// line per Sepolia node.
 *   build/okupa-dnswalk [resolver ip] [nodes] */
#include "okupa_dns.h"
#include "okupa_dnsdisc.h"
#include "okupa_enr.h"
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
#define STACK 256
#define MAX_QUERIES 400

static int fd;
static struct sockaddr_in resolver;
static uint16_t next_id = 0x4f4b;
static unsigned queries, refused;

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

int main(int argc, char **argv)
{
    static char stack[STACK][OK_DNSDISC_HASH + 1];
    char domain[128], name[200], txt[400];
    uint8_t key[33], fork[4];
    uint64_t next;
    struct timeval tv = {3, 0};
    ok_dnsdisc_root root;
    size_t len, pos;
    int top = 0, found = 0, seen = 0, limit = argc > 2 ? atoi(argv[2]) : 20;

    memset(&resolver, 0, sizeof resolver);
    resolver.sin_family = AF_INET;
    resolver.sin_port = htons(53);
    if (inet_pton(AF_INET, argc > 1 ? argv[1] : "1.1.1.1", &resolver.sin_addr) != 1) return 2;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return 2;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ok_forkid(&ok_sepolia, UINT64_MAX, (uint64_t)time(NULL), fork, &next);

    if (ok_dnsdisc_url(TREE, key, domain, sizeof domain) != OK_OK) return 2;
    if (!lookup(domain, txt, sizeof txt, &len) || ok_dnsdisc_root_parse(txt, len, key, &root) != OK_OK) {
        fprintf(stderr, "okupa-dnswalk: no verified root for %s\n", domain);
        return 1;
    }
    fprintf(stderr, "okupa-dnswalk: root seq %llu verified, nodes under %s\n", (unsigned long long)root.seq, root.enr_root);
    strcpy(stack[top++], root.enr_root);
    while (top && found < limit) {
        char hash[OK_DNSDISC_HASH + 1];
        strcpy(hash, stack[--top]);
        snprintf(name, sizeof name, "%s.%s", hash, domain);
        if (!lookup(name, txt, sizeof txt, &len)) continue;
        if (!ok_dnsdisc_matches(hash, OK_DNSDISC_HASH, txt, len)) {
            refused++; /* the resolver gave us something else */
            continue;
        }
        if (ok_dnsdisc_kind(txt, len) == OK_DNSDISC_BRANCH) {
            char children[STACK][OK_DNSDISC_HASH + 1];
            int k = 0;
            pos = 0;
            while (k < STACK && ok_dnsdisc_branch_next(txt, len, &pos, children[k]) == OK_OK) k++;
            while (k-- && top < STACK) strcpy(stack[top++], children[k]); /* first child on top */
        } else if (ok_dnsdisc_kind(txt, len) == OK_DNSDISC_ENR) {
            ok_enr e;
            int i;
            seen++;
            if (ok_enr_parse_text(txt, len, &e) != OK_OK) {
                refused++;
                continue;
            }
            if (!e.has_eth || memcmp(e.fork_hash, fork, 4) || !e.has_ip || !e.tcp) continue;
            printf("enode://");
            for (i = 0; i < 64; i++) printf("%02x", e.pub[i]);
            printf("@%u.%u.%u.%u:%u\n", e.ip[0], e.ip[1], e.ip[2], e.ip[3], e.tcp);
            found++;
        }
    }
    fprintf(stderr, "okupa-dnswalk: %d Sepolia nodes from %d records, %u queries, %u refused\n", found, seen, queries, refused);
    close(fd);
    return found ? 0 : 1;
}
