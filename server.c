/*
 * server.c - a minimal, careful TCP server (C90 + POSIX).
 *
 * Protocol: the client sends one line of text ending in '\n'. If the line is
 * the expected greeting, the server answers "Hi there!\n", otherwise
 * "Invalid message!\n". Then it closes the connection and waits for the next
 * client. Clients are served one at a time.
 *
 * Usage: ./server [host [port]]      default: 127.0.0.1 12345
 *        ./server 0.0.0.0            accept connections from other machines
 *        ./server ::1                IPv6 loopback
 *
 * Build: gcc -std=c90 -pedantic -Wall -Wextra -Werror server.c -o server
 */
#define _POSIX_C_SOURCE 200809L     /* sockets, getaddrinfo, sigaction are POSIX */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEFAULT_HOST    "127.0.0.1"     /* loopback: only this machine can connect */
#define DEFAULT_PORT    "12345"
#define EXPECTED        "hello from Dan's client"
#define MSG_SIZE        256             /* longest accepted line, including '\n' */
#define CLIENT_TIMEOUT  10              /* seconds a client may stay silent */
/* Buffers for a peer printed in numeric form. INET6_ADDRSTRLEN (POSIX) fits
   any IPv6 or IPv4 address; the extra room is for an IPv6 "%zone" suffix.
   If a name still doesn't fit, getnameinfo() fails and we say so. */
#define HOST_STR_SIZE   (INET6_ADDRSTRLEN + 16)
#define PORT_STR_SIZE   6               /* "65535" plus the '\0' */

/* recv_line() results other than a line length (which is >= 0) */
#define LINE_ERROR      (-1)            /* error or timeout; errno says which */
#define LINE_TOO_LONG   (-2)            /* no '\n' within the buffer */
#define LINE_CLOSED     (-3)            /* peer closed before sending a line */

/* Create a socket listening on host:port. getaddrinfo() returns a list of
   candidate addresses (IPv4 and/or IPv6); use the first one that works. */
static int listen_on(const char *host, const char *port)
{
    struct addrinfo hints, *res, *ai;
    int fd = -1, on = 1, rc, saved_errno = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;        /* IPv4 or IPv6, whatever host is */
    hints.ai_socktype = SOCK_STREAM;    /* TCP */
    hints.ai_flags = AI_PASSIVE;        /* the address is for bind() */

    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo %s %s: %s\n", host, port, gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            saved_errno = errno;
            continue;
        }
        /* allow an immediate restart while old connections are in TIME-WAIT */
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == 0 &&
            bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
            listen(fd, SOMAXCONN) == 0)
            break;                      /* success */
        saved_errno = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        fprintf(stderr, "cannot listen on %s %s: %s\n", host, port, strerror(saved_errno));
    return fd;
}

/* Send all len bytes: send() may send fewer than asked. */
static int send_all(int fd, const char *buf, size_t len)
{
    ssize_t n;

    while (len > 0) {
        n = send(fd, buf, len, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;               /* interrupted before sending: retry */
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Read one line. TCP is a byte stream, so a line may arrive in pieces:
   keep calling recv() until '\n' arrives. Stores the line without its
   "\r\n" and returns its length, or LINE_ERROR / LINE_TOO_LONG /
   LINE_CLOSED. Bytes after the '\n' are ignored: this protocol sends one
   line per connection. */
static long recv_line(int fd, char *buf, size_t size)
{
    size_t used = 0;
    ssize_t n;
    char *nl;

    for (;;) {
        if (used == size - 1)
            return LINE_TOO_LONG;
        n = recv(fd, buf + used, size - 1 - used, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return LINE_ERROR;
        }
        if (n == 0)
            return LINE_CLOSED;         /* orderly shutdown, no full line */
        used += (size_t)n;
        buf[used] = '\0';
        nl = strchr(buf, '\n');
        if (nl != NULL) {
            *nl = '\0';
            if (nl > buf && nl[-1] == '\r')
                nl[-1] = '\0';          /* accept telnet-style "\r\n" too */
            return (long)strlen(buf);
        }
    }
}

/* Serve one connected client, then return. */
static void serve(int fd)
{
    char line[MSG_SIZE];
    const char *reply;
    struct timeval tv;
    long n;

    /* A silent client must not block everyone behind it forever. */
    tv.tv_sec = CLIENT_TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0)
        perror("setsockopt SO_RCVTIMEO");

    n = recv_line(fd, line, sizeof line);
    if (n == LINE_CLOSED) {
        printf("  client closed the connection without sending a line\n");
        return;
    }
    if (n == LINE_ERROR) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            printf("  client silent for %d s, dropping it\n", CLIENT_TIMEOUT);
        else
            perror("  recv");
        return;
    }
    if (n == LINE_TOO_LONG) {
        printf("  line longer than %d bytes\n", MSG_SIZE - 1);
        reply = "Invalid message!\n";
    } else {
        printf("  client says: %s\n", line);
        reply = strcmp(line, EXPECTED) == 0 ? "Hi there!\n" : "Invalid message!\n";
    }
    if (send_all(fd, reply, strlen(reply)) < 0)
        perror("  send");
}

int main(int argc, char *argv[])
{
    const char *host = argc > 1 ? argv[1] : DEFAULT_HOST;
    const char *port = argc > 2 ? argv[2] : DEFAULT_PORT;
    struct sigaction sa;
    struct sockaddr_storage peer;       /* big enough for IPv4 and IPv6 */
    socklen_t peer_len;
    char peer_host[HOST_STR_SIZE], peer_port[PORT_STR_SIZE];
    int listen_fd, fd;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [host [port]]\n", argv[0]);
        return 2;
    }

    /* Writing to a connection the client already closed raises SIGPIPE,
       whose default action terminates the process. Ignore it, so send()
       fails with EPIPE instead and the server keeps running. */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    listen_fd = listen_on(host, port);
    if (listen_fd < 0)
        return 1;
    printf("listening on %s port %s\n", host, port);

    for (;;) {
        fflush(stdout);                 /* show progress even when piped */
        peer_len = sizeof peer;
        fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
        if (fd < 0) {
            if (errno != EINTR)
                perror("accept");
            continue;                   /* a failed accept is not fatal */
        }
        if (getnameinfo((struct sockaddr *)&peer, peer_len,
                        peer_host, sizeof peer_host, peer_port, sizeof peer_port,
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            printf("connection from %s port %s\n", peer_host, peer_port);
        else
            printf("connection from an unknown address\n");

        serve(fd);
        close(fd);
    }
}
