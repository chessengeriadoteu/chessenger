/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <errno.h>
#include <io.h>
#include <conio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

int __cdecl _get_stream_buffer_pointers(FILE*, char***, char***, int**);
static int stdio_held(void* buf, unsigned int count) {
    char **base, **ptr;
    int* cnt;
    if (_get_stream_buffer_pointers(stdin, &base, &ptr, &cnt) != 0 || *cnt <= 0) return 0;
    if (buf == NULL) return *cnt;
    const int take = *cnt < (int)count ? *cnt : (int)count;
    memcpy(buf, *ptr, (size_t)take);
    *ptr += take;
    *cnt -= take;
    return take;
}

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

#include <stdio.h>
#include <string.h>

static volatile LONG con_raw = 0;
static char con_line[1024];
static int con_len = 0;
static int con_shown = 0;
static char con_done[4096];
static int con_done_len = 0;
static WCHAR con_last = 0;

static void con_echo(const char* s, int n) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0, written = 0;
    if (n <= 0 || out == NULL || out == INVALID_HANDLE_VALUE || !GetConsoleMode(out, &mode)) return;
    fflush(stdout);
    WriteConsoleA(out, s, (DWORD)n, &written, NULL);
}

static void con_inject(HANDLE h, const char* s, int n, int enter) {
    for (int i = 0; i <= n; i++) {
        if (i == n && !enter) break;
        const WCHAR c = i < n ? (WCHAR)(unsigned char)s[i] : L'\r';
        INPUT_RECORD r[2];
        DWORD written = 0;
        memset(r, 0, sizeof(r));
        r[0].EventType = KEY_EVENT;
        r[0].Event.KeyEvent.bKeyDown = TRUE;
        r[0].Event.KeyEvent.wRepeatCount = 1;
        r[0].Event.KeyEvent.wVirtualKeyCode = i < n ? (WORD)(VkKeyScanW(c) & 0xFF) : (WORD)VK_RETURN;
        r[0].Event.KeyEvent.uChar.UnicodeChar = c;
        r[1] = r[0];
        r[1].Event.KeyEvent.bKeyDown = FALSE;
        WriteConsoleInputW(h, r, 2, &written);
    }
}

void win_stdin_console(int on) {
    int is_pipe;
    (void)input_handle(&is_pipe);
    InterlockedExchange(&con_raw, (on && !is_pipe) ? 1 : 0);
}

void win_stdin_console_prompt(int cooked) {
    int is_pipe;
    HANDLE h = input_handle(&is_pipe);
    if (is_pipe) return;
    con_shown = 0;
    if (!cooked) return;
    int start = 0;
    for (int i = 0; i < con_done_len; i++)
        if (con_done[i] == '\n') {
            con_inject(h, con_done + start, i - start, 1);
            start = i + 1;
        }
    con_done_len = 0;
    if (con_len > 0) con_inject(h, con_line, con_len, 0);
    con_len = 0;
}

static int con_read_raw(HANDLE h, void* buf, unsigned int count) {
    DWORD mode = 0, n = 0;
    const int echo = GetConsoleMode(h, &mode) && (mode & ENABLE_ECHO_INPUT);
    if (echo && con_shown < con_len) {
        con_echo(con_line + con_shown, con_len - con_shown);
        con_shown = con_len;
    }
    while (GetNumberOfConsoleInputEvents(h, &n) && n > 0) {
        INPUT_RECORD rec[32];
        DWORD got = 0;
        if (!ReadConsoleInputW(h, rec, n < 32 ? n : 32, &got) || got == 0) break;
        for (DWORD i = 0; i < got; i++) {
            if (rec[i].EventType != KEY_EVENT || !rec[i].Event.KeyEvent.bKeyDown) continue;
            const WCHAR c = rec[i].Event.KeyEvent.uChar.UnicodeChar;
            const WORD vk = rec[i].Event.KeyEvent.wVirtualKeyCode;
            const int repeat = rec[i].Event.KeyEvent.wRepeatCount > 0 ? rec[i].Event.KeyEvent.wRepeatCount : 1;
            for (int k = 0; k < repeat; k++) {
                if (c == L'\n' && con_last == L'\r') {
                    con_last = 0;
                    continue;
                }
                con_last = c;
                if (c == L'\r' || c == L'\n' || vk == VK_RETURN) {
                    if (con_done_len + con_len + 1 <= (int)sizeof(con_done)) {
                        memcpy(con_done + con_done_len, con_line, (size_t)con_len);
                        con_done_len += con_len;
                        con_done[con_done_len++] = '\n';
                    }
                    con_len = con_shown = 0;
                    if (echo) con_echo("\r\n", 2);
                } else if (c == 8 || c == 127 || vk == VK_BACK) {
                    if (con_len > 0) {
                        con_len--;
                        if (echo && con_shown > con_len) {
                            con_echo("\b \b", 3);
                            con_shown = con_len;
                        }
                    }
                } else if (c >= 32 && c < 127) {
                    if (con_len < (int)sizeof(con_line) - 1) {
                        con_line[con_len++] = (char)c;
                        if (echo) {
                            const char ch = (char)c;
                            con_echo(&ch, 1);
                            con_shown = con_len;
                        }
                    }
                }
            }
        }
    }
    if (con_done_len == 0) {
        errno = EAGAIN;
        return -1;
    }
    int take = con_done_len < (int)count ? con_done_len : (int)count;
    memcpy(buf, con_done, (size_t)take);
    memmove(con_done, con_done + take, (size_t)(con_done_len - take));
    con_done_len -= take;
    return take;
}

int win_stdin_ready(void) {
    int is_pipe;
    DWORD available = 0;
    HANDLE handle = input_handle(&is_pipe);
    if (stdio_held(NULL, 0) > 0) return 1;
    if (!is_pipe) return _kbhit();
    if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) return 1;
    return available > 0;
}

int win_stdin_read(int fd, void* buf, unsigned int count) {
    int is_pipe;
    DWORD available = 0;
    HANDLE handle = input_handle(&is_pipe);
    const int held = stdio_held(buf, count);
    if (held > 0) return held;
    if (!is_pipe && con_raw) return con_read_raw(handle, buf, count);
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
