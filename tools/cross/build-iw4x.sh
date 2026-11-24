#!/usr/bin/env bash
# IW4Iris — cross-compile an IW4x client tree (with or without the IW4Iris
# overlay applied) on Linux with MinGW-w64.
#
# usage: tools/cross/build-iw4x.sh <iw4x-source-dir> [release|debug] [jobs]
#
# Requirements (see BUILD.md): premake5 >= 5.0.0-beta8, uasm, GNU make,
# i686-w64-mingw32-g++ (POSIX threads, C++20), mingw-w64 >= 13 headers,
# a host protoc 3.20.x (built automatically from deps/protobuf if missing).
set -euo pipefail

src="$(cd "${1:?usage: $0 <iw4x-source-dir> [release|debug] [jobs]}" && pwd)"
cfg="${2:-release}"
jobs="${3:-$(nproc)}"
here="$(cd "$(dirname "$0")" && pwd)"
compat="$here/compat"

for tool in premake5 uasm make i686-w64-mingw32-g++; do
  command -v "$tool" >/dev/null || { echo "error: $tool not found on PATH" >&2; exit 1; }
done

cd "$src"
git submodule update --init --recursive --depth 1

protoc="$src/build/host-protobuf/protoc"
if [ ! -x "$protoc" ]; then
  echo "info: building host protoc"
  cmake -S deps/protobuf/cmake -B build/host-protobuf -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build/host-protobuf --target protoc --parallel "$jobs" >/dev/null
fi

premake5 --mingw --no-buildinfo gmake
premake5 --mingw generate-buildinfo

cppflags=""
force_include="-include STDInclude.hpp"
gccmajor="$(i686-w64-mingw32-g++ -dumpversion | sed "s/[^0-9].*//")"
if [ "$gccmajor" -lt 14 ]; then
  cppflags="-include $compat/iw4iris_compat.h"
  # GCC 12/13 report well-known false positives from their middle end:
  # -Warray-bounds on the TEB reads in mingw-w64's intrinsics
  # (__readfsdword, "source object is likely at address zero") and
  # -Wmaybe-uninitialized / -Wstringop-* / -Wrestrict inside libstdc++'s
  # std::string and std::variant. GCC 14 no longer reports them. The flags
  # have to come after the project's -Wall, which FORCE_INCLUDE does (it is
  # passed last on the compiler command line).
  force_include="-include STDInclude.hpp -Wno-array-bounds -Wno-maybe-uninitialized -Wno-stringop-overflow -Wno-stringop-overread -Wno-restrict -Wno-dangling-pointer"
  # GCC < 14 warns "#pragma once in main file" when compiling the
  # precompiled header itself, which -Werror turns into an error. Build the
  # PCH up front with that one diagnostic not fatal; everything else is
  # still compiled with the project's -Werror.
  objdir="build/mingw/obj/Win32/${cfg^}/iw4x"
  mkdir -p "$objdir"
  cmd="$(make -s -n -C build/mingw -f iw4x.make config="${cfg}_win32" CPPFLAGS="$cppflags" \
          "obj/Win32/${cfg^}/iw4x/STDInclude.hpp.gch" | grep -- '-x c++-header' | head -1 || true)"
  if [ -n "$cmd" ]; then
    (cd build/mingw && eval "${cmd/-Werror/-Werror -Wno-error}") 2>&1 | grep -v 'pragma once' || true
    touch "$objdir/STDInclude.hpp"
  fi
fi

# Build the third-party projects first with their own force-includes, then
# the client with ours (FORCE_INCLUDE on the command line reaches every
# sub-make, so it may only be overridden once the dependencies are built).
deps="$(sed -n 's/^PROJECTS := //p' build/mingw/Makefile | tr ' ' '\n' | grep -vx iw4x | tr '\n' ' ')"
# shellcheck disable=SC2086
make -C build/mingw -j"$jobs" config="${cfg}_win32" PROTOC="$protoc" CPPFLAGS="$cppflags" $deps
make -C build/mingw -j"$jobs" config="${cfg}_win32" PROTOC="$protoc" CPPFLAGS="$cppflags" \
  FORCE_INCLUDE="$force_include" iw4x
echo "info: built $(ls build/mingw/bin/Win32/${cfg^}/iw4x.dll)"
