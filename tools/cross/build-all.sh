#!/usr/bin/env bash
# IW4Iris — cross-compile everything for Windows on Linux and package it.
#
# usage: tools/cross/build-all.sh [work-dir]
#
# Needs the toolchain described in BUILD.md (MinGW-w64 GCC >= 13 with
# mingw-w64 >= 13 headers, premake5 >= 5.0.0-beta8, uasm, cmake, python3).
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
work="${1:-$repo/build/iw4x-cinematic}"
commit="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["iw4x"]["commit"])' "$repo/compatibility/manifest.json")"

git -C "$repo" submodule update --init --recursive
if [ ! -d "$work/.git" ]; then
  git clone --no-checkout "$repo/external/iw4x-client" "$work"
fi
git -C "$work" checkout --force "$commit"
git -C "$work" remote set-url origin https://github.com/iw4x/iw4x-client.git
git -C "$work" submodule update --init --recursive --depth 1

"$repo/tools/iw4x-overlay.sh" "$work"
"$repo/tools/cross/build-iw4x.sh" "$work" release

cmake -S "$repo" -B "$repo/build/win32" -DCMAKE_TOOLCHAIN_FILE="$repo/tools/cross/mingw32.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DIW4IRIS_BUILD_LAUNCHER=ON >/dev/null
cmake --build "$repo/build/win32" --parallel
"$repo/tools/package.sh" "$work/build/mingw/bin/Win32/Release/iw4x.dll" "$repo/build/win32/launcher/IW4Iris Launcher.exe"
