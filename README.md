# IW4Iris — Cinematic Studio for IW4x

A native cinematic editor and offline renderer for **Call of Duty: Modern
Warfare 2 (2009)** on the [IW4x](https://github.com/iw4x/iw4x-client)
client: keyframed cameras over your demos, a multitrack timeline with a graph
editor, speed ramps, lens and depth of field, colour, and frame-exact
recording to image sequences or ProRes / H.264 / H.265 — built into the game
itself, for trickshot montages and cinematics.

> **Offline only.** The cinematic build blocks all network traffic and
> refuses to connect to servers. Use it for demos and local scenes.

> **Status: v0.1.0, not yet tested in the game.** The editor, camera system,
> demo control and recorder are implemented and the cinematic client builds,
> but none of the engine integration has been run in IW4x by the authors
> yet. Game-independent code is covered by tests. See
> [FEATURE_MATRIX.md](FEATURE_MATRIX.md) for exactly what is verified,
> implemented, experimental or planned, and [docs/TESTING.md](docs/TESTING.md)
> to help verify it.

## What it does

* **Cameras** — free camera (WASD, roll, speed, smoothing, gamepad), dolly
  paths with per-key interpolation (smooth, linear, Catmull-Rom, Bézier,
  hold), eases, constant-speed traversal, quaternion or multi-turn Euler
  rotation, orbit / follow / tripod rigs with player targets, deterministic
  handheld shake, camera cuts.
* **Lenses** — focal lengths from 10 to 300 mm on real sensor sizes,
  anamorphic squeeze, aperture and focus distance with thin-lens depth of
  field mapped onto the engine's blur.
* **Timeline** — keys, box selection, dragging with frame snapping,
  markers, camera cuts, a playback-speed track for slow motion and speed
  ramps, keyframeable look properties, a graph editor with Bézier handles,
  undo / redo.
* **Demos** — a browser for IW4x demos with metadata, search, favourites
  and labels; playback speed, pause, frame stepping and seeking.
* **Look** — film tweaks (brightness, contrast, desaturation, tints), depth
  of field, sun, fog, HUD / gun / crosshair, lighting presets, and a reset
  that restores every setting IW4Iris changed.
* **Recording** — PNG / JPEG sequences without extra software; H.264,
  H.265, ProRes 422 / HQ / 4444 / 4444 XQ and lossless FFV1 through FFmpeg;
  a virtual clock so every output frame is exactly timed however long it
  takes to render; frames are read before the editor is drawn, so footage is
  always clean; a render queue.
* **Compositing** — camera export to Blender (with an importer add-on) and
  After Effects (an ExtendScript that builds the camera), plus CSV.
* **Shot presets** — trickshot orbit, slow-motion jump, killcam transition,
  scope close-up, weapon reveal, rooftop establishing and more; each builds
  ordinary editable keys.
* **Projects** — `.iw4iris` files (versioned JSON) you can share; they
  reference maps and demos, never game files.
* **Launcher** — finds your IW4x installation, builds an isolated cinematic
  copy next to it (links to the game data, nothing in your install is
  changed), launches it offline, repairs and uninstalls.

## Quick start

1. Install IW4x as usual.
2. Unzip `IW4Iris-v0.1.0-Windows-x86.zip` anywhere and run
   **IW4Iris Launcher.exe** → Setup → Set up → Home → Launch.
3. In the game press **F1**, open a demo from the Demos panel, hold the
   right mouse button to fly, press **K** to add keys, make the dolly the
   active camera, and record from the Recording panel.

Details: [INSTALL.md](INSTALL.md). Problems: [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## Default keys

| Key | Action |
| --- | --- |
| F1 | Show / hide the editor |
| Right mouse (hold) / F | Fly the free camera |
| W A S D, Space / Ctrl, Q / E | Move, up / down, roll |
| K / U | Add a key / update the selected key from the view |
| Space | Play / pause |
| ← / → | Previous / next frame (Shift: one second) |
| B / N | In / out point |
| M | Marker |
| G | Graph editor |
| Ctrl+Z / Ctrl+Y, Ctrl+S | Undo / redo, save |
| ? | All shortcuts |

## Building

[BUILD.md](BUILD.md): Visual Studio 2022 (`tools\build.ps1`), or a MinGW-w64
cross build on Linux (`tools/cross/build-all.sh`). The game-independent
tests run anywhere with `tools/run-tests.sh`.

## Documentation

| | |
| --- | --- |
| [INSTALL.md](INSTALL.md) | installation and first steps |
| [BUILD.md](BUILD.md) | building from source |
| [ARCHITECTURE.md](ARCHITECTURE.md) | how it is put together |
| [COMPATIBILITY.md](COMPATIBILITY.md) | IW4x revision, engine bindings, limitations |
| [FEATURE_MATRIX.md](FEATURE_MATRIX.md) | what is verified, implemented, planned |
| [ROADMAP.md](ROADMAP.md) | what comes next |
| [RESEARCH.md](RESEARCH.md) | analysis of T6Iris, IWXMVM and IW4x |
| [docs/PROJECT_FORMAT.md](docs/PROJECT_FORMAT.md) | the `.iw4iris` format |
| [docs/EXPORT.md](docs/EXPORT.md) | Blender and After Effects |
| [docs/TESTING.md](docs/TESTING.md) | tests and the in-game verification procedure |
| [DEVELOPMENT_STATUS.md](DEVELOPMENT_STATUS.md) | where development stands |

## Credits and licence

IW4Iris is free software under the GNU GPL v3 or later ([LICENSE](LICENSE)).
It is built into the [IW4x client](https://github.com/iw4x/iw4x-client)
(GPL-3.0) and owes its design to [IWXMVM](https://github.com/reallyluckyy/IWXMVM)
by reallyluckyy and contributors, [Iris Studio](https://github.com/yerbisonline/IrisStudio)
and [T6Iris](https://github.com/Censoreeddd/T6Iris) (all GPL-3.0). Third-party
components: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Call of Duty and Modern Warfare are trademarks of Activision Publishing.
IW4Iris is not affiliated with Activision or with the IW4x team, and ships
no game files.
