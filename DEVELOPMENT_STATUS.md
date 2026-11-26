# Development status

Persistent notes so development can continue across sessions. Update this
file with every significant change.

_Last updated: 2026-11-26 — v0.5.1 (film sets load again: the offline
guard no longer drops the game's packets to its own local server; first
session with a real IW4x installation and in-game testing on the
developer machine). Audit findings and their verification state:
[docs/BACKLOG.md](docs/BACKLOG.md); comparison with other tools:
[docs/COMPETITIVE.md](docs/COMPETITIVE.md)._

### Handoff (read first)

* **In-game testing is possible on the development machine** (Windows 11,
  Steam MW2 with IW4x, Visual Studio 2022 Build Tools, CMake, Python 3.12):
  `tools/build.ps1` builds everything with MSVC; the launcher at the
  repository root installs `client/` into the cinematic environment
  (`%LOCALAPPDATA%\IW4Iris\game`), and the game can be driven and observed
  there. Runtime results go into docs/BACKLOG.md with their evidence.
* **Film set loading screen (F-48, fixed in 0.5.1):** every film set stayed
  on the loading screen because layer 1 of the offline guard dropped
  loopback packets that IW4x sends through `Sys_SendPacket`. Reproduced in
  the game with 0.5.0; the loopback trampoline is in
  `IrisOfflineGuard.cpp`. Before 0.5.1 no film set ever reached the game
  view, so **every Director-mode feature (actors, props, effects, motion
  graphics in a set) is still unverified in the game**.
* **Verified in the game with 0.5.1:** film set from the main menu
  (Highrise) and while a modded server's demo played (Afghan); template
  actor spawned; *Reload set* precached the rifle and rebuilt the actor
  holding it; demo opened with the new Play button; panels docked after an
  update. **Open question:** a film set started after such a demo keeps the
  demo's mod folder (`fs_game`), so the mod's own scripts may run in the
  set; decide whether to clear it (IW4x's own mod switch restarts the
  renderer).
* **Earlier user results:** the Director Mod compiles and answers in the
  game (0.3.1 scripts: "Director Mod ready: protocol 2", 48 models
  precached on mp_highrise); a 1248 s demo plays, indexes, fits the
  timeline and seeks backwards (user logs of commit c499116).
* **Engine bindings used for players / moments:** only IW4x's size-checked
  structures (`cg_s::snap`, `snapshot_s::clients / entities`, `centity_s`
  prev / next states, `cg_s::frameInterpolation`, `playerState_s`, entity
  flags `EF_DEAD / EF_CROUCHING / EF_PRONE / EF_ADS`, `ET_EVENTS +
  EV_OBITUARY`, `ENTITYNUM_NONE`). No new addresses.
* **Angles (0.5):** `iris/sync` (kill feeds, sync suggestion, scene-time
  shift), `Project::angles`, `CameraCut::angle`, `RenderJob::angle`.
  DemoControl's clock (`SetClock`) moves an angle's scene time onto the
  main demo's; everything that works in scene time (playhead, seeks,
  moments, captures) follows. `Session::loadedAngle` says which recording
  plays (identified by path per demo generation); a project with angles
  never replaces its main demo. Unverified: obituaries reach every client
  (kill feeds match), one server session shares the server clock.
* **BeatSync (0.5):** `iris/audio` (WAV, peaks, onsets, tempo, Ellis DP
  beat tracking: tested on synthetic click tracks only), `iris/beatsync`
  (beats in file time, actions, `AudioForRange`), `Project::audio`.
  `IrisAudio.cpp` loads (WAV directly, else FFmpeg to mono s16le 44.1 kHz),
  analyses in a thread, previews through waveOut (mono, restarts when it
  drifts > 120 ms). Captures add the music through FFmpeg. Unverified: the
  preview in the game (waveOut next to the game's own sound), FFmpeg's
  `adelay=...:all=1` (FFmpeg >= 4.2) and `apad=whole_dur` (>= 4.2).
* **Unverified assumptions to check first with a real multi-player demo:**
  the snapshot client list holds every connected player; player entity
  `apos.trBase` carries the view pitch; obituary entities carry victim in
  `otherEntityNum` and killer in `attackerEntityNum`; the 3x analysis pass
  sees every snapshot at the user's frame rate.

## State

| Area | State |
| --- | --- |
| Core library (`core/`) | scene, Director reconciler, timeline navigation, fuzzy search, diagnostics, players, moments, preflight, angle sync, audio analysis and BeatSync; 187 test cases pass on Linux (GCC 13, `-Werror` with `-Wconversion -Wshadow -Wpedantic`) and under ASan / UBSan |
| Director Mod (`mods/iw4iris_director`) | **runtime-tested (start only):** compiles at map load and answers "ready: protocol 2" with its precache (user logs, 0.3.1 scripts; and in the game here with 0.5.0). Requests (actors, props, effects) not yet exercised: until 0.5.1 the game never joined a film set (F-48) |
| Director bridge, graphics renderer, Director / motion graphics editor | implemented; cross-compile warning-free; **never run in the game** |
| IW4x component (`engine/iw4x/`) | implemented; **cross-compiles and links** into IW4x `912099b7c405` with IW4x's `-Werror` flags; **builds with MSVC in the Windows CI job** (client DLL since run 3); **runs in the game** (0.5.0 here: editor, start screen, scene wizard, film set start, diagnostics; users: demos, camera, seeking) |
| Launcher (`launcher/`) | **runtime-tested on Windows:** environment setup, Director Mod install, *Update & Launch* (user logs; and here with 0.5.0) |
| Blender add-on | helpers tested against the C++ exporter; not run inside Blender |
| After Effects script | generated and structure-tested; not run in AE |
| Windows (MSVC) build | GitHub Actions `windows-2022` job runs `tools/build.ps1`: green since run 5 (455594a) except runs 20 to 23 (C4459, fixed in a9fa81b); runs 24 to 29 green (angles, coverage, BeatSync): client DLL, launcher and tests built with Visual Studio 2022, the test suite runs and passes on Windows, the package is uploaded as an artifact |
| Win32 test binary | cross-compiles; runs and passes on Windows in the CI job (MSVC build); Wine does not run in the container |

## How the current binaries were produced

**Since 0.5.1 (Windows, MSVC):** `toolsuild.ps1 -Configuration Release`
(tests included), `tools\package.ps1`, then `bash tools/update-prebuilt.sh`
on the development machine, after the in-game checks in docs/BACKLOG.md
(F-45, F-48 to F-51). The version comes from `CMakeLists.txt`.

**Up to 0.5.0 (Linux container, MinGW cross-build):**

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
tools/update-prebuilt.sh      # refresh the prebuilt binaries at the repository root
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

1. **In-game confirmation of the film set start** (user is testing): the
   map loads, iw4iris.log shows "Director Mod ready". If not, the log shows
   the last step reached.
2. **Milestone A in the game** (docs/TESTING.md §1–§6): camera override,
   editor, recording of real frames.
3. **Demo Director in the game** with a real multi-player demo: player
   list complete, names / teams right, eyes / chase cameras stable,
   moments (kills) found, the analysis pass returns to its start.
3a. **Angles in the game** (§5a) with two demos of one match: the
   suggestion, a kill at its marker in both, switching, Queue per angle.
3b. **Music in the game** (§5b): preview heard in time, beats on real
   music, music in recorded MP4 / next to PNG sequences.
4. **Milestone B** (§8): an actor spawns, animates, holds a weapon, is
   deleted and respawned.
5. **Motion graphics in the game** (§9): is a matching depth buffer bound
   at present time? If not, research a scene-stage draw.
6. Then: checkpoint rewind research (needs a way to restore client and
   cgame state; IW4x does not offer one), bone cameras (DObj bone
   bindings), render passes, persistent GPU buffers (F-06) after profiling.

## Conventions

* Core code: 4-space indent, `iris::` namespace, no engine knowledge.
* Component code: IW4x style (2-space indent), `IW4Iris::` namespace,
  files `Iris*.cpp`; all game access through `IrisEngine.cpp`.
* Never add an engine address IW4x does not already declare and use. The
  one address the component writes out itself, `0x6CAF080`
  (`GfxCmdBufSourceState`), is the one IW4x's `Renderer::ListSamplers`
  uses, and its contents are validated every frame before use.
* The Director Mod is IW4Iris' own code; iw4cine has no licence and was only
  studied.
* Every feature is listed in FEATURE_MATRIX.md with an honest status.
