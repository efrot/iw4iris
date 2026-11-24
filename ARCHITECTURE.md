# Architecture

IW4Iris has four layers. Only the engine adapter knows anything about IW4.

```
┌─────────────────────────────── launcher/ (Windows exe) ──────────────────────────────┐
│ detect IW4x · validate · isolated environment (junctions + copies) · launch · repair │
└───────────────────────────────────────────────────────────────────────────────────────┘
                     starts iw4x.exe in the environment, which loads ↓
┌──────────────────────────── cinematic build of iw4x.dll ─────────────────────────────┐
│  IW4x client (pinned revision, + engine/iw4x/patches)                                 │
│    └─ Components::Cinematic  (engine/iw4x/src, installed as Modules/IW4Iris)          │
│         Runtime ── main-thread frame, view hook, input, render-thread entry          │
│         Engine ─── THE IW4 ADAPTER: refdef, dvars, demo state, entities              │
│         OfflineGuard · Clock · DemoControl · Capture · Director · Graphics           │
│         Editor (Dear ImGui): panels, timeline, graph editor, overlay                 │
│         Session ── shared state (project, playhead, history), main-thread queue      │
│                    │ uses                                                             │
│  iris::core (core/, static library, no engine knowledge)                             │
│    math · curve · camera (lens, path) · timeline · rig · project · history           │
│    recording · exporters · demo · presets · scene · assets · shots · templates        │
│    mesh · font · logo · model · projection · graphics (motion graphics geometry)       │
└───────────────────────────────────────────────────────────────────────────────────────┘
mods/iw4iris_director (GSC Director Mod, installed into the environment's userraw by the launcher)
tools/blender (Python add-on) · tools/*.ps1|*.sh (build, overlay, package) · tests/
```

## iris::core — game independent

C++20, depends only on nlohmann/json. Built for Linux (tests) and Windows
(inside IW4x, and by CMake for the launcher). Compiled with
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` in CMake and with
IW4x's own warnings-as-errors flags in the client build.

| Header | Responsibility |
| --- | --- |
| `math.hpp` | `Vec3`, `Quat`, `Mat3`; engine angle convention (`AnglesToAxis` matches id Tech 3 `AngleVectors`, tested against it), slerp, squad, log/exp |
| `curve.hpp` | `ScalarCurve`: hold / linear / smooth (Bessel, extremum-clamped) / Bézier keys with handles, AE-style eases, Gauss–Legendre integration |
| `camera.hpp` | Lens model (focal length ↔ FOV, sensors, `cg_fov` ↔ true FOV, thin-lens DoF), `CameraPath` (positions: linear, auto-clamped Hermite, centripetal Catmull-Rom, Bézier; rotation: quaternion squad or unwrapped Euler; constant speed by arc-length table) |
| `timeline.hpp` | Rational frame rates, timecode, camera cuts, `TimeRemap` (cinematic time → scene time through a speed curve), loop / in / out |
| `rig.hpp` | Evaluates a project camera at time *t* (free, static/look-at, dolly, orbit, follow, game view), deterministic shake, the free-camera controller |
| `project.hpp` | `.iw4iris` data model, versioned JSON, migrations, atomic save, relative demo paths, autosave paths, IWXMVM campath import, missing-reference checks |
| `history.hpp` | Snapshot undo/redo with mergeable steps |
| `recording.hpp` | Recording settings, validation, FFmpeg arguments, Windows command-line quoting, disk estimates, exact per-frame clock steps |
| `exporters.hpp` | Blender JSON, After Effects JSX, CSV with documented coordinate conversions |
| `demo.hpp` | `.dm_13.json` metadata, `.dm_13` record scanning (snapshot server times and file offsets), demo discovery and search |
| `presets.hpp` | Shot presets that generate editable keys / speed keys / markers |

### Determinism

Every timeline-driven value is a pure function of cinematic time *t*:
camera paths, rigs, shake (seeded noise of *t*), keyframed properties, the
speed curve. The scene (demo) time is derived from *t* through `TimeRemap`,
never the other way round during a capture. The same *t* therefore always
yields the same frame, which is what offline rendering, slow motion and
multi-pass rendering need.

## The engine adapter — `Engine.cpp`

All reads and writes of IW4 memory are here: `cg_s::refdef`,
`clientActive_t`, `clientStatic_t::realtime`, `clientConnection_t`,
`cg_entitiesArray`, dvars, the command buffer. It uses only bindings IW4x
declares (COMPATIBILITY.md lists each). Dvar writes go through an override
registry that remembers the original value the first time, so
`RestoreAll()` ("reset to defaults") restores the game exactly.

## Threads

| Thread | Runs |
| --- | --- |
| Game main thread | `Runtime::MainFrame` (Scheduler `MAIN`, once per `Com_Frame`): offline check, posted work, `DemoControl::Update`, `Capture::MainFrame`, playhead, free camera, dvars, autosave. `Hooks::OnViewReady` (inside `CG_DrawActiveFrame`): reads the game view, evaluates the shot camera, writes `refdef`. `Runtime::RendererFrame` (Scheduler `RENDERER`): capture sync tags. Window procedure (editor input, F1). |
| Render thread | `Runtime::BackendFrame` (`Renderer::OnBackendFrame`, before `Present`): `Capture::BackendFrame` reads the back buffer **first**, then the editor is built and drawn. |
| Encoder workers | PNG/JPEG encoding (up to 4) or one FFmpeg pipe writer. |
| Background | demo index scan, demo folder scan. |

`Session::mutex` (recursive) guards the shared editor state; the UI holds it
while building a frame. Anything that touches the game from the UI is
`Session::Post`ed to the main thread. Dear ImGui has its own mutex
(`Runtime::uiMutex`) because input arrives on the main thread and frames are
built on the render thread.

## Camera override

IW4x hooks the `R_EndDobjScene` call inside `CG_DrawActiveFrame`
(`Rumble::CG_SetRumbleReceiver`). The patch calls
`IW4Iris::Hooks::OnViewReady` there: the frame's view has been computed and
the scene not yet submitted. `OnViewReady` replaces `refdef.view.org`,
`axis` and `tanHalfFovX/Y`. While a cinematic camera is active, the editor
sets `cg_thirdPerson 1` (draw the followed player's body), `cg_drawGun 0`
and maximum LOD bias, restoring them afterwards.

## Offline guard (fail closed)

See `OfflineGuard.hpp`. The engine packet sender is replaced by a function
that drops everything; Winsock and HTTP imports of the executable and the
client DLL refuse non-loopback destinations (and listening sockets not bound
to loopback); every frame the connection is
checked and a non-loopback connection disconnects the client and disables
the tools until restart. If the packet sender cannot be replaced the tools
never start.

## Recording

```
main thread                         render thread                    workers
──────────                          ─────────────                    ───────
seek to range start - warm-up
Clock::Begin, pre-advance
per frame: Clock::Advance(step k)   per Present:
OnViewReady: t from scene time        warm-up: read sync tag → latency
RENDERER: draw sync tag (warm-up)     range: GetRenderTargetData →     PNG/JPEG (stb)
                                      queue.Push (blocks when full) →  or FFmpeg stdin
Finish: restore dvars, join, report
```

* **Clock.** `timeGetTime` and `QueryPerformanceCounter` of the executable
  are import-patched; during a capture they return a virtual time that
  advances by `ComputeFrameSteps()` (exact integer milliseconds per frame,
  no accumulated error). `Clock::Verify` checks `Sys_Milliseconds` follows it
  before any capture.
* **Frame identity.** The camera time of a rendered frame comes from the
  scene time that frame shows (`Capture::FrameTime`), so camera and scene
  stay consistent wherever IW4x's `MAIN` pipeline sits inside `Com_Frame`.
  The renderer's latency is measured with 2-bit-per-channel colour tags
  drawn during warm-up frames, which are discarded.
* **Clean output.** Frames are read before the editor is drawn; overlays are
  drawn into the editor's own draw list afterwards.
* **Backpressure.** A bounded queue; when the encoder is behind, the render
  thread waits, which is correct for an offline render.

## Director Mode — hybrid native / GSC

```
editor (native)                         film set (local listen server, same process)
───────────────                         ────────────────────────────────────────────
iris::Scene (project) ── SyncScene ──► request queue ──► IW4IrisNextRequest ──► Director Mod (GSC)
   actors, props, effects                                                       script models, tags,
EvaluateActor / Prop at the       ◄── IW4IrisPoses(server time) ◄────────────── animations, effects
   predicted timeline time        ◄── IW4IrisReportTag (bones) ◄──────────────
asset catalog ◄── DB_EnumXAssets ──── IW4IrisManifest (precache at level start)
```

* `Director.hpp` / `IrisDirector.cpp`: builtins, asset discovery, the
  manifest, scene diffing into requests (spawn, weapon, attachments,
  animation, props, links, deletes), effect events, bone samples
  interpolated to the client's render time, film set start / reload /
  leave, map list, surface traces, view target.
* `mods/iw4iris_director`: the GSC side (docs/DIRECTOR_PROTOCOL.md).
* The scene itself (`iris::Scene`, directions, templates, shots, asset
  classification) is game independent and unit-tested.

## Motion graphics

`Graphics.hpp` / `IrisGraphics.cpp` (docs/MOTION_GRAPHICS.md): geometry
built on the main thread from `iris::GraphicDef` (cached by geometry key),
poses evaluated in `OnViewReady` into a draw list tagged with the frame's
scene time, drawn on the render thread before `Capture::BackendFrame`
reads the back buffer. View, projection and depth use are validated at run
time and reported.

## Project files

`docs/PROJECT_FORMAT.md` documents the schema. Saves are atomic
(`.tmp` + rename), autosaves go next to the project, demo paths are stored
relative to the project when possible, unknown fields are ignored, unknown
enum values become warnings, and older schemas are migrated on load.

## Launcher

`launcher/src/InstallPlan.*` (portable, unit-tested) decides what the
environment links and copies; `Environment.*` (Windows) creates directory
junctions with `FSCTL_SET_REPARSE_POINT`, copies or hard-links root files,
installs the cinematic `iw4x.dll`, and records everything in
`iw4iris-environment.json`. Uninstall removes only recorded entries that are
still what was created, and never descends into a reparse point.

## Source map

```
core/                 game-independent library (include/iris, src; src/vendor wraps third-party headers)
mods/iw4iris_director the Director Mod (GSC) and its mod.json
engine/iw4x/src/      the IW4x component (copied to src/Components/Modules/IW4Iris)
engine/iw4x/patches/  the IW4x source patch
engine/iw4x/premake/  premake target for the editor libraries
launcher/             the Windows launcher
tests/                C++ unit tests (own runner), tests/python (Blender add-on)
tools/                build / overlay / package scripts, Blender add-on, cross-build
external/iw4x-client  pinned IW4x (submodule)
third_party/          Dear ImGui (docking), nlohmann/json, stb, nanosvg, earcut (submodules)
resources/fonts       Rubik, Font Awesome Free (OFL)
samples/, presets/    sample projects, preset catalogues
```
