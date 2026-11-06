/* HTTPS with the core alone (BearSSL TLS + our HTTP), the way the console will
 * talk to 0G. Only the TCP socket and the entropy live here.
 *   build/okupa-https <host> <path> [json body]   (POST if a body is given) */
#include "okupa_http.h"
#include "okupa_tls.h"
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    ssize_t n = read(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    ssize_t n = write(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

int main(int argc, char **argv)
{
    static ok_tls t;
    static char req[8192], resp[65536];
    struct addrinfo hints, *ai;
    struct timeval tv = {20, 0};
    uint8_t seed[32];
    ok_http_response r;
    size_t n, got = 0, body;
    const char *body_in = argc > 3 ? argv[3] : NULL;
    FILE *f;
    int fd;
    ok_result res;

    if (argc < 3) {
        fprintf(stderr, "usage: okupa-https <host> <path> [json body]\n");
        return 2;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(argv[1], "443", &hints, &ai)) return 1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        perror("connect");
        return 1;
    }
    freeaddrinfo(ai);
    if (!(f = fopen("/dev/urandom", "rb")) || fread(seed, 1, sizeof seed, f) != sizeof seed) return 1;
    fclose(f);

    ok_tls_open(&t, argv[1], (int64_t)time(NULL), seed, sizeof seed, sock_read, sock_write, &fd);
    if (ok_http_request(body_in ? "POST" : "GET", argv[1], argv[2], body_in ? "Content-Type: application/json\r\n" : NULL,
                        body_in, body_in ? strlen(body_in) : 0, req, sizeof req, &n) != OK_OK)
        return 1;
    if (ok_tls_write(&t, req, n) != OK_OK) {
        fprintf(stderr, "okupa-https: TLS failed, BearSSL error %d\n", ok_tls_error(&t));
        return 1;
    }
    for (;;) {
        if (ok_tls_read(&t, resp + got, sizeof resp - 1 - got, &n) != OK_OK) break;
        if (n == 0) break;
        got += n;
        if ((res = ok_http_response_read(resp, got, &r, &body)) == OK_OK && body < got) break;
        if (got >= sizeof resp - 1) break;
    }
    close(fd);
    if (ok_http_response_read(resp, got, &r, &body) != OK_OK) {
        fprintf(stderr, "okupa-https: incomplete response (%lu bytes, TLS error %d)\n", (unsigned long)got, ok_tls_error(&t));
        return 1;
    }
    printf("okupa-https: TLS ok (roots pinned in the core), HTTP %d, %lu bytes\n", r.status, (unsigned long)body);
    fwrite(r.body, 1, body, stdout);
    printf("\n");
    return r.status == 200 ? 0 : 1;
}
