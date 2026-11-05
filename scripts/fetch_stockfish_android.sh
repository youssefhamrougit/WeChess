#!/usr/bin/env bash
# Build Stockfish for Android and package it as an executable native library.
#
# Stockfish is GPL-3.0. Distributing an APK that contains it makes the whole
# combined work subject to the GPL: you must ship (or offer) the corresponding
# source, including WeChess's own source. See THIRD_PARTY_NOTICES.md.
#
# Requirements: Android NDK (r27+), git, make, and network access.
#   export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.0.12077973
#   ./scripts/fetch_stockfish_android.sh [arm64-v8a|x86_64]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ABI="${1:-arm64-v8a}"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
[ -n "$NDK" ] || { echo "Set ANDROID_NDK_HOME to your NDK directory."; exit 1; }

case "$ABI" in
  arm64-v8a) ARCH=arm64; TARGET=aarch64-linux-android; API=26 ;;
  x86_64)    ARCH=x64;   TARGET=x86_64-linux-android;  API=26 ;;
  *) echo "Unsupported ABI: $ABI (use arm64-v8a or x86_64)"; exit 2 ;;
esac

WORK="$ROOT/build/stockfish-android/$ABI"
mkdir -p "$WORK"
if [ ! -d "$WORK/src" ]; then
  git clone --depth 1 https://github.com/official-stockfish/Stockfish.git "$WORK/src"
fi

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
[ -d "$TOOLCHAIN" ] || TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
CC="$TOOLCHAIN/bin/${TARGET}${API}-clang++"
[ -x "$CC" ] || { echo "Compiler not found: $CC"; exit 1; }

cd "$WORK/src/src"
make -j"$(nproc)" build ARCH="$ARCH" COMP=gcc \
  CC="$CC" CXX="$CC" \
  EXTRALDFLAGS="-static-libstdc++" \
  net

BIN="$WORK/src/src/stockfish"
[ -x "$BIN" ] || BIN="$(ls "$WORK"/src/src/stockfish* | head -n1)"

DEST="$ROOT/android/app/src/main/jniLibs/$ABI"
mkdir -p "$DEST"
cp "$BIN" "$DEST/libstockfish.so"
chmod 755 "$DEST/libstockfish.so"

"$TOOLCHAIN/bin/llvm-strip" "$DEST/libstockfish.so" 2>/dev/null || true

echo "Installed $DEST/libstockfish.so"
echo "Verify the NNUE net is embedded (a bare binary cannot search):"
echo "  strings \"$DEST/libstockfish.so\" | grep -c 'nn-' || true"
