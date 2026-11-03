#!/bin/bash
#
# Build MoonTRCK (aarch64) pour PortMaster — depuis WSL/Ubuntu.
# Lance ce fichier depuis la racine du repo: bash shell/build-moontrck-wsl.sh
#
set -euo pipefail

SRC="$PWD"
SYSROOT=/tmp/sdl2-sysroot
BUILD="$SRC/build/aarch64-moontrck"
OUT="$SRC/build/portmaster-moontrck"
STAGE="$OUT/stage"
ZIPROOT="$OUT/ziproot"
BIN="$STAGE/MoonTRCK.aarch64"

cd "$SRC"

command -v ninja >/dev/null 2>&1 || { echo "FAIL: ninja manquant. sudo apt install ninja-build"; exit 1; }
command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || { echo "FAIL: cross toolchain manquant. sudo apt install crossbuild-essential-arm64"; exit 1; }

SDL2_SRC=/tmp/sdl2-src
SDL2_BUILD=/tmp/sdl2-build

echo "############ 1/5  cross-build SDL2 (link SDK only, NOT shipped) ############"
rm -rf "$SDL2_SRC" "$SDL2_BUILD"
git clone --depth 1 --branch release-2.0.18 https://github.com/libsdl-org/SDL.git "$SDL2_SRC"
cmake -S "$SDL2_SRC" -B "$SDL2_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
    -DCMAKE_FIND_ROOT_PATH="$SYSROOT" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
    -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON \
    -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON \
    -DSDL_WAYLAND=OFF -DSDL_X11=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
    -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_ARTS=OFF -DSDL_ESD=OFF -DSDL_SNDIO=OFF -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=ON \
    -DSDL_VULKAN=OFF -DSDL_HIDAPI=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$SDL2_BUILD"
cmake --install "$SDL2_BUILD"

echo
echo "############ 2/5  cross-build MoonTRCK ############"
rm -rf "$BUILD"
cmake -S "$SRC/shell" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$SRC/shell/toolchain-aarch64.cmake" \
    -DPT_HANDHELD=ON \
    -DSDL2_DIR="$SYSROOT/lib/cmake/SDL2"
cmake --build "$BUILD"

echo
echo "############ 3/5  stage the package ############"
rm -rf "$OUT"
mkdir -p "$STAGE/moontrck/licenses"

cp "$SRC/shell/portmaster-moontrck/MoonTRCK.sh" "$STAGE/"
cp "$SRC/shell/portmaster-moontrck/port.json"        "$STAGE/"
cp "$SRC/shell/portmaster-moontrck/gameinfo.xml"     "$STAGE/"
cp "$SRC/shell/portmaster-moontrck/README.md"        "$STAGE/"
chmod +x "$STAGE/MoonTRCK.sh"

cp "$SRC/docs/images/screenshot.png" "$STAGE/screenshot.png"

cp "$BUILD/pockettracker-sdl" "$BIN"
chmod +x "$BIN"
aarch64-linux-gnu-strip "$BIN"

cp "$SRC/LICENSE"                                       "$STAGE/moontrck/licenses/LICENSE"
cp "$SRC/docs/licenses/THIRD-PARTY-NOTICES.md"          "$STAGE/moontrck/licenses/"
cp "$SRC/CREDITS.md"                                    "$STAGE/moontrck/licenses/CREDITS.md"
cp "$SRC/native/vendor/ogg/COPYING"                     "$STAGE/moontrck/licenses/libogg-COPYING"
cp "$SRC/native/vendor/opus/COPYING"                    "$STAGE/moontrck/licenses/libopus-COPYING"
cp "$SRC/native/vendor/opus/LICENSE_PLEASE_READ.txt"    "$STAGE/moontrck/licenses/libopus-LICENSE_PLEASE_READ.txt"
cp "$SRC/docs/licenses/OFL-1.1-LiberationSans.txt"      "$STAGE/moontrck/licenses/OFL-1.1-LiberationSans.txt"
cp "$SRC/docs/licenses/OFL-1.1-QuinqueFive.txt"         "$STAGE/moontrck/licenses/OFL-1.1-QuinqueFive.txt"

echo
echo "############ 4/5  verify the ARTIFACT ############"
file "$BIN"

GLIBC_MAX=$(readelf -V "$BIN" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/GLIBC_//' | sort -V | tail -1)
echo "max GLIBC required : $GLIBC_MAX   (must be <= 2.31)"
if [ "$(printf '%s\n%s\n' "$GLIBC_MAX" "2.31" | sort -V | tail -1)" != "2.31" ]; then
    echo "WARN: demands glibc $GLIBC_MAX, newer than the CFW floor 2.31 (may not start on Knulli without Docker/Ubuntu 20.04)."
fi

echo
echo "dynamic deps:"
readelf -d "$BIN" | grep NEEDED
if ! readelf -d "$BIN" | grep -q 'libSDL2-2.0.so.0'; then
    echo "FAIL: not dynamically linked against libSDL2."
    exit 1
fi

echo
echo "############ 5/5  zip ############"
rm -rf "$ZIPROOT"; mkdir -p "$ZIPROOT"
cp -a "$STAGE/moontrck" "$ZIPROOT/"
cp "$STAGE/MoonTRCK.sh" "$ZIPROOT/"
cp "$STAGE/MoonTRCK.aarch64" "$ZIPROOT/"
cp "$STAGE/port.json" "$STAGE/gameinfo.xml" "$STAGE/screenshot.png" "$ZIPROOT/moontrck/"
cp "$STAGE/README.md" "$ZIPROOT/moontrck/moontrck.md"
(cd "$ZIPROOT" && zip -r "$OUT/moontrck.zip" . -x '.*')

echo
ls -lh "$OUT/moontrck.zip"
echo "OK: $OUT/moontrck.zip"
