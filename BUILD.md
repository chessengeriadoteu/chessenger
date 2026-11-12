# Build Chessenger 4.2 from source

This page belongs to the [README](README.md) of Chessenger 4.2.

Chessenger has been adapted to be built and used on the systems below; each link leads to its build steps.

- [macOS, Apple Silicon](#macos-apple-silicon)
- [macOS, Intel](#macos-intel)
- [Linux](#linux): x86-64 and ARM64
- [Windows, Command Prompt](#windows-command-prompt): x86-64, 32-bit x86 and ARM64
- [Windows, MSYS2](#windows-msys2): x86-64 and ARM64
- [FreeBSD](#freebsd): ARM64
- [OpenBSD](#openbsd): ARM64
- [Linux on RISC-V](#linux-on-risc-v): cross-compiled with zig, tested under QEMU (user mode)
- [Android](#android): ARM64, cross-compiled with the Android NDK
- [iPhone and iPad (WebAssembly)](#iphone-and-ipad-webassembly): cross-compiled with zig
- [All release binaries at once](#all-release-binaries-at-once)
- [Light binary, build without tablebases and neural network (NNUE)](#light-binary-build-without-tablebases-and-neural-network-nnue)

## Before you build

- Every standard build (not the light builds) puts the network file `net_g5_s1086.bin` (next to `nnue.c`) and the 70 tablebase files in `tablebases/` into the binary with the C23 `#embed` directive.
- So the compiler must know `#embed`: gcc 15 or newer, or a recent clang.
- A missing file or a network with a different md5 stops a `make` build.

## Check your build

Run `bench`. The last lines must show `Nodes searched  : 2976315`, the same nodes as every release binary.

```
./chessenger bench
```

On Windows:

```
chessenger.exe bench
```

The playing levels must show `total nodes 24620884`:

```
./chessenger bench levels
```

## macOS, Apple Silicon

**Needs**

- The Xcode Command Line Tools, which bring clang and make:

```
xcode-select --install
```

**Build**

```
make
./chessenger bench
```

## macOS, Intel

**Build** on an Intel Mac (`make` builds for Intel, `-march=x86-64-v2`):

```
make
./chessenger bench
```

**Build** an Intel binary on a Mac with Apple Silicon:

```
arch -x86_64 make CC="cc -arch x86_64"
```

## Linux

**Needs**

- make and gcc 15 or newer (the default gcc of Ubuntu 26.04 is 15.2).

**Build**

```
make
./chessenger bench
```

## Windows, Command Prompt

**Needs**

- llvm-mingw (clang for Windows), for example:

```
winget install MartinStorsjo.LLVM-MinGW.UCRT
```

**Build:** open Command Prompt in the source folder and run the two lines for your Windows.

64-bit Windows, for CPUs with AVX2 and BMI2 (`-march=x86-64-v3`):

```
clang -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 -D_WIN32_WINNT=0x0A00 --embed-dir=tablebases -march=x86-64-v3 -static -s -o chessenger.exe *.c fathom\tbprobe.c levels\levels.c -lm
chessenger.exe bench
```

32-bit Windows (`-march=pentium4`):

```
i686-w64-mingw32-clang -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 -D_WIN32_WINNT=0x0A00 --embed-dir=tablebases -march=pentium4 -static -s -o chessenger.exe *.c fathom\tbprobe.c levels\levels.c -lm
chessenger.exe bench
```

Windows on ARM (no `-march`):

```
aarch64-w64-mingw32-clang -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 -D_WIN32_WINNT=0x0A00 --embed-dir=tablebases -static -s -o chessenger.exe *.c fathom\tbprobe.c levels\levels.c -lm
chessenger.exe bench
```

**Notes**

- The Makefile checks the network file with POSIX shell tools (`md5sum`), which the Windows Command Prompt does not have, so here the compiler is called directly.
- In MSYS2 make works.

## Windows, MSYS2

**Needs**

- MSYS2 (https://www.msys2.org/) with make and clang.

**Build** in the MSYS2 CLANG64 shell (x86-64):

```
pacman -S make mingw-w64-clang-x86_64-clang
make
./chessenger.exe bench
```

**Build** in the MSYS2 CLANGARM64 shell (Windows on ARM), where `ARCH=` leaves out `-march=x86-64-v3`:

```
pacman -S make mingw-w64-clang-aarch64-clang
make ARCH=
./chessenger.exe bench
```

**Build a binary that also runs outside MSYS2**, for example in a GUI (the binaries above need `libwinpthread-1.dll` from MSYS2, so they run inside that shell).

x86-64:

```
make LDLIBS="-static -lm"
```

Windows on ARM:

```
make ARCH= LDLIBS="-static -lm"
```

## FreeBSD

**Needs**

- GNU make; the clang of the base system builds it:

```
pkg install gmake
```

**Build**

```
gmake
./chessenger bench
```

**Notes**

- The `make` of the base system does not read this Makefile.

## OpenBSD

**Needs**

- GNU make; the clang of the base system builds it:

```
pkg_add gmake
```

**Build**

```
gmake
./chessenger bench
```

## Linux on RISC-V

Cross-compiled with zig, tested under QEMU (user mode).

**Step 1:** build on a Mac with zig 0.17:

```
make CC="zig cc -target riscv64-linux-musl -D_XOPEN_SOURCE=700 -static" ARCH=""
```

**Step 2:** run on Linux with QEMU (package `qemu-user` on Ubuntu):

```
qemu-riscv64 ./chessenger bench
```

## Android

Cross-compiled on a Mac with the Android NDK r30, for Android 5.0 (API level 21) and newer, as a static binary.

**Build** in the source folder, with `NDK` set to the folder of the unpacked NDK:

```
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang --target=aarch64-linux-android21 -mcpu=generic -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 --embed-dir=tablebases -D_XOPEN_SOURCE=700 -static -s -o chessenger-4.2-android-arm64 search.c board.c console.c eval.c king_safety.c magic.c main.c movegen.c nnue.c pawn_structure.c san.c search_core.c smp.c syzygy.c syzygy_embedded.c tt.c utils.c xboard.c fathom/tbprobe.c levels/levels.c -lm
```

**Check** on the phone in Termux (`ANDROID.md`): `bench` must show 2976315 nodes. `android/oex/buduj_apk.zsh` packs this binary into the APK (usage in its first lines).

## iPhone and iPad (WebAssembly)

Cross-compiled with zig 0.17 for WASI (`wasm32-wasi`), one thread. The files in `wasm/` supply what WASI lacks: functions for signals and the terminal, and reading the keyboard during a search in a-Shell.

**Build** in the source folder. These lines build `chessenger-4.2-ios.wasm`; without `-msimd128` in the first line and with `-o chessenger-4.2-ios-nosimd.wasm` in the last line they build `chessenger-4.2-ios-nosimd.wasm`:

```
F=(-target wasm32-wasi -O3 -msimd128 -std=c99 -DNDEBUG -DSMP=1 -DTB_EMBEDDED=1 --embed-dir=tablebases -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_PROCESS_CLOCKS -include wasm/wasi_shim.h)
zig cc "${F[@]}" -include wasm/wasi_search.h -c search.c -o search.o
zig cc "${F[@]}" -s -Wl,-z,stack-size=8388608 -Wl,--max-memory=1073741824 -o chessenger-4.2-ios.wasm search.o board.c console.c eval.c king_safety.c magic.c main.c movegen.c nnue.c pawn_structure.c san.c search_core.c smp.c syzygy.c syzygy_embedded.c tt.c utils.c xboard.c fathom/tbprobe.c levels/levels.c wasm/wasi_stub.c -lm -lwasi-emulated-signal -lwasi-emulated-mman -lwasi-emulated-process-clocks
```

**Check** in a-Shell (`IOS.md`): `bench` inside the console must show 2976315 nodes.

## All release binaries at once

**Needs**

- A Mac with Apple Silicon, zig 0.17 and Homebrew LLVM 22 (`LLVM_BIN` points to it):

```
brew install zig llvm@22
```

**Build**

macOS, Linux and Windows binaries into `dist/` (the macOS and Linux ones with PGO, except `macos-x64-avx2`; the Windows one without). The released `windows-x64-avx2` and `windows-arm64-pgo` are built outside make, with llvm-mingw, so `make release` gives only `windows-arm64`, without PGO, as a Windows file:

```
make release
```

32-bit Windows binary into `dist/`:

```
make release-x32
```

## Light binary, build without tablebases and neural network (NNUE)

With make (macOS, Linux); run `make clean` first when switching between them.

Without the network and the tablebases inside:

```
make LIGHT=1
```

With the network inside; tablebases only from SyzygyPath:

```
make TB_EMBEDDED=0
```

Release binaries named with `-light`:

```
make release LIGHT=1
```

**How the light binary finds its files**

- The network `net_g5_s1086.bin`: from the UCI option EvalFile, from its own folder or from the current folder.
- The Syzygy tablebases: from the folder `tablebases/` next to it or from SyzygyPath.
- Without a network it plays on the classic evaluation and says so (`bench` then gives 4554810 nodes).
- If the path of the binary's folder contains the path list separator (`:` on macOS and Linux, `;` on Windows), the light binary does not find `tablebases/` next to it, so keep it in a folder without that character.
