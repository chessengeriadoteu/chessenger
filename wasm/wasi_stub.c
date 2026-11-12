/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <errno.h>
#include <string.h>
#include "wasi_shim.h"
int dup(int fd) { (void)fd; errno = ENOSYS; return -1; }
int dup2(int a, int b) { (void)a; (void)b; errno = ENOSYS; return -1; }
int pipe(int fd[2]) { (void)fd; errno = ENOSYS; return -1; }
int tcgetattr(int fd, struct termios* t) { (void)fd; (void)t; errno = ENOTTY; return -1; }
int tcsetattr(int fd, int a, const struct termios* t) { (void)fd; (void)a; (void)t; errno = ENOTTY; return -1; }
int sigemptyset(sigset_t* s) { memset(s, 0, sizeof *s); return 0; }
int sigaddset(sigset_t* s, int n) { (void)s; (void)n; return 0; }
int sigaction(int n, const struct sigaction* a, struct sigaction* o) { (void)n; (void)a; if (o) memset(o, 0, sizeof *o); return 0; }
int sigprocmask(int h, const sigset_t* s, sigset_t* o) { (void)h; (void)s; if (o) memset(o, 0, sizeof *o); return 0; }
int posix_madvise(void* a, __SIZE_TYPE__ n, int r) { (void)a; (void)n; (void)r; return 0; }

#include <time.h>
#include <unistd.h>
#define WASI_PAUSE_S 60
static double wasi_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }
static int wasi_pread_ok = -1;
static int wasi_pread_works(void) {
    if (wasi_pread_ok < 0) { char c; wasi_pread_ok = pread(0, &c, 0, 0) >= 0; }
    return wasi_pread_ok;
}
int wasi_stop_delay_s(void) { return wasi_pread_works() ? 0 : WASI_PAUSE_S; }
ssize_t wasi_read_in_search(int fd, void* buf, size_t n, long long limit_ms) {
    static double t_last = -1e9, t_start = 0, t_pread = -1e9;
    if (fd != 0) return read(fd, buf, n);
    if (wasi_pread_works()) {
        const double t = wasi_now();
        if (t - t_pread < 0.05) { errno = EAGAIN; return -1; }
        t_pread = t;
        const ssize_t r = pread(0, buf, n, 0);
        if (r > 0) return r;
        if (r == 0) { errno = EAGAIN; return -1; }
        wasi_pread_ok = 0;
    }
    if (limit_ms > 0) { errno = EAGAIN; return -1; }
    const double t = wasi_now();
    if (t - t_last > 0.5) t_start = t;
    t_last = t;
    if (t - t_start < WASI_PAUSE_S) { errno = EAGAIN; return -1; }
    const ssize_t r = read(0, buf, n);
    t_start = t_last = wasi_now();
    return r;
}
