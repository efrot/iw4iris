#!/usr/bin/env bash
# IW4Iris — install the IW4Iris component into an IW4x client checkout.
#
# usage: tools/iw4x-overlay.sh <iw4x-client-dir>
#
#   * applies engine/iw4x/patches/*.patch (skipped when already applied),
#   * copies the component to src/Components/Modules/IW4Iris/,
#   * copies the cinematic core, Dear ImGui and stb_image_write to
#     deps/iw4iris/ and the premake target to deps/premake/iw4iris.lua.
#
# The checkout must be at the IW4x revision in compatibility/manifest.json.
set -euo pipefail

dst="$(cd "${1:?usage: $0 <iw4x-client-dir>}" && pwd)"
repo="$(cd "$(dirname "$0")/.." && pwd)"
expected="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["iw4x"]["commit"])' "$repo/compatibility/manifest.json")"
actual="$(git -C "$dst" rev-parse HEAD)"
if [ "$actual" != "$expected" ]; then
  echo "warning: $dst is at $actual, IW4Iris is verified against $expected" >&2
fi

for patch in "$repo"/engine/iw4x/patches/*.patch; do
  if git -C "$dst" apply --reverse --check --ignore-whitespace "$patch" 2>/dev/null; then
    echo "info: $(basename "$patch") already applied"
  else
    git -C "$dst" apply --ignore-whitespace "$patch"
    echo "info: applied $(basename "$patch")"
  fi
done

# Copy with rsync --checksum where available so unchanged files keep their
# timestamps and incremental builds stay incremental.
sync()
{
  if command -v rsync >/dev/null; then
    rsync -a --checksum --delete "$@"
  else
    local dest="${@: -1}"
    rm -rf "$dest"
    mkdir -p "$dest"
    cp -r "${@:1:$#-1}" "$dest"
  fi
}

module="$dst/src/Components/Modules/IW4Iris"
mkdir -p "$module"
sync "$repo/engine/iw4x/src/" "$module/"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/core" "$stage/imgui/backends" "$stage/imgui/misc/cpp" "$stage/stb" "$stage/nanosvg" "$stage/earcut/mapbox"
cp -r "$repo/core/include" "$repo/core/src" "$stage/core/"
cp "$repo"/third_party/imgui/*.cpp "$repo"/third_party/imgui/*.h "$repo/third_party/imgui/LICENSE.txt" "$stage/imgui/"
cp "$repo"/third_party/imgui/backends/imgui_impl_dx9.* "$repo"/third_party/imgui/backends/imgui_impl_win32.* "$stage/imgui/backends/"
cp "$repo"/third_party/imgui/misc/cpp/imgui_stdlib.* "$stage/imgui/misc/cpp/"
cp "$repo/third_party/stb/stb_image_write.h" "$repo/third_party/stb/stb_truetype.h" "$repo/third_party/stb/stb_image.h" "$stage/stb/"
cp "$repo/third_party/nanosvg/src/nanosvg.h" "$repo/third_party/nanosvg/LICENSE.txt" "$stage/nanosvg/"
cp "$repo/third_party/earcut/include/mapbox/earcut.hpp" "$stage/earcut/mapbox/"
cp "$repo/third_party/earcut/LICENSE" "$stage/earcut/"
cp "$repo/engine/iw4x/premake/stb_impl.cpp" "$stage/stb_impl.cpp"
mkdir -p "$dst/deps/iw4iris"
sync "$stage/" "$dst/deps/iw4iris/"
cp "$repo/engine/iw4x/premake/iw4iris.lua" "$dst/deps/premake/iw4iris.lua"
echo "info: IW4Iris installed into $dst"
