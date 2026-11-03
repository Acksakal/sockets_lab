# TCP sockets in C90

A minimal TCP server and client in C90 + POSIX, written as a reference for
how to use the sockets API correctly. `socket_play.html` is an interactive,
step-by-step visual explanation of what happens underneath, including testing
with `nc`.

| File | What it is |
|---|---|
| `server.c` | Waits for clients and answers each one's single line of text |
| `client.c` | Sends one line typed by the user and prints the answer |
| `socket_play.html` | The visual guide; open it in any browser |

## Build

```sh
gcc -std=c90 -pedantic -Wall -Wextra -Werror server.c -o server
gcc -std=c90 -pedantic -Wall -Wextra -Werror client.c -o client
```

## Run

```sh
./server                 # terminal 1: listens on 127.0.0.1 port 12345
./client                 # terminal 2: type  hello from Dan's client
```

The server answers `Hi there!` to that exact line and `Invalid message!` to
anything else, then waits for the next client.

Both programs take an optional host and port: `./server 0.0.0.0` accepts
connections from other machines, and `./client 192.168.1.23` connects to one.

## Protocol

One line of text in each direction, ending in `\n` (`\r\n` is also accepted),
at most 255 bytes including the newline. One exchange per connection.

## Practices it demonstrates

- **`getaddrinfo()`** turns a host and port into addresses, so the same code
  works for IPv4 and IPv6. Each returned address is tried in turn.
- **Every return value is checked.** Failed calls report the reason with
  `perror()`, `strerror()` or `gai_strerror()`.
- **TCP is a byte stream:** `recv()` may return part of a line and `send()`
  may send part of a buffer, so both are called in loops (`recv_line()`,
  `send_all()`).
- **Timeouts** (`SO_RCVTIMEO`): a silent client can hold the server for at
  most 10 seconds, and the client waits at most 20 seconds for an answer.
- **`SIGPIPE` is ignored**, so writing to a peer that has already closed
  fails with `EPIPE` instead of killing the process.
- **`EINTR` is retried**: a call interrupted by a signal is repeated.
- **`SO_REUSEADDR`** lets the server restart at once while old connections
  are still in TIME-WAIT.
- **The server listens on 127.0.0.1 by default**, so nothing outside this
  machine can reach it unless you ask for that.
- **The client reads the message before connecting**, so it doesn't hold
  the server while you type.

The server handles one client at a time. Serving several at once needs
`fork()`, threads or `poll()`.

## Portability

C90 language rules (no `//` comments, declarations at the start of blocks,
no `long long`) plus POSIX.1-2008 for the sockets API, requested with
`_POSIX_C_SOURCE 200809L`. Tested on Linux with GCC and Clang, including
builds with AddressSanitizer and UndefinedBehaviorSanitizer. Written for
Cygwin as well.

## References

- POSIX (The Open Group Base Specifications):
  [getaddrinfo](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getaddrinfo.html),
  [getnameinfo](https://man.archlinux.org/man/getnameinfo.3p.en.raw),
  [socket](https://pubs.opengroup.org/onlinepubs/9799919799/functions/socket.html),
  [listen](https://pubs.opengroup.org/onlinepubs/9699919799.2013edition/functions/listen.html),
  [accept](https://pubs.opengroup.org/onlinepubs/9699919799/functions/accept.html),
  [send](https://man.archlinux.org/man/send.3p.en),
  [recv](https://man7.org/linux/man-pages/man3/recv.3p.html),
  [setsockopt](https://manpages.org/setsockopt),
  [&lt;sys/socket.h&gt;](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_socket.h.html)
- [RFC 9293: Transmission Control Protocol](https://rfc-editor.org/rfc/rfc9293.html)
- [GNU C Library: Operation Error Signals (SIGPIPE)](https://www.gnu.org/s/hello/manual/libc/Operation-Error-Signals.html)
