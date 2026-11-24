# Development status

Persistent notes so development can continue across sessions. Update this
file with every significant change.

_Last updated: 2026-11-24 — v0.1.0 preview._

## State

| Area | State |
| --- | --- |
| Core library (`core/`) | complete for v0.1; 78 test cases pass on Linux (GCC 13, `-Werror` with `-Wconversion -Wshadow -Wpedantic`) |
| IW4x component (`engine/iw4x/`) | implemented; **cross-compiles and links** into IW4x `912099b7c405` with IW4x's `-Werror` flags; **never run in the game** |
| Launcher (`launcher/`) | implemented; cross-compiles; install rules unit-tested; never run on Windows |
| Blender add-on | helpers tested against the C++ exporter; not run inside Blender |
| After Effects script | generated and structure-tested; not run in AE |
| Windows (MSVC) build | scripted in `tools/build.ps1` following IW4x's CI; not run |
| Win32 test binary | cross-compiles; could not run (Wine segfaults in the container) |

## How the current binaries were produced (Linux container)

```
apt-get install g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix   # GCC 13
# mingw-w64 13 headers/libs (Ubuntu archive, arch-independent):
dpkg -i --force-depends mingw-w64-common_13.0.0-2ubuntu1_all.deb mingw-w64-i686-dev_13.0.0-2ubuntu1_all.deb
# premake5 5.0.0-beta8 (GitHub release) and UASM (built from github.com/Terraspace/UASM) on PATH
tools/cross/build-all.sh          # or step by step:
tools/iw4x-overlay.sh <iw4x-checkout>
tools/cross/build-iw4x.sh <iw4x-checkout> release
cmake -S . -B build/win32 -DCMAKE_TOOLCHAIN_FILE=tools/cross/mingw32.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/win32
tools/package.sh <iw4x.dll> "build/win32/launcher/IW4Iris Launcher.exe"
```

Pitfalls met (all handled by the scripts now):

* IW4x needs mingw-w64 ≥ 13 headers (Ubuntu 24.04 has 11).
* GCC 13 needs `std::ceilf` shims, a non-fatal "#pragma once in main file"
  for the PCH and suppression of known libstdc++ false positives — GCC ≥ 14
  needs none of this.
* Do **not** pass `UASM=...` on the make command line: make exports it,
  and UASM reads its options from the `UASM` environment variable.
* `FORCE_INCLUDE` on the make command line reaches every sub-make, so the
  dependencies are built first without it (two-phase make in the script).
* Component `.cpp` files are prefixed `Iris*` because IW4x already has
  `Runtime.cpp`, `Clock.cpp`, `Engine.cpp` and `Session.cpp`; premake puts
  all objects in one directory. After renaming sources, delete stale
  `obj/.../*.d` files that mention `IW4Iris/`.

## Next steps (priority order)

1. **Run docs/TESTING.md §1–§4 in the game** (needs a Windows machine with
   MW2 + IW4x). Above all: does the view hook move the camera (§3)?
   Record findings in COMPATIBILITY.md and FEATURE_MATRIX.md.
2. If the view hook is insufficient: find the `R_RenderScene` /
   `R_SetViewParmsForScene` call sites for IW4 the way IWXMVM did for IW3,
   **verify them in the game**, and propose them to IW4x as `Game::`
   bindings (no unverified addresses in IW4Iris).
3. Verify pause / seek via `clientStatic_t::realtime`; otherwise research
   the `msec` path of `Com_Frame`.
4. Verify the render clock (`Clock::Verify` logs the outcome) and frame-sync
   tags with a real capture; fix the capture timing model if
   `unalignedFrames` is non-zero.
5. Windows CI building the release package with MSVC.
6. Phase 5 (actors) via GSC script models in local private matches.

## Conventions

* Core code: 4-space indent, `iris::` namespace, no engine knowledge.
* Component code: IW4x style (2-space indent), `IW4Iris::` namespace,
  files `Iris*.cpp`; all game access through `IrisEngine.cpp`.
* Never add an engine address IW4x does not already declare and use.
* Every feature is listed in FEATURE_MATRIX.md with an honest status.
