# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Dariusz Domagała
#
# make              build for this machine with portable flags (macOS, Linux, Windows MSYS2/MinGW)
# make release      all published binaries except 32-bit Windows into dist/ (needs zig for Linux and Windows, tested with 0.17)
# make release-x32  32-bit Windows binary into dist/ (needs zig, tested with 0.17)
# make release also needs Homebrew LLVM (LLVM_BIN, the LLVM version of zig's clang) for the Linux binaries with PGO

CC      ?= cc
ZIG     ?= zig
CFLAGS  := -std=c99 -O3 -flto -DNDEBUG -Wall -Wextra
LDLIBS  := -lm
ENGINE  := $(strip $(filter-out win_stdin.c,$(sort $(wildcard *.c))) $(wildcard fathom/tbprobe.c))
# Playing levels (version 3.1 and later): levels/levels.c is the only unit of the folder levels/ (it includes the
# other files there itself), so it is listed by name, not by levels/*.c.
ENGINE  += levels/levels.c
VERSION := 3.1
# release date in the build banner (`main.c`, BUILD_DATE); only a string, search and evaluation do not read it
BUILD_DATE := 2026-10-21
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

# Light binary (version 3.1 and later): `make LIGHT=1` builds without the network and without the tablebases inside.
# The engine reads the network file siec_g1_w01.bin from the UCI option EvalFile, from its own folder or from the current
# folder, and the Syzygy tablebases from the folder tablebases/ next to it (or from SyzygyPath). Without a network it plays
# on the classic evaluation (one thread) and says so. Neither siec_g1_w01.bin nor tablebases/ is needed to build it; the
# binaries get the suffix -light. When switching LIGHT run `make clean` first: make does not track CFLAGS.
LIGHT ?= 0
ifeq ($(LIGHT),1)
    ifeq ($(TB_EMBEDDED),1)
        $(error LIGHT=1 builds without built-in tablebases: leave out TB_EMBEDDED=1)
    endif
    CFLAGS += -DNNUE_LIGHT=1 -DTB_NEXT_TO_ENGINE=1
    TB_EMBEDDED := 0
endif
# NNUE network (version 3.0 and later): nnue.c embeds the network file with the C23 embed directive (clang 19+, gcc 15+).
# The file must lie next to nnue.c; a missing file or a different md5 stops the build, so no binary is built without its network.
NNUE_FILE := siec_g1_w01.bin
NNUE_MD5  := 9cc71f559520fb91ed4ff27b593ea32c
ifneq ($(LIGHT),1)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
    ifeq ($(wildcard $(NNUE_FILE)),)
        $(error network file $(NNUE_FILE) is missing next to nnue.c: the build embeds it into the binary)
    endif
    NNUE_MD5_FILE := $(firstword $(shell md5 -q '$(NNUE_FILE)' 2>/dev/null || md5sum '$(NNUE_FILE)' 2>/dev/null))
    ifneq ($(NNUE_MD5_FILE),$(NNUE_MD5))
        $(error network file $(NNUE_FILE) has md5 $(NNUE_MD5_FILE), expected $(NNUE_MD5))
    endif
endif
endif

# Syzygy tablebases for 3 and 4 pieces built into the binary: syzygy_embedded.c embeds the 70 files from tablebases/
# (35 endings x .rtbw and .rtbz) with the C23 embed directive. `make TB_EMBEDDED=0` builds without them (SyzygyPath then
# needs tablebase files on disk). When switching TB_EMBEDDED run `make clean` first: make does not track CFLAGS.
TB_EMBEDDED ?= 1
TB_DIR   := tablebases
TB_SRC   := syzygy_embedded.c
ifeq ($(TB_EMBEDDED),1)
    TB_ENDINGS := KBBvK KBNvK KBPvK KBvK KBvKB KBvKN KBvKP KNNvK KNPvK KNvK KNvKN KNvKP KPPvK KPvK KPvKP KQBvK KQNvK KQPvK
    TB_ENDINGS += KQQvK KQRvK KQvK KQvKB KQvKN KQvKP KQvKQ KQvKR KRBvK KRNvK KRPvK KRRvK KRvK KRvKB KRvKN KRvKP KRvKR
    TB_FILES   := $(foreach e,$(TB_ENDINGS),$(TB_DIR)/$(e).rtbw $(TB_DIR)/$(e).rtbz)
    ifeq ($(filter clean,$(MAKECMDGOALS)),)
        TB_MISSING := $(filter-out $(wildcard $(TB_FILES)),$(TB_FILES))
        ifneq ($(TB_MISSING),)
            $(error $(words $(TB_MISSING)) of the 70 tablebase files are missing (first: $(firstword $(TB_MISSING))): restore $(TB_DIR)/ or build without built-in tablebases: make TB_EMBEDDED=0)
        endif
    endif
    CFLAGS += -DTB_EMBEDDED=1 --embed-dir=$(TB_DIR)
else
    ENGINE := $(filter-out $(TB_SRC),$(ENGINE))
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

ifneq ($(LIGHT),1)
$(EXE): $(NNUE_FILE)
endif
$(EXE): $(TB_FILES)
$(EXE): $(wildcard levels/*.h levels/*.c)
$(EXE): $(SOURCES) $(wildcard *.h fathom/*.h fathom/*.c)
	$(CC) $(CFLAGS) $(ARCH) -o $@ $(SOURCES) $(LDLIBS)

D := dist/chessenger-$(VERSION)
ifeq ($(LIGHT),1)
    D := $(D)-light
endif
LINUX := -D_POSIX_C_SOURCE=200809L -static -s
WIN   := win_stdin.c -s
# zig 0.17 + mingw: -flto does not link (undefined nanl in zigc), so Windows builds go without LTO
WCFLAGS := $(filter-out -flto,$(CFLAGS))
# Eight release binaries. `-pgo` in the file name = built with profile-guided optimisation: instrumented binary, training
# run on `bench`, final binary with the profile. Search output (nodes) is identical to the build without PGO; only speed changes.
# macos-arm64: Apple clang. linux-arm64, linux-x64-avx2: zig cannot write a profile, so the same source is built for macOS on the
# same CPU architecture with Homebrew clang (LLVM_BIN, the LLVM version of zig's clang: 22 for zig 0.17) and trained there, the x86-64
# one under Rosetta 2 (training binaries are not released; the x86-64 one targets macOS 10.13, the minimum of the Homebrew
# profile runtime, with 16 KB segment alignment, the alignment of its profile sections); zig then uses that profile.
# Functions whose code differs between the macOS and the musl C library headers (character class macros) get no profile,
# which is what -Wno-profile-instr-out-of-date silences.
# windows-arm64 with PGO is built outside make (clang from llvm-mingw, trained on Windows on ARM); here `make release` builds
# windows-arm64 with zig, without PGO and with the same nodes. windows-x32 (`make release-x32`) is released as built here, without PGO.
PROFDATA ?= xcrun llvm-profdata
PGO      := pgo_training
MACARM   := -arch arm64 $(CFLAGS) -mcpu=apple-m1 -mmacosx-version-min=11.0
LLVM_BIN ?= /opt/homebrew/opt/llvm@22/bin
SDK       = $(shell xcrun --show-sdk-path 2>/dev/null)
TRAIN_A   = -arch arm64 -isysroot $(SDK) $(CFLAGS) -mcpu=apple-m1 -mmacosx-version-min=11.0
TRAIN_X   = -arch x86_64 -isysroot $(SDK) $(CFLAGS) -march=x86-64 -mmacosx-version-min=10.13 -Wl,-segalign,0x4000
ZPGO     := -DBUILD_PGO=1 -Wno-profile-instr-out-of-date
# File variant in the build banner (`main.c`, BUILD_FLAGS), e.g. macos-arm64-pgo; only a string, search and evaluation do not read it
VARIANT   = '-DBUILD_FLAGS="$(1)$(if $(filter 1,$(LIGHT)), light)"'

release:
	mkdir -p dist
	rm -rf $(PGO) && mkdir -p $(PGO)/mac $(PGO)/arm64 $(PGO)/x64
	if [ "$(LIGHT)" = 1 ] && [ -f $(NNUE_FILE) ]; then for t in mac arm64 x64; do cp $(NNUE_FILE) $(PGO)/$$t/; done; fi   # light: train on the network if it lies here
	$(CC) $(MACARM) -fprofile-instr-generate=$(PGO)/mac/%p.profraw -o $(PGO)/mac/chessenger $(ENGINE) $(LDLIBS)
	$(LLVM_BIN)/clang $(TRAIN_A) -fprofile-instr-generate=$(PGO)/arm64/%p.profraw -o $(PGO)/arm64/chessenger $(ENGINE) $(LDLIBS)
	$(LLVM_BIN)/clang $(TRAIN_X) -fprofile-instr-generate=$(PGO)/x64/%p.profraw -o $(PGO)/x64/chessenger $(ENGINE) $(LDLIBS)
	for t in mac arm64 x64; do ./$(PGO)/$$t/chessenger bench < /dev/null > $(PGO)/$$t/bench.log 2>&1 || exit 1; done
	$(PROFDATA) merge -o $(PGO)/mac.profdata $(PGO)/mac/*.profraw
	$(LLVM_BIN)/llvm-profdata merge -o $(PGO)/arm64.profdata $(PGO)/arm64/*.profraw
	$(LLVM_BIN)/llvm-profdata merge -o $(PGO)/x64.profdata $(PGO)/x64/*.profraw
	$(CC) $(MACARM) -fprofile-instr-use=$(PGO)/mac.profdata -DBUILD_PGO=1 $(call VARIANT,macos-arm64-pgo) -o $(D)-macos-arm64-pgo $(ENGINE) $(LDLIBS)
	$(CC) -arch x86_64 $(CFLAGS) -march=x86-64-v2 -mavx2 -mmacosx-version-min=10.9 $(call VARIANT,macos-x64-avx2) -o $(D)-macos-x64-avx2 $(ENGINE) $(LDLIBS)
	$(ZIG) cc -target aarch64-linux-musl  -mcpu=generic        $(CFLAGS) $(LINUX) -fprofile-instr-use=$(PGO)/arm64.profdata $(ZPGO) $(call VARIANT,linux-arm64-pgo) -o $(D)-linux-arm64-pgo    $(ENGINE) $(LDLIBS)
	$(ZIG) cc -target x86_64-linux-musl   -mcpu=x86_64_v2+avx2 $(CFLAGS) $(LINUX) -fprofile-instr-use=$(PGO)/x64.profdata   $(ZPGO) $(call VARIANT,linux-x64-avx2-pgo) -o $(D)-linux-x64-avx2-pgo $(ENGINE) $(LDLIBS)
	$(ZIG) cc -target x86_64-windows-gnu  -mcpu=x86_64_v2+avx2 $(WCFLAGS) $(call VARIANT,windows-x64-avx2) -o $(D)-windows-x64-avx2.exe $(ENGINE) $(WIN) $(LDLIBS)
	$(ZIG) cc -target x86_64-windows-gnu  -mcpu=x86_64_v3      $(WCFLAGS) $(call VARIANT,windows-x64-bmi2) -o $(D)-windows-x64-bmi2.exe $(ENGINE) $(WIN) $(LDLIBS)
	$(ZIG) cc -target aarch64-windows-gnu -mcpu=generic        $(WCFLAGS) $(call VARIANT,windows-arm64) -o $(D)-windows-arm64.exe     $(ENGINE) $(WIN) $(LDLIBS)
	rm -rf $(PGO)

# 32-bit Windows, built separately
release-x32:
	mkdir -p dist
	$(ZIG) cc -target x86-windows-gnu -mcpu=pentium4 $(WCFLAGS) $(call VARIANT,windows-x32) -o $(D)-windows-x32.exe $(ENGINE) $(WIN) $(LDLIBS)

clean:
	rm -rf $(EXE) dist $(PGO)

.PHONY: release release-x32 clean
