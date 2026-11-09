#include "friend_net.h"
#include <arpa/inet.h>
#include <dswifi9.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* The friend game's web server: one listening socket and two connections at
 * most, all non-blocking, stepped once a frame from the game loop. Each
 * connection reads one request, gets its answer from game/src/friend.c, sends
 * it as the socket takes it, and closes. Nothing here ever waits. */
#define CLIENTS 2

static int listener = -1;
static struct {
    int fd;
    size_t got, sent, len;
    uint32_t since;
    char in[3072];
    char out[OK_FRIEND_OUT];
} client[CLIENTS];

int ok_dsi_friend_open(void)
{
    struct sockaddr_in a;
    int one = 1, k;
    if (listener >= 0) return 1;
    if (!Wifi_GetIP()) return 0;
    for (k = 0; k < CLIENTS; k++) client[k].fd = -1;
    if ((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0) return 0;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(OK_FRIEND_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&a, sizeof a) || listen(listener, 2)) {
        close(listener);
        listener = -1;
        return 0;
    }
    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL, 0) | O_NONBLOCK);
    return 1;
}

void ok_dsi_friend_close(void)
{
    int k;
    for (k = 0; k < CLIENTS; k++)
        if (client[k].fd >= 0) close(client[k].fd), client[k].fd = -1;
    if (listener >= 0) close(listener);
    listener = -1;
}

void ok_dsi_friend_where(char out[24])
{
    struct in_addr ip;
    ip.s_addr = Wifi_GetIP();
    snprintf(out, 24, "%s:%d", inet_ntoa(ip), OK_FRIEND_PORT);
}

static void drop(int k)
{
    close(client[k].fd);
    client[k].fd = -1;
}

void ok_dsi_friend_step(ok_friend *f, uint32_t now)
{
    int k;
    if (listener < 0) return;
    for (k = 0; k < CLIENTS; k++) { /* a newcomer, if there is room */
        int fd;
        if (client[k].fd >= 0) continue;
        if ((fd = accept(listener, NULL, NULL)) < 0) break;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        client[k].fd = fd;
        client[k].got = client[k].sent = client[k].len = 0;
        client[k].since = now;
    }
    for (k = 0; k < CLIENTS; k++) {
        int r;
        if (client[k].fd < 0) continue;
        if (now - client[k].since > 5) { /* stalled: let it go */
            drop(k);
            continue;
        }
        if (!client[k].len) { /* still reading the request */
            r = recv(client[k].fd, client[k].in + client[k].got, sizeof client[k].in - client[k].got, 0);
            if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN)) {
                drop(k);
                continue;
            }
            if (r > 0) client[k].got += (size_t)r;
            client[k].len = ok_friend_http(f, client[k].in, client[k].got, now, client[k].out, sizeof client[k].out);
            if (!client[k].len && client[k].got == sizeof client[k].in) drop(k); /* too big, and never whole */
            continue;
        }
        r = send(client[k].fd, client[k].out + client[k].sent, client[k].len - client[k].sent, 0);
        if (r > 0) client[k].sent += (size_t)r;
        else if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
            drop(k);
            continue;
        }
        if (client[k].sent == client[k].len) {
            shutdown(client[k].fd, SHUT_WR);
            drop(k);
        }
    }
}
