#!/bin/zsh
# Builds the Chessenger Open Exchange (OEX) engine package for Android chess apps.
# Usage: zsh android/oex/buduj_apk.zsh BINARY OUT.apk
#   BINARY        the static ARM64 Android binary, chessenger-VER-android-arm64
#   ANDROID_HOME  Android SDK with platforms/android-36 and build-tools/36.1.0; JDK 17 (javac) in PATH
#   KEYSTORE      optional: PKCS12 key store to sign with; its password in the variable KS_PASS
# Without KEYSTORE only OUT.unsigned.apk is built. The same BINARY gives the same OUT.unsigned.apk
# (fixed dates and order in the zip). LICENSE, THIRD-PARTY.md and LICENSES/ come from the top of this source tree.
set -euo pipefail
(( $# == 2 )) || { print -u2 "usage: $0 BINARY OUT.apk"; exit 2; }
BIN=${1:A}; OUT=${2:A}
HERE=${0:A:h}; TOP=${HERE:h:h}
[[ -n ${ANDROID_HOME:-} ]] || { print -u2 "set ANDROID_HOME to the Android SDK"; exit 1; }
BT=$ANDROID_HOME/build-tools/36.1.0; AJ=$ANDROID_HOME/platforms/android-36/android.jar
[[ -f $AJ && -x $BT/aapt2 ]] || { print -u2 "need platforms/android-36 and build-tools/36.1.0 in $ANDROID_HOME"; exit 1; }
[[ -f $BIN ]] || { print -u2 "no binary $BIN"; exit 1; }
file -b $BIN | grep -q "ARM aarch64" || { print -u2 "the binary is not ARM64"; exit 1; }
W=$(mktemp -d "${TMPDIR:-/tmp}/build_apk.XXXXXX")
mkdir -p $W/res $W/cls $W/dex $W/stage/lib/arm64-v8a $W/stage/assets/LICENSES
# 1. resources and manifest
$BT/aapt2 compile --dir $HERE/src/res -o $W/res/res.zip
$BT/aapt2 link -o $W/base.apk -I $AJ --manifest $HERE/src/AndroidManifest.xml $W/res/res.zip
# 2. Java code to dex
javac -source 8 -target 8 -Xlint:-options -bootclasspath $AJ -encoding UTF-8 -d $W/cls $HERE/src/java/eu/chessengeria/chessenger/*.java
$BT/d8 --release --min-api 21 --lib $AJ --output $W/dex $W/cls/eu/chessengeria/chessenger/*.class
cp $W/dex/classes.dex $W/stage/classes.dex
# 3. the engine as a native library, and the licenses
cp $BIN $W/stage/lib/arm64-v8a/libchessenger.so
cp $TOP/LICENSE $W/stage/assets/COPYING
cp $TOP/THIRD-PARTY.md $W/stage/assets/THIRD-PARTY.md
cp $TOP/LICENSES/*.txt $W/stage/assets/LICENSES/
# 4. deterministic zip, aligned (16 KB pages for the native library)
python3 -I $HERE/zip_det.py $W/raw.apk $W/base.apk $W/stage
$BT/zipalign -f -P 16 4 $W/raw.apk ${OUT%.apk}.unsigned.apk
$BT/zipalign -c -P 16 4 ${OUT%.apk}.unsigned.apk
print "unsigned sha256 $(shasum -a 256 ${OUT%.apk}.unsigned.apk | cut -d" " -f1)"
# 5. signature: v1 (apksigner adds it for minSdk 21), v2 and v3; the password goes only through the variable
[[ -n ${KEYSTORE:-} ]] || { print "no KEYSTORE: unsigned APK only"; exit 0; }
[[ -n ${KS_PASS:-} ]] || { print -u2 "set KS_PASS to the password of $KEYSTORE"; exit 1; }
$BT/apksigner sign --ks $KEYSTORE --ks-pass env:KS_PASS --v2-signing-enabled true --v3-signing-enabled true \
  --out $OUT ${OUT%.apk}.unsigned.apk
$BT/apksigner verify -v --print-certs $OUT | grep -E "Verified using|Signer #1 certificate (DN|SHA-256)"
print "apk sha256 $(shasum -a 256 $OUT | cut -d' ' -f1)"
