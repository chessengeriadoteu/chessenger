/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifdef __wasi__
#include <signal.h>
#include <termios.h>
#include <__typedef_sigset_t.h>
int dup(int fd);
int dup2(int a, int b);
int pipe(int fd[2]);
#ifndef SA_RESTART
struct sigaction { void (*sa_handler)(int); sigset_t sa_mask; int sa_flags; };
#define SA_RESTART 0x10000000
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
int sigemptyset(sigset_t* s);
int sigaddset(sigset_t* s, int n);
int sigaction(int n, const struct sigaction* a, struct sigaction* o);
int sigprocmask(int h, const sigset_t* s, sigset_t* o);
#endif
#ifndef SIGTSTP
#define SIGTSTP 18
#endif
#ifndef SIGCONT
#define SIGCONT 19
#endif
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#endif
#if defined(__wasi__) && !defined(TIOCGWINSZ)
#define TIOCGWINSZ 0x5413
#endif
#if defined(__wasi__) && !defined(__ENDIAN_DEFINED)
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _BYTE_ORDER _LITTLE_ENDIAN
#define __ENDIAN_DEFINED 1
#endif
