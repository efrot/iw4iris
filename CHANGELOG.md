# Changelog

## 0.3.0 — 2026-11-25 ("Creator Preview": audit fixes and usability, not yet tested in the game)

Verification states of every item: [docs/BACKLOG.md](docs/BACKLOG.md).

### Fixed

* **Creating a cinematic on a map failed with "script compile error /
  bad syntax"** (reported from the game): the Director Mod used `anim`, a
  reserved word of the script language, as a name. Fixed (mod 0.3.1; the
  launcher's *Update & Launch* installs it); a static check of the scripts
  runs in CI, and the editor reports a film set whose map never loads
  instead of waiting forever (F-35).
* A scene template used a pointer to an actor after the actor list grew
  (memory error seen once on Windows); tests now also run under the
  address and undefined-behaviour sanitizers (F-36).
* **The time could not be moved through a demo:** the timeline stayed at
  its default 10 seconds whatever the demo's length. It now covers the
  whole demo once the demo index is known (F-28).
* Recordings showed actors, props, effects and graphics frozen: the
  playhead did not follow the capture (F-27).
* The Director bridge treated objects as spawned before the mod answered;
  it now changes state only on acknowledgement, with timeouts, bounded
  retries, ordering, stale-answer rejection and bounded queues. Director
  Mod protocol 2 (F-01).
* Flying changed the project's active camera; flying now uses a separate
  free view (F-02).
* Graphics meant to hide behind walls were drawn on top of everything when
  occlusion was unverified; they are now reported as degraded instead
  (F-03).
* Recording now checks the scene (film set ready, failed objects, assets
  needing a reload, occlusion for depth graphics) (F-04) and pre-rolls
  film-set animations so they start on time (F-08).
* A failed launcher setup left links and copies behind; it now rolls back
  (F-30).
* The first Windows (MSVC) builds found six compiler errors GCC accepted;
  all fixed at their cause (F-31).

### Added

* **Timeline navigation:** drag on the ruler or empty rows, magnetic
  snapping (Alt to move freely), typed times (`12.5`, `1:05`, `90f`,
  timecode), an in/out bar (drag ends, move, draw, double-click), an
  overview scroll bar, row scrolling, follow playhead, previous / next
  edit point (Up / Down), I / O, + / -, \.
* **Free view** (V) with "camera from view" and "move camera to view".
* **Command palette** (Ctrl+K) and a single **+ Add** menu in the toolbar.
* **Start screen** with the four ways in and recent projects.
* **Diagnostics:** capability states with fixes, a guided self-test, a
  redacted support bundle; a launcher Diagnostics page.
* **Launcher:** Install & Launch / Update & Launch in one click.
* Windows CI (MSVC) and Linux CI; `tools/cross/msvc-lint.sh`.

## 0.2.0 — 2026-11-24 (Director Mode and motion graphics preview, not yet tested in the game)

### Added

* **Director Mode:** create cinematics on any installed map without
  recording gameplay. A film set is the map loaded as a local offline
  session; the new **IW4Iris Director Mod** (GSC, installed by the
  launcher) owns the actors, props and effects, the editor drives it
  through native GSC builtins (protocol 1).
* Scene wizard (map, template), Director home, character library by
  faction, animation library, props & effects library, all built from
  the assets the map actually loads.
* Actors: character, transform keys, idle animation, animation clips,
  weapons with camo and attachments, extra models on bones, visibility,
  directions ("walk to", "turn towards", "aim at", ...) compiled into
  editable tracks, 17 framed shot types from an actor.
* Props (on the map or an actor's bone), effects (one-shot / repeating, on
  bones), click selection, outlines, actor paths, a move / rotate / size
  gizmo, snap to ground, composition guides, beginner / professional mode.
* **Motion graphics:** extruded 3D text (TrueType / OpenType, kerning,
  lines, vertical and circular layouts), logos from SVG or transparent
  images (extruded or flat), image planes, primitives, OBJ models,
  tracking anchors; materials, tracking (world, actor bone, billboard,
  lens), parenting, 18 seeded animation presets, title templates, a
  one-click camera orbit; a world-space D3D9 renderer included in
  recordings, with run-time checked depth occlusion.
* Timeline rows for actor clips (move / trim), object keys, graphic
  animations and effect plays.
* Project schema 2 with the scene; schema 1 projects migrate.
* Launcher installs, verifies and repairs the Director Mod.
* 115 test cases (37 new) covering the scene, directions, assets, shots,
  templates, geometry, fonts, logos, models, projection and graphics.

### Third-party

* nanosvg (zlib), earcut.hpp (ISC); stb_truetype and stb_image (MIT / PD).

## 0.1.0 — 2026-11-24 (first preview, not yet tested in the game)

### Added

* **Cinematic core library** (`iris::core`): engine-convention math and
  quaternion interpolation, keyframe curves with Bézier handles and eases,
  camera paths (smooth with clamped auto tangents, linear, centripetal
  Catmull-Rom, Bézier, hold; quaternion or Euler rotation; constant speed),
  lens model and thin-lens depth of field, rational frame rates and
  timecode, speed-ramp time remapping, camera cuts, markers, the
  `.iw4iris` project format with migrations and atomic saves, undo history,
  camera rigs (free, static, dolly, orbit, follow), deterministic shake,
  recording configuration and exact per-frame clock steps, Blender /
  After Effects / CSV exporters, IW4x demo metadata and record scanning,
  twelve shot presets, IWXMVM / T6Iris campath import. 78 test cases.
* **IW4x component** (`Components::Cinematic`) in a dedicated cinematic
  build of the IW4x client: engine adapter on IW4x's own bindings, camera
  override at IW4x's `CG_DrawActiveFrame` hook, fail-closed offline guard,
  virtualised render clock with self-check, demo playback / speed / pause /
  seek, back-buffer capture with frame-sync calibration, built-in PNG / JPEG
  writer and FFmpeg pipe, render queue.
* **Editor** (Dear ImGui, docking): menus, toolbar, workspaces, scene
  outliner, inspector, multitrack timeline, graph editor, look panel,
  demo browser, recording, render queue, export, shot presets, lighting
  presets, settings, diagnostics, history, onboarding, shortcuts, in-view
  path overlay.
* **Launcher**: installation detection and validation, isolated cinematic
  environment from junctions and copies, launch offline, launch with a
  project, repair, uninstall, FFmpeg discovery.
* **Blender add-on** importing IW4Iris camera exports.
* Build, overlay and packaging scripts for Windows (PowerShell, VS 2022)
  and Linux (MinGW-w64 cross build), documentation, sample project.

### Known limitations

See COMPATIBILITY.md and FEATURE_MATRIX.md: actors, dynamic lights, render
passes beyond beauty, bone cameras, custom render sizes and snapshot
rewinding are not implemented; pause / seek / sun and the camera hook are
experimental until verified in the game.
