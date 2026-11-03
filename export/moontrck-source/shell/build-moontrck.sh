#!/bin/bash
#
# Build the PortMaster (aarch64) package for MoonTRCK: build/portmaster/moontrck.zip
#
# Run from the repo root, on a box with docker:
#
#     docker build -t pockettracker-build -f shell/Dockerfile.portmaster shell/
#     docker run --rm -v "$PWD:/src" pockettracker-build bash /src/shell/build-moontrck.sh
#
set -euo pipefail

SRC=${SRC:-/src}

# THE SDL2 WE BUILD IS A LINK-TIME SDK AND IS NEVER SHIPPED. The port runs against the DEVICE's
# libSDL2 — the copy its CFW patched for that hardware's display (KMSDRM) and audio (ALSA).
SDL2_TAG=${SDL2_TAG:-release-2.0.18}

# Every Tier-1 CFW is at or above Ubuntu 20.04's glibc. Asserted on the artifact below.
GLIBC_MAX_ALLOWED=${GLIBC_MAX_ALLOWED:-2.31}

SYSROOT=/usr/aarch64-linux-gnu
SDL2_SRC=/tmp/sdl2-src
SDL2_BUILD=/tmp/sdl2-build
BUILD=$SRC/build/aarch64-moontrck
OUT=$SRC/build/portmaster-moontrck
STAGE=$OUT/stage
ZIPROOT=$OUT/ziproot
BIN=$STAGE/MoonTRCK.aarch64

cd "$SRC"
git config --global --add safe.directory '*'

echo
echo "############ 1/5  cross-build SDL2 $SDL2_TAG (link SDK only, NOT shipped) ############"
rm -rf "$SDL2_SRC" "$SDL2_BUILD"
git clone --depth 1 --branch "$SDL2_TAG" https://github.com/libsdl-org/SDL.git "$SDL2_SRC"
cmake -S "$SDL2_SRC" -B "$SDL2_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
    -DSDL_SHARED=ON \
    -DSDL_STATIC=OFF \
    -DSDL_TEST=OFF
cmake --build "$SDL2_BUILD"
cmake --install "$SDL2_BUILD"

echo
echo "############ 2/5  cross-build MoonTRCK ############"
# ⚠️ Sur un bind mount Windows, rm -rf échoue parfois en "Directory not empty" et laisse un arbre à
# moitié supprimé : compiler dessus produit des erreurs absurdes (config.h / *.o.d manquants). On
# s'arrête net plutôt que de construire un binaire faux. shell/build-moontrck.ps1 purge avant Docker.
rm -rf "$BUILD" 2>/dev/null || true
if [ -e "$BUILD" ]; then
    echo "FAIL: $BUILD n'a pas pu être supprimé (bind mount Windows)." >&2
    echo "      Lance shell/build-moontrck.ps1, ou supprime-le depuis Windows avant de relancer." >&2
    exit 1
fi
cmake -S "$SRC/shell" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$SRC/shell/toolchain-aarch64.cmake" \
    -DPT_HANDHELD=ON \
    -DSDL2_DIR="$SYSROOT/lib/cmake/SDL2"
cmake --build "$BUILD"

echo
echo "############ 3/5  stage the package ############"
rm -rf "$OUT" 2>/dev/null || true
if [ -e "$OUT" ]; then
    echo "FAIL: $OUT n'a pas pu être supprimé (bind mount Windows)." >&2
    exit 1
fi
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
ls -1 "$STAGE/moontrck/licenses/"

echo
echo "############ 4/5  verify the ARTIFACT ############"
file "$BIN"

GLIBC_MAX=$(readelf -V "$BIN" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/GLIBC_//' | sort -V | tail -1)
echo "max GLIBC required : $GLIBC_MAX   (must be <= $GLIBC_MAX_ALLOWED)"
if [ "$(printf '%s\n%s\n' "$GLIBC_MAX" "$GLIBC_MAX_ALLOWED" | sort -V | tail -1)" != "$GLIBC_MAX_ALLOWED" ]; then
    echo "FAIL: demands glibc $GLIBC_MAX, newer than the CFW floor $GLIBC_MAX_ALLOWED."
    exit 1
fi

echo
echo "dynamic deps:"
readelf -d "$BIN" | grep NEEDED
if ! readelf -d "$BIN" | grep -q 'libSDL2-2.0.so.0'; then
    echo "FAIL: not dynamically linked against libSDL2 - SDL2 got statically bundled."
    exit 1
fi
if [ -d "$STAGE/moontrck/libs.aarch64" ]; then
    echo "FAIL: libs.aarch64 is present - a shipped libSDL2 would shadow the device's."
    exit 1
fi

if sed 's/#.*//' "$STAGE/MoonTRCK.sh" | grep -qi 'gptokeyb'; then
    echo "FAIL: MoonTRCK.sh runs gptokeyb."
    exit 1
fi
echo "gptokeyb           : not invoked by the launch script (correct)"
if strings "$BIN" | grep -q 'KILDBG'; then
    echo "FAIL: built from the KIL investigation branch."
    exit 1
fi
echo "debug instr.       : none"
echo "SDL2 link floor    : $SDL2_TAG"

NOTICES="$STAGE/moontrck/licenses/THIRD-PARTY-NOTICES.md"
echo
echo "licence notices:"
MISSING=""
VENDORED=""
for D in "$SRC"/native/vendor/*/; do VENDORED="$VENDORED $(basename "$D")"; done
for COMPONENT in $VENDORED kissfft daisysp soundpipe; do
    if grep -qi -- "$COMPONENT" "$NOTICES"; then
        echo "  ok      $COMPONENT"
    else
        echo "  MISSING $COMPONENT"
        MISSING="$MISSING $COMPONENT"
    fi
done
if [ -n "$MISSING" ]; then
    echo "FAIL: statically linked but not named in THIRD-PARTY-NOTICES.md:$MISSING"
    exit 1
fi

echo
echo "############ 5/5  zip (upstream's transform) ############"
rm -rf "$ZIPROOT"
mkdir -p "$ZIPROOT"
cp -a "$STAGE/moontrck"  "$ZIPROOT/"
cp    "$STAGE/MoonTRCK.sh" "$ZIPROOT/"
cp    "$STAGE/MoonTRCK.aarch64" "$ZIPROOT/"
cp    "$STAGE/port.json" "$STAGE/gameinfo.xml" "$STAGE/screenshot.png" "$ZIPROOT/moontrck/"
cp    "$STAGE/README.md"        "$ZIPROOT/moontrck/moontrck.md"
( cd "$ZIPROOT" && zip -r "$OUT/moontrck.zip" . -x '.*' )
echo
ls -lh "$OUT/moontrck.zip"
unzip -l "$OUT/moontrck.zip"

SCRIPT_AT_ROOT=$(unzip -l "$OUT/moontrck.zip" | grep -c ' MoonTRCK\.sh$') || SCRIPT_AT_ROOT=0
echo "launch script at zip root : $SCRIPT_AT_ROOT   (want 1)"
if [ "$SCRIPT_AT_ROOT" != "1" ]; then
    echo "FAIL: MoonTRCK.sh is not at the zip root."
    exit 1
fi

GAMEINFO_IMG=$(sed -n 's:.*\./\(.*\)</image>.*:\1:p' "$STAGE/gameinfo.xml")
IMG_BYTES=$(unzip -p "$OUT/moontrck.zip" "$GAMEINFO_IMG" | wc -c) || IMG_BYTES=0
echo "gameinfo           : $GAMEINFO_IMG -> $IMG_BYTES bytes in the zip"
if [ "$IMG_BYTES" -lt 100 ]; then
    echo "FAIL: gameinfo.xml points at $GAMEINFO_IMG, which is not in the zip (or is empty)."
    exit 1
fi

echo
echo "licences, read back out of the zip:"
for L in LICENSE THIRD-PARTY-NOTICES.md CREDITS.md libogg-COPYING libopus-COPYING \
         libopus-LICENSE_PLEASE_READ.txt OFL-1.1-LiberationSans.txt OFL-1.1-QuinqueFive.txt; do
    BYTES=$(unzip -p "$OUT/moontrck.zip" "moontrck/licenses/$L" | wc -c) || BYTES=0
    echo "  $BYTES bytes   $L"
    if [ "$BYTES" -lt 100 ]; then
        echo "FAIL: moontrck/licenses/$L is missing or empty inside the zip."
        exit 1
    fi
done

echo
echo "OK: $OUT/moontrck.zip"
