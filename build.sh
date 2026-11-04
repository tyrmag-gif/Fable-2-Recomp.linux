#!/usr/bin/env bash
# Build the Fable 2 ReXGlue project on Linux (port of build.cmd).
#
# Toolchain: clang (20+) + lld + Ninja + CMake (3.25+) + Python 3.
# On Arch Linux the build dependencies are listed by `./build.sh deps`.
#
# Usage:
#   ./build.sh                  build fable_2_codegen (runs codegen from fable_2_manifest.toml)
#   ./build.sh fable_2          build the full recompiled game
#   ./build.sh -release [t]     Release (-O3) instead of Debug   (short: -r)
#   ./build.sh -clean [t]       wipe the build dir of the config (short: -c)
#   ./build.sh x360extract      build the ISO extractor (needs the .NET 8 SDK)
#   ./build.sh deps             print the Arch Linux packages to install
#   ./build.sh <other target>   build any other CMake target
#
# Before building fable_2 you need your own game files in the repo root:
#   tools/linux/extract_iso.sh /path/to/Fable2.iso
# which puts default.xex and data/ here. The build never copies game content;
# run the game with tools/linux/fable2.sh from the build dir (see docs/LINUX.md).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
ROOT="$PWD"

SDK_VER="0.10.0"
SDK_ZIP="rexglue-sdk-${SDK_VER}-linux-amd64.zip"
SDK_SHA256="0388edb3f8fb1444f1b0ad5d440cb02d9690ac20a76b04fd6b7dcc19f59328a6"
SDK_URL="https://github.com/rexglue/rexglue-sdk/releases/download/v${SDK_VER}/${SDK_ZIP}"
OFFICIAL="$ROOT/out/tooling/rexglue-sdk-${SDK_VER}/linux-amd64"
SDKSRC="$ROOT/thirdparty/rexglue-sdk"

ARCH_PACKAGES=(base-devel clang lld cmake ninja python git curl unzip autoconf
  gtk3 libx11 libxss libxkbcommon wayland wayland-protocols libdecor
  vulkan-icd-loader vulkan-headers alsa-lib libpulse pipewire)

die() { echo "error: $*" >&2; exit 1; }

case "${1:-}" in
  deps)
    echo "Arch Linux build dependencies:"
    echo "  sudo pacman -S --needed ${ARCH_PACKAGES[*]}"
    echo "Plus a Vulkan driver for your GPU, e.g. vulkan-radeon (AMD),"
    echo "vulkan-intel (Intel) or nvidia-utils (NVIDIA)."
    echo "Optional, for the ISO extractor: sudo pacman -S dotnet-sdk-8.0"
    exit 0 ;;
  x360extract)
    exec "$ROOT/tools/x360extract/build.sh" "${@:2}" ;;
esac

# --- Toolchain ---------------------------------------------------------------
# Prefer a versioned clang (Ubuntu/Debian style) when present, else plain clang
# (Arch, Fedora). Override with CC=/CXX= in the environment.
pick() { for c in "$@"; do command -v "$c" >/dev/null && { echo "$c"; return; }; done; }
CC="${CC:-$(pick clang-21 clang-20 clang)}"
CXX="${CXX:-$(pick clang++-21 clang++-20 clang++)}"
[[ -n "$CC" && -n "$CXX" ]] || die "clang not found (Arch: sudo pacman -S clang lld)"
clang_major="$("$CXX" -dumpversion | cut -d. -f1)"
(( clang_major >= 20 )) || die "clang $clang_major is too old; the SDK needs clang 20 or newer"
for tool in cmake ninja python3 git curl unzip; do
  command -v "$tool" >/dev/null || die "$tool not found (run ./build.sh deps)"
done

# --- Arguments ---------------------------------------------------------------
CONFIG="linux-amd64-debug"
TARGET=""
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    -release|-r) CONFIG="linux-amd64-release" ;;
    -clean|-c) CLEAN=1 ;;
    *) [[ -z "$TARGET" ]] && TARGET="$arg" ;;
  esac
done
TARGET="${TARGET:-fable_2_codegen}"
BUILD_DIR="$ROOT/out/build/$CONFIG"

if (( CLEAN )); then
  if [[ -d "$BUILD_DIR" ]]; then rm -rf "$BUILD_DIR"; echo "Cleaned $BUILD_DIR";
  else echo "Nothing to clean: $BUILD_DIR does not exist"; fi
  exit 0
fi

if [[ "$CONFIG" == "linux-amd64-debug" ]]; then
  RUNTIMECONFIG="Debug"; RUNTIMEDIR="runtime-sdk-debug"
else
  RUNTIMECONFIG="Release"; RUNTIMEDIR="runtime-sdk"
fi
RUNTIME="$ROOT/out/tooling/$RUNTIMEDIR/linux-amd64"
RUNTIME_BUILD="$ROOT/out/build/$RUNTIMEDIR-linux"

# --- Official codegen SDK (same 0.10.0 release build.cmd uses on Windows) ----
if [[ ! -x "$OFFICIAL/bin/rexglue" ]]; then
  echo "Downloading the official ReXGlue SDK $SDK_VER (codegen tool) ..."
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl -L --fail -sS -o "$tmp/$SDK_ZIP" "$SDK_URL"
  echo "$SDK_SHA256  $tmp/$SDK_ZIP" | sha256sum -c --quiet - \
    || die "SDK download failed its SHA-256 check"
  mkdir -p "$(dirname "$OFFICIAL")"
  unzip -q -o "$tmp/$SDK_ZIP" -d "$(dirname "$OFFICIAL")"
  [[ -x "$OFFICIAL/bin/rexglue" ]] || die "SDK incomplete after extract: $OFFICIAL"
fi

# --- Bootstrap the CMake integration on a fresh clone -------------------------
if [[ ! -f generated/rexglue.cmake ]]; then
  "$OFFICIAL/bin/rexglue" codegen fable_2_manifest.toml
fi

# --- Matched runtime SDK, built from the pinned fork submodule -----------------
if [[ ! -f "$SDKSRC/CMakeLists.txt" ]]; then
  git submodule update --init thirdparty/rexglue-sdk
fi
python3 tools/prepare_runtime_sdk.py "$SDKSRC"
if [[ ! -f "$RUNTIME/lib/cmake/rexglue/rexglueConfig.cmake" || -n "${FABLE2_REBUILD_SDK:-}" ]]; then
  echo "Building the ReXGlue runtime SDK ($RUNTIMECONFIG, Vulkan) ..."
  cmake -S "$SDKSRC" -B "$RUNTIME_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$RUNTIMECONFIG" \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="-march=x86-64-v2" -DCMAKE_CXX_FLAGS="-march=x86-64-v2" \
    -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_TRACY=OFF \
    -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX="$RUNTIME"
  cmake --build "$RUNTIME_BUILD"
  cmake --install "$RUNTIME_BUILD"
elif [[ -f "$RUNTIME_BUILD/build.ninja" ]]; then
  # Incremental: rebuild + reinstall only what changed in the SDK source.
  cmake --build "$RUNTIME_BUILD" && cmake --install "$RUNTIME_BUILD" >/dev/null
fi

# --- Configure + build the game -----------------------------------------------
cmake --preset "$CONFIG" \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_PREFIX_PATH="$RUNTIME" -Drexglue_DIR="$RUNTIME/lib/cmake/rexglue" \
  -DREXGLUE_SDK_ROOT="$RUNTIME" -DREXGLUE_SDK_SOURCE="$SDKSRC" \
  -DFABLE2_CODEGEN_TOOL="$OFFICIAL/bin/rexglue"

# CMake can only list the generated sources once codegen has written them.
if [[ ! -f generated/default/sources.cmake ]]; then
  [[ -f default.xex ]] || die "default.xex not found in $ROOT.
       Extract your own disc first: tools/linux/extract_iso.sh /path/to/Fable2.iso"
  cmake --build "$BUILD_DIR" --target fable_2_codegen
  cmake "$BUILD_DIR"
fi
cmake --build "$BUILD_DIR" --target "$TARGET"

if [[ "$TARGET" == "fable_2" ]]; then
  echo
  echo "Built: $BUILD_DIR/fable_2"
  echo "Run it with: tools/linux/stage_content.sh \"$BUILD_DIR\" && \"$BUILD_DIR/fable2.sh\""
fi
