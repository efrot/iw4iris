#!/usr/bin/env bash
# IW4Iris — copy the packaged binaries into the repository root, so that
# GitHub's "Code -> Download ZIP" is ready to run: "IW4Iris Launcher.exe"
# at the top and client\ (the cinematic iw4x.dll and Sentry's helpers)
# next to it, beside resources\fonts.
#
# usage: tools/update-prebuilt.sh [version]     (after tools/package.sh)
#
# client\ must hold only the client's files: the launcher installs every
# file in it into the cinematic environment.
set -euo pipefail
version="${1:-0.4.0}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
pkg="$repo/dist/IW4Iris-v${version}-Windows-x86"
[ -f "$pkg/IW4Iris Launcher.exe" ] && [ -f "$pkg/client/iw4x.dll" ] ||
  { echo "error: $pkg is incomplete; run tools/package.sh first" >&2; exit 1; }
cp "$pkg/IW4Iris Launcher.exe" "$repo/IW4Iris Launcher.exe"
rm -rf "$repo/client"
cp -r "$pkg/client" "$repo/client"
(cd "$repo" && sha256sum "IW4Iris Launcher.exe" client/* > PREBUILT-SHA256SUMS.txt)
echo "info: updated the prebuilt binaries from $pkg"
cat "$repo/PREBUILT-SHA256SUMS.txt"
