# Changelog

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
