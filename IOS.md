# Chessenger 4.2 on iPhone and iPad

Chessenger runs on iPhone and iPad in a-Shell, a free terminal app, as a WebAssembly file, with the Chessenger console: board, play, analyze, hint and UCI.
You need one file from the release page: `chessenger-4.2-ios.wasm`.

## Setup

- Install a-Shell from the App Store (free, by Nicolas Holzschuch) and open it.
- Save `chessenger-4.2-ios.wasm` in the Files app.
- In a-Shell type these lines, one at a time, each followed by Return (a-Shell does not split a line at `&&`):
  ```
  pickFolder
  mkdir -p ~/Documents/bin
  cp chessenger-4.2-ios.wasm ~/Documents/bin/chessenger.wasm
  cd ~/Documents
  chessenger console
  ```
  `pickFolder` opens a file picker: choose the folder that holds the file and tap Open.

Next time, open a-Shell and type `chessenger console`. Play with `play white 5+3` and moves such as `e4`; `analyze` analyses until you type `stop`; `help` lists all commands, `quit` leaves.

## If something does not work

- If `chessenger console` does not start, type `wasm ~/Documents/bin/chessenger.wasm console`.
- If that fails too, use `chessenger-4.2-ios-nosimd.wasm` (about 2 times slower) in place of the first file in the setup above.
- No line editing with the arrow keys. Type `bench` inside the console: the line `chessenger bench` in a-Shell shows nothing.
