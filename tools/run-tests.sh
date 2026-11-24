#!/usr/bin/env bash
# IW4Iris — build and run the game-independent tests on Linux.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$repo" -B "$repo/build/linux" -DCMAKE_BUILD_TYPE=Debug -DIW4IRIS_BUILD_LAUNCHER=OFF >/dev/null
cmake --build "$repo/build/linux" --parallel
"$repo/build/linux/iw4iris_tests"
python3 -I "$repo/tests/python/test_blender_addon.py"
