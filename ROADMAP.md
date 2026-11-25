# Roadmap

Each phase must build and test on its own. Status as of v0.3.0 (Creator Preview).

## Phase 0 — research and feasibility · done

RESEARCH.md, ARCHITECTURE.md, COMPATIBILITY.md, FEATURE_MATRIX.md, this
file. IW4x revision pinned (`912099b7c405`).

## Phase 1 — engine integration · implemented, awaiting runtime verification

Native IW4x component, offline guard, editor overlay (F1), input routing,
logging, viewport, free camera. **Next:** run docs/TESTING.md §1–§3 in the
game; confirm the view hook (the first milestone: a working free camera).

## Phase 2 — cinematic camera MVP · implemented

Dolly cameras, keys, interpolation, timeline, project save / load.
**Next:** acceptance workflow (docs/TESTING.md §4) in the game.

## Phase 3 — demo editing · implemented (pause / seek experimental)

Demo browser, playback, speed, pause, frame step, seeking, timeline sync.
**Next:**
1. Verify pause / seek through `clientStatic_t::realtime`; if it is not
   clean, research the `msec` path of `Com_Frame` (as IWXMVM does in IW3).
2. Snapshot checkpoints for backward seeks: the record scanner already
   records the file offset of every snapshot; restoring needs the client
   state reset IWXMVM performs in IW3 (`CL_FirstSnapshot`, snapshot
   validity, server-command sequences) translated to IW4's structures.
3. Event markers: decode obituary entity events (delta-coded) from
   snapshots, or collect them live during playback.

## Phase 4 — recording · implemented

Back-buffer capture, image sequences, FFmpeg, ranges, clean footage,
deterministic clock, render queue. **Next:** verify in the game; render at
resolutions above the monitor (off-screen render target); temporal
supersampling.

## Phase 5 — Director Mode · implemented, awaiting runtime verification

The GSC route was chosen (the proven technique of MW2 cinematic mods,
re-implemented independently): the **IW4Iris Director Mod** owns the game
entities of a *film set* (a map loaded as a local offline session) and the
native editor drives it through GSC builtins registered with IW4x's
`Script::AddFunction` (docs/DIRECTOR_PROTOCOL.md).

* **Milestone A — game integration:** compiled against the pinned IW4x;
  in-game verification of camera override, editor, recording pending
  (docs/TESTING.md §1–6).
* **Milestone B — Director Mod proof of concept:** implemented: film set,
  spawn / move / animate / arm / delete / respawn actors. In-game run
  pending (docs/TESTING.md §8). **This is the gate for everything below.**
* **Milestone C — scene creator:** implemented: map wizard, character /
  animation / prop / effect libraries, placement, hierarchy, timeline
  rows, save / load / reconstruction.
* **Milestone D — advanced filmmaking:** implemented: directions, multiple
  actors, paths, props, effects, smart shots, templates. Planned: bot
  actors (real movement animation), vehicles, killstreak sequences,
  performance recording, pose editor.
* **Milestone E — production:** recording works for film sets (server-frame
  granularity for clip / effect starts). Planned: render passes beyond
  beauty, scene export to Blender (actors and graphics, not only camera).
* Native DObj / XAnim actors stay a research item (bindings IW4x does not
  declare).

## Phase 5b — Motion graphics · geometry verified, renderer awaiting runtime verification

* **M1 world-space geometry / M2 3D text / M3 logo importer:** geometry,
  fonts, SVG and transparent-image tracing are unit-tested; the D3D9
  renderer draws them in world space with runtime-verified depth and view
  (docs/MOTION_GRAPHICS.md). In-game check: docs/TESTING.md §9.
* **M4 tracking / M5 workflow:** implemented (actor bones via the Director
  Mod, billboard, lens-locked, parenting; panel, inspector, gizmo,
  presets, title templates).
* **M6 rendering integration:** clean beauty renders with graphics. Planned:
  graphics-only and mask passes, shadows, DoF on graphics (needs a render
  stage before post-processing), native XModel assets through ZoneBuilder.
* **M7 advanced:** OBJ import and procedural presets done; planned: glTF,
  audio waveform / beat markers, more materials.

## Phase 6 — advanced visuals · partly implemented

Done: film tweaks, DoF from the lens, sun (experimental), fog toggle,
lighting presets. **Next:** a post-process pass (lift / gamma / gain,
curves, LUT, grain, vignette) drawn before capture in the backend; green
screen and pass separation via entity / surface filtering; depth from the
depth buffer if IW4's renderer exposes it to D3D9 readback.

## Phase 7 — professional workflow · partly implemented

Done: graph editor, Blender and After Effects export, render queue, shot
presets, project management, viewport gizmo for scene objects, beginner /
professional mode, composition guides. **Next:** ripple editing, nested
track groups, command palette, glTF export.

## Phase 8 — release engineering · partly implemented

Done: launcher, build / overlay / package scripts (Windows and Linux),
tests, documentation. **Next:** Windows CI (MSVC) building the release
package, in-game regression checklist runs per release.
