/* Finds Sepolia nodes through discovery v4, the network's own peer exchange,
 * with the core's crawler; only the UDP socket and the clock live here.
 * Prints one enode:// per node found.
 *   build/okupa-crawl [seconds] [--mainnet seeds.txt]   (seeds: enode:// lines) */
#include "okupa_discv4.h"
#include "okupa_eth.h"
#include "okupa_keys.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int fd;

static int udp_send(void *ctx, const ok_v4_endpoint *to, const uint8_t *packet, size_t n)
{
    struct sockaddr_in a;
    (void)ctx;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(to->udp);
    memcpy(&a.sin_addr.s_addr, to->ip, 4);
    return (int)sendto(fd, packet, n, 0, (struct sockaddr *)&a, sizeof a);
}

static int udp_recv(void *ctx, uint8_t *buf, size_t cap, ok_v4_endpoint *from)
{
    struct sockaddr_in a;
    socklen_t alen = sizeof a;
    ssize_t n = recvfrom(fd, buf, cap, 0, (struct sockaddr *)&a, &alen);
    (void)ctx;
    if (n <= 0) return 0;
    memcpy(from->ip, &a.sin_addr.s_addr, 4);
    from->udp = ntohs(a.sin_port);
    from->tcp = 0;
    return (int)n;
}

static void found(void *ctx, const ok_enr *e)
{
    int i;
    (void)ctx;
    printf("enode://");
    for (i = 0; i < 64; i++) printf("%02x", e->pub[i]);
    printf("@%u.%u.%u.%u:%u\n", e->ip[0], e->ip[1], e->ip[2], e->ip[3], e->tcp);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    static ok_v4_crawl c;
    uint8_t priv[32], pub[64], fork[4];
    uint64_t next;
    time_t start = time(NULL), now;
    int seconds = argc > 1 ? atoi(argv[1]) : 60, i;
    FILE *f = fopen("/dev/urandom", "rb");
    struct sockaddr_in any;

    do {
        if (!f || fread(priv, 1, 32, f) != 32) return 2;
    } while (ok_keys_public(priv, pub) != OK_OK);
    fclose(f);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&any, 0, sizeof any);
    any.sin_family = AF_INET;
    if (fd < 0 || bind(fd, (struct sockaddr *)&any, sizeof any)) return 2;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    ok_forkid(argc > 3 && !strcmp(argv[2], "--mainnet") ? &ok_mainnet : &ok_sepolia, UINT64_MAX, (uint64_t)start, fork, &next);
    c.send = udp_send;
    c.recv = udp_recv;
    c.found = found;
    ok_v4_crawl_init(&c, priv, fork, (uint32_t)start ^ (uint32_t)getpid());
    if (argc > 3 && !strcmp(argv[2], "--mainnet")) {
        char text[200];
        FILE *seeds = fopen(argv[3], "r");
        while (seeds && fgets(text, sizeof text, seeds)) {
            uint8_t id[64];
            unsigned a, b, cc, d, port, k, v;
            ok_v4_endpoint at;
            for (k = 0; k < 64 && sscanf(text + 8 + 2 * k, "%2x", &v) == 1; k++) id[k] = (uint8_t)v;
            if (k < 64 || sscanf(text + 8 + 128, "@%u.%u.%u.%u:%u", &a, &b, &cc, &d, &port) != 5) continue;
            at.ip[0] = (uint8_t)a;
            at.ip[1] = (uint8_t)b;
            at.ip[2] = (uint8_t)cc;
            at.ip[3] = (uint8_t)d;
            at.tcp = at.udp = (uint16_t)port;
            ok_v4_crawl_add_node(&c, id, &at);
        }
        if (seeds) fclose(seeds);
    } else {
        for (i = 0; i < ok_sepolia_bootnode_count; i++) ok_v4_crawl_add_enr(&c, ok_sepolia_bootnodes[i]);
    }
    while ((now = time(NULL)) - start < seconds) {
        ok_v4_crawl_step(&c, (uint32_t)now);
        usleep(20000);
    }
    fprintf(stderr, "okupa-crawl: %ds, %d nodes known, %u bonded, %u records, %u on the network, %u sent, %u received\n",
            seconds, c.count, c.bonded, c.records, c.found_count, c.sent, c.received);
    ok_wipe(priv, sizeof priv);
    return c.found_count ? 0 : 1;
}
