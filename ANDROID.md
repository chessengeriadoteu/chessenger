# Chessenger 4.2 on Android

Chessenger runs on 64-bit Android phones and tablets (ARM64) in three ways: as an engine for Android chess apps from the APK, as a UCI engine in DroidFish from a single file, and in Termux, a free terminal app, with the Chessenger console: board, play, analyze, hint and UCI.
The release page has two files for Android: the APK `chessenger-4.2-android-arm64.apk` and the single file `chessenger-4.2-android-arm64`, a static binary, not an archive.

## Install the APK (recommended)

`chessenger-4.2-android-arm64.apk` installs Chessenger 4.2 as an Open Exchange (OEX) engine for Android chess apps. It has no chess board of its own.
Tested on Android 10 and Android 16 (64-bit ARM).

- In the Chess app (jwtc.android.chess): open Play, tap the menu button, Game Settings, and pick `OEX: Chessenger 4.2` under Engine.
- In DroidFish: tap the title bar, then Manage Chess Engines, Select Engine and `Chessenger 4.2`.

The engine inside the APK is the same file as `chessenger-4.2-android-arm64` (MD5 45b3b5c92eb50f9414b9db078b9ade5b).
The APK is signed with the Chessenger release key. Certificate SHA-256: 6e:f8:23:d9:04:62:0b:65:2d:5e:10:61:d0:7b:af:78:d9:42:c6:76:df:8c:f8:9e:96:27:6b:64:3c:b2:1d:72.
The APK contains the GPL-3.0 license. Its source files are in `android/oex/` in the source tree.

## In Termux

- Install Termux from F-Droid: https://f-droid.org/packages/com.termux/ (the Google Play version of Termux is experimental; we did not test it).
- Download `chessenger-4.2-android-arm64` to your phone.
- In Termux type these lines, one at a time, each followed by Enter:
  ```
  termux-setup-storage
  cp /sdcard/Download/chessenger-4.2-android-arm64 ~/chessenger
  chmod +x ~/chessenger
  ~/chessenger console
  ```
  `termux-setup-storage` asks for access to files once.

Next time, open Termux and type `~/chessenger console`. Type a move such as `e4` to play, `help` for all commands, `quit` to leave.

## In the DroidFish chess app

Chessenger also works as a UCI engine in DroidFish.

- Install DroidFish from F-Droid: https://f-droid.org/packages/org.petero.droidfish/
- In the Files app, move `chessenger-4.2-android-arm64` from Downloads to the folder `DroidFish/uci` in internal storage (hold the file, then More options, Move to…).
- In DroidFish tap the title bar, then `Manage Chess Engines`, `Select Engine` and `chessenger-4.2-android-arm64`. Allow access to files when DroidFish asks.

## If something does not work

- `Permission denied`: Android does not run a program straight from the Download folder. Copy it into Termux first, as in the Termux steps above, and start `~/chessenger`.
- Old 32-bit phones (armeabi-v7a only) are not supported in 4.2.
- Files does not show internal storage: choose More options, Show internal storage.
