/* tracecatch.c — occupy the firmware's trace socket and print what arrives, so the mss's own
 * internal logs (libtraceif) become visible. The firmware daemons log to a stream Unix socket
 * (/tmp/trace_unix_socket) instead of stdout; on the bench nobody answers it, so they run mute.
 * This answers it and copies each chunk to stdout, non-text bytes shown as '.'. Read-only. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#define MAXC 64

int main(int argc, char **argv)
{
    struct sockaddr_un a;
    struct pollfd p[MAXC + 1];
    int n = 1, i, ls;
    static unsigned char buf[65536];
    static char txt[65536 + 1];
    if (argc < 2) { fprintf(stderr, "usage: tracecatch <socket>\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);
    unlink(argv[1]);
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, argv[1], sizeof a.sun_path - 1);
    ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 32) < 0) { perror("socket"); return 2; }
    chmod(argv[1], 0777);
    setvbuf(stdout, NULL, _IOLBF, 0);
    p[0].fd = ls; p[0].events = POLLIN;
    for (;;) {
        if (poll(p, (nfds_t)n, -1) < 0) { if (errno == EINTR) continue; break; }
        if (p[0].revents & POLLIN) {
            int fd = accept(ls, NULL, NULL);
            if (fd >= 0) { if (n <= MAXC) { p[n].fd = fd; p[n].events = POLLIN; p[n].revents = 0; n++; } else close(fd); }
        }
        for (i = 1; i < n; i++) {
            ssize_t r, k;
            if (!(p[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            r = read(p[i].fd, buf, sizeof buf);
            if (r <= 0) { close(p[i].fd); p[i] = p[n - 1]; n--; i--; continue; }
            for (k = 0; k < r; k++) txt[k] = (buf[k] >= 32 && buf[k] < 127) ? (char)buf[k] : '.';
            txt[r] = 0;
            printf("%s\n", txt);
        }
    }
    return 0;
}
