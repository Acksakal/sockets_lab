/*
 * client.c - a minimal, careful TCP client (C90 + POSIX).
 *
 * Connects to the server, sends one line typed by the user, prints the
 * server's one-line answer, and exits.
 *
 * Usage: ./client [host [port]]      default: 127.0.0.1 12345
 *
 * Build: gcc -std=c90 -pedantic -Wall -Wextra -Werror client.c -o client
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
#include <netdb.h>

#define DEFAULT_HOST    "127.0.0.1"
#define DEFAULT_PORT    "12345"
#define MSG_SIZE        256             /* longest line, including '\n' */
#define TIMEOUT         20              /* seconds to wait for the server */

/* recv_line() results other than a line length (which is >= 0) */
#define LINE_ERROR      (-1)            /* error or timeout; errno says which */
#define LINE_TOO_LONG   (-2)            /* no '\n' within the buffer */
#define LINE_CLOSED     (-3)            /* peer closed before sending a line */

/* Connect to host:port. getaddrinfo() may return several addresses (for
   example IPv6 and IPv4 for "localhost"); try each until one connects. */
static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints, *res, *ai;
    int fd = -1, rc, saved_errno = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;        /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;    /* TCP */

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
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;                      /* connected */
        saved_errno = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        fprintf(stderr, "cannot connect to %s %s: %s\n", host, port, strerror(saved_errno));
    return fd;
}

/* Give up on send() and recv() after TIMEOUT seconds instead of waiting forever. */
static int set_timeouts(int fd)
{
    struct timeval tv;

    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0)
        return -1;
    return 0;
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

/* Read one line (see server.c): recv() until '\n', since TCP may split it. */
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
            return LINE_CLOSED;
        used += (size_t)n;
        buf[used] = '\0';
        nl = strchr(buf, '\n');
        if (nl != NULL) {
            *nl = '\0';
            if (nl > buf && nl[-1] == '\r')
                nl[-1] = '\0';
            return (long)strlen(buf);
        }
    }
}

int main(int argc, char *argv[])
{
    const char *host = argc > 1 ? argv[1] : DEFAULT_HOST;
    const char *port = argc > 2 ? argv[2] : DEFAULT_PORT;
    struct sigaction sa;
    char line[MSG_SIZE], reply[MSG_SIZE];
    size_t len;
    long n;
    int fd, status = 1;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [host [port]]\n", argv[0]);
        return 2;
    }

    /* If the server closes first, send() would raise SIGPIPE and kill us.
       Ignore it, so send() fails with EPIPE and we can report it. */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    printf("Message: ");
    fflush(stdout);
    if (fgets(line, sizeof line, stdin) == NULL) {
        fprintf(stderr, "no input\n");
        return 1;
    }
    len = strlen(line);
    if (len == 0 || line[len - 1] != '\n') {
        if (len == sizeof line - 1) {
            fprintf(stderr, "message longer than %d bytes\n", MSG_SIZE - 2);
            return 1;
        }
        line[len++] = '\n';             /* input ended without Enter: add it */
        line[len] = '\0';
    }

    fd = connect_to(host, port);
    if (fd < 0)
        return 1;
    if (set_timeouts(fd) < 0)
        perror("setsockopt");           /* not fatal: we just wait longer */

    if (send_all(fd, line, len) < 0) {
        perror("send");
    } else {
        n = recv_line(fd, reply, sizeof reply);
        if (n >= 0) {
            printf("Server: %s\n", reply);
            status = 0;
        } else if (n == LINE_CLOSED) {
            fprintf(stderr, "server closed the connection without answering\n");
        } else if (n == LINE_TOO_LONG) {
            fprintf(stderr, "server reply longer than %d bytes\n", MSG_SIZE - 1);
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "no answer within %d s\n", TIMEOUT);
        } else {
            perror("recv");
        }
    }
    close(fd);
    return status;
}
