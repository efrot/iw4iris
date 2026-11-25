#!/usr/bin/env bash
# IW4Iris - approximate MSVC's /W4 /WX on the cinematic component with GCC.
#
# IW4x builds with Visual Studio at warning level 4 and warnings as errors.
# Two kinds of MSVC warnings have no equivalent in the project's GCC flags:
# C4456/C4457/C4458 (a declaration hides a local, parameter or member) and
# C4244/C4267 (a conversion may lose data). This re-runs the compile
# commands of IW4Iris's own files (from the MinGW cross build) with
# -fsyntax-only and GCC's closest warnings, and fails when any are reported
# in IW4Iris sources.
#
# It is an approximation: the Windows CI job (MSVC) remains the authority.
#
# usage: tools/cross/msvc-lint.sh <iw4x-client checkout> [release|debug]
#        (after tools/iw4x-overlay.sh and tools/cross/build-iw4x.sh)
set -euo pipefail
iw4x="$(cd "${1:?usage: msvc-lint.sh <iw4x-client checkout> [release|debug]}" && pwd)"
cfg="${2:-release}"
cd "$iw4x"
[ -f build/mingw/iw4x.make ] || { echo "error: run tools/cross/build-iw4x.sh first" >&2; exit 1; }

touch src/Components/Modules/IW4Iris/*.cpp deps/iw4iris/core/src/*.cpp
mapfile -t commands < <(make -s -n -C build/mingw -f iw4x.make config="${cfg}_win32" \
  FORCE_INCLUDE="-include STDInclude.hpp" 2>/dev/null | grep -- '-c ' | grep -E 'Modules/IW4Iris/|deps/iw4iris/core/src/' || true)
[ "${#commands[@]}" -gt 0 ] || { echo "error: no IW4Iris compile commands found" >&2; exit 1; }

extra="-fsyntax-only -Wno-error -Wshadow=local -Wconversion -Wno-sign-conversion -Wfloat-conversion"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
for cmd in "${commands[@]}"; do
  # Drop the output arguments (-o file, -MF file): only the diagnostics matter.
  cmd="$(sed -E 's/ -o "?[^ ]+"?//; s/ -MF "?[^ ]+"?//; s/ -MD//; s/ -MP//' <<<"$cmd")"
  (cd build/mingw && eval "$cmd $extra") >>"$log" 2>&1 || true
done
# Only diagnostics in IW4Iris's own code count (core and the component).
issues="$(grep -E '(Modules/IW4Iris/|deps/iw4iris/core/)[^:]+:[0-9]+:[0-9]+: (warning|error)' "$log" | sort -u || true)"
touch src/Components/Modules/IW4Iris/*.cpp deps/iw4iris/core/src/*.cpp  # the real build recompiles them
if [ -n "$issues" ]; then
  echo "$issues"
  echo "error: $(wc -l <<<"$issues") MSVC-style warning(s) in IW4Iris sources" >&2
  exit 1
fi
echo "info: no MSVC-style warnings in ${#commands[@]} IW4Iris source file(s)"
