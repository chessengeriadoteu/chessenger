/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifdef __wasi__
#include <unistd.h>
#include <fcntl.h>
ssize_t wasi_read_in_search(int fd, void* buf, size_t n, long long limit_ms);
#define read(fd, buf, n) wasi_read_in_search((fd), (buf), (n), search_time_limit_ms)
#endif
