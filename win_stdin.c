/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <errno.h>
#include <io.h>
#include <conio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE input_handle(int* is_pipe) {
    static HANDLE handle = NULL;
    static int pipe = 0;
    DWORD mode;
    if (handle == NULL) {
        handle = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(handle, &mode);
    }
    *is_pipe = pipe;
    return handle;
}

int win_stdin_ready(void) {
    int is_pipe;
    DWORD available = 0;
    HANDLE handle = input_handle(&is_pipe);
    if (!is_pipe) return _kbhit();
    if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) return 1;
    return available > 0;
}

int win_stdin_read(int fd, void* buf, unsigned int count) {
    int is_pipe;
    DWORD available = 0;
    HANDLE handle = input_handle(&is_pipe);
    if (is_pipe) {
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) return 0;
        if (available == 0) {
            errno = EAGAIN;
            return -1;
        }
        if (available < count) count = (unsigned int)available;
    } else if (!_kbhit()) {
        errno = EAGAIN;
        return -1;
    }
    return _read(fd, buf, count);
}
