# Roadmap

Each phase must build and test on its own. Status as of v0.1.0.

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

## Phase 5 — actors and scene creation · blocked on research

Data model and UI placeholders exist. Two routes:
* **GSC route** (proven by iw4cine): in a local private match, spawn
  `script_model`s, set models, play animations with
  `scriptModelPlayAnim`, attach weapons — driven from the timeline through
  IW4x's GSC function registration (`GSC::Script::AddFunction`).
  Deterministic seeking needs animation time control.
* **Native route**: client-side DObjs with XAnim trees; needs bindings
  IW4x does not declare yet (would be researched and added to IW4x's
  `Game::` with verification, not guessed).

## Phase 6 — advanced visuals · partly implemented

Done: film tweaks, DoF from the lens, sun (experimental), fog toggle,
lighting presets. **Next:** a post-process pass (lift / gamma / gain,
curves, LUT, grain, vignette) drawn before capture in the backend; green
screen and pass separation via entity / surface filtering; depth from the
depth buffer if IW4's renderer exposes it to D3D9 readback.

## Phase 7 — professional workflow · partly implemented

Done: graph editor, Blender and After Effects export, render queue, shot
presets, project management. **Next:** ripple editing, nested track groups,
3D gizmos, glTF camera export.

## Phase 8 — release engineering · partly implemented

Done: launcher, build / overlay / package scripts (Windows and Linux),
tests, documentation. **Next:** Windows CI (MSVC) building the release
package, in-game regression checklist runs per release.
