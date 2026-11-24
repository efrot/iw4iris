# Changelog

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
