#!/usr/bin/env bash
# IW4Iris — assemble the release package from built binaries (Linux, for
# cross-compiled builds). Same layout as tools/package.ps1.
#
# usage: tools/package.sh <iw4x.dll> <IW4Iris Launcher.exe> [version]
set -euo pipefail
client="${1:?usage: $0 <iw4x.dll> <launcher.exe> [version]}"
launcher="${2:?usage: $0 <iw4x.dll> <launcher.exe> [version]}"
version="${3:-0.4.0}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
name="IW4Iris-v${version}-Windows-x86"
out="$repo/dist/$name"
rm -rf "$out" "$repo/dist/$name.zip"
mkdir -p "$out/client" "$out/resources/tools/blender" "$out/resources/fonts" "$out/docs" "$out/samples" "$out/presets"
i686-w64-mingw32-strip -o "$out/client/iw4x.dll" "$client" 2>/dev/null || cp "$client" "$out/client/iw4x.dll"
# Sentry's crash helpers are built with the client and belong next to it.
for helper in sentry-crash.exe sentry-wer.dll; do
  if [ -f "$(dirname "$client")/$helper" ]; then
    i686-w64-mingw32-strip -o "$out/client/$helper" "$(dirname "$client")/$helper" 2>/dev/null || cp "$(dirname "$client")/$helper" "$out/client/"
  fi
done
i686-w64-mingw32-strip -o "$out/IW4Iris Launcher.exe" "$launcher" 2>/dev/null || cp "$launcher" "$out/IW4Iris Launcher.exe"
cp "$repo"/resources/fonts/* "$out/resources/fonts/"
cp "$repo/tools/blender/iw4iris_import.py" "$out/resources/tools/blender/"
cp -r "$repo"/samples/* "$out/samples/"
cp -r "$repo"/presets/* "$out/presets/"
# The Director Mod (GSC); the launcher installs it into the environment.
mkdir -p "$out/mods"
cp -r "$repo/mods/iw4iris_director" "$out/mods/"
for doc in README.md INSTALL.md TROUBLESHOOTING.md CHANGELOG.md FEATURE_MATRIX.md COMPATIBILITY.md LICENSE THIRD-PARTY-NOTICES.md; do
  cp "$repo/$doc" "$out/"
done
cp -r "$repo"/docs/* "$out/docs/"
(cd "$repo/dist" && python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', sys.argv[1])" "$name")
echo "info: $repo/dist/$name.zip"
