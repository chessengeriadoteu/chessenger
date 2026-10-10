# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Dariusz Domagała
#
# make              build for this machine with portable flags (macOS, Linux, Windows MSYS2/MinGW)
# make release      all published binaries except 32-bit Windows into dist/ (needs zig >= 0.13 for Linux and Windows)
# make release-x32  32-bit Windows binary into dist/ (needs zig >= 0.13)

CC      ?= cc
ZIG     ?= zig
CFLAGS  := -std=c99 -O3 -flto -DNDEBUG -Wall -Wextra
LDLIBS  := -lm
ENGINE  := $(strip $(filter-out win_stdin.c,$(wildcard *.c)) $(wildcard fathom/tbprobe.c))
VERSION := 2.0
# release date in the build banner (`main.c`, BUILD_DATE); only a string, search and evaluation do not read it
BUILD_DATE := 2026-10-10
CFLAGS  += '-DBUILD_DATE="$(BUILD_DATE)"'

# Syzygy (version 2.0 and later): fathom/tbprobe.c is the only Fathom unit (it includes tbchess.c itself), so it is
# listed by name, not by fathom/*.c. It trips -Wsign-compare under -Wextra (tbprobe.c:2466, third-party code, left unchanged).
ifneq ($(wildcard fathom/tbprobe.c),)
    CFLAGS += -Wno-sign-compare
endif
# Lazy SMP (version 2.0 and later, smp.c): the code is compiled in only with -DSMP=1 and needs -pthread
ifneq ($(wildcard smp.c),)
    CFLAGS += -DSMP=1 -pthread
endif

ifeq ($(OS),Windows_NT)
    ARCH    ?= -march=x86-64-v3
    SOURCES := $(ENGINE) win_stdin.c
    EXE     := chessenger.exe
else
    UNAME_S := $(shell uname -s)
    UNAME_M := $(shell uname -m)
    SOURCES := $(ENGINE)
    EXE     := chessenger
    ifeq ($(UNAME_S),Darwin)
        ifeq ($(UNAME_M),arm64)
            ARCH ?= -mcpu=apple-m1
        else
            ARCH ?= -march=x86-64-v2
        endif
    else
        CFLAGS += -D_POSIX_C_SOURCE=200809L
        ifeq ($(UNAME_M),x86_64)
            ARCH ?= -march=x86-64-v2
        else
            ARCH ?= -march=armv8-a
        endif
    endif
endif

$(EXE): $(SOURCES) $(wildcard *.h fathom/*.h fathom/*.c)
	$(CC) $(CFLAGS) $(ARCH) -o $@ $(SOURCES) $(LDLIBS)

D := dist/chessenger-$(VERSION)
LINUX := -D_POSIX_C_SOURCE=200809L -static -s
WIN   := win_stdin.c -s
# zig 0.17 + mingw: -flto does not link (undefined nanl in zigc), so Windows builds go without LTO
WCFLAGS := $(filter-out -flto,$(CFLAGS))
# macos-arm64 is built with PGO: instrumented binary, training run on `bench`, final binary with the profile.
# Search output (nodes) is identical to the build without PGO; only speed changes.
PROFDATA ?= xcrun llvm-profdata
PGO      := pgo_training
MACARM   := -arch arm64 $(CFLAGS) -mcpu=apple-m1 -mmacosx-version-min=11.0

release:
	mkdir -p dist
	rm -rf $(PGO) && mkdir -p $(PGO)
	$(CC) $(MACARM) -fprofile-instr-generate=$(PGO)/%p.profraw -o $(PGO)/chessenger $(ENGINE) $(LDLIBS)
	./$(PGO)/chessenger bench < /dev/null > $(PGO)/bench.log 2>&1
	$(PROFDATA) merge -o $(PGO)/chessenger.profdata $(PGO)/*.profraw
	$(CC) $(MACARM) -fprofile-instr-use=$(PGO)/chessenger.profdata -DBUILD_PGO=1 -o $(D)-macos-arm64 $(ENGINE) $(LDLIBS)
	rm -rf $(PGO)
	$(CC) -arch x86_64 $(CFLAGS) -march=x86-64 -mmacosx-version-min=10.9 -o $(D)-macos-x64   $(ENGINE) $(LDLIBS)
	$(ZIG) cc -target aarch64-linux-musl  -mcpu=generic        $(CFLAGS) $(LINUX) -o $(D)-linux-arm64 $(ENGINE) $(LDLIBS)
	$(ZIG) cc -target x86_64-linux-musl   -mcpu=x86_64_v2      $(CFLAGS) $(LINUX) -o $(D)-linux-x64   $(ENGINE) $(LDLIBS)
	$(ZIG) cc -target x86_64-windows-gnu  -mcpu=x86_64         $(WCFLAGS) -o $(D)-windows-x64.exe        $(ENGINE) $(WIN) $(LDLIBS)
	$(ZIG) cc -target x86_64-windows-gnu  -mcpu=x86_64_v2      $(WCFLAGS) -o $(D)-windows-x64-popcnt.exe $(ENGINE) $(WIN) $(LDLIBS)
	$(ZIG) cc -target x86_64-windows-gnu  -mcpu=x86_64_v2+avx2 $(WCFLAGS) -o $(D)-windows-x64-avx2.exe   $(ENGINE) $(WIN) $(LDLIBS)
	$(ZIG) cc -target x86_64-windows-gnu  -mcpu=x86_64_v3      $(WCFLAGS) -o $(D)-windows-x64-bmi2.exe   $(ENGINE) $(WIN) $(LDLIBS)
	$(ZIG) cc -target aarch64-windows-gnu -mcpu=generic        $(WCFLAGS) -o $(D)-windows-arm64.exe      $(ENGINE) $(WIN) $(LDLIBS)

# 32-bit Windows, built separately
release-x32:
	mkdir -p dist
	$(ZIG) cc -target x86-windows-gnu -mcpu=i686 $(WCFLAGS) -o $(D)-windows-x32.exe $(ENGINE) $(WIN) $(LDLIBS)

clean:
	rm -rf $(EXE) dist $(PGO)

.PHONY: release release-x32 clean
