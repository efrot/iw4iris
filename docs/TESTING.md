# Testing

## Automated (game independent)

```
tools/run-tests.sh
```

* `iw4iris_tests` — 115 test cases: math against id Tech 3's `AngleVectors`,
  curves, camera paths, lenses, timeline, time remapping, recording
  configuration and frame steps, project round trips / migrations / atomic
  saves, undo, rigs, shake, free camera, exporters, demo metadata and record
  scanning (with IW4x's Huffman coder), presets, the launcher's install
  rules and Director Mod manifest rules, the shipped samples, and for
  Director Mode / motion graphics: scene evaluation, directions, the asset
  library, shots, templates, project schema 2, contours, triangulation,
  extrusion and bevels (closed solids), fonts (Rubik), SVG and traced
  logos, OBJ import, projection maths, tracking and animation presets.
* `tests/python/test_blender_addon.py` — the Blender add-on's helpers,
  including an export written by the C++ exporter (run the C++ tests first).

The cinematic client, the launcher and the Win32 test binary are also built
by the cross build; a successful build is itself a check of every engine
binding the component uses against the pinned IW4x headers.

## In the game

None of the following has been run by the authors yet. Run them on Windows
with the release package and report the results (log + Diagnostics
screenshot) in the issue tracker. Each step names what it verifies.

### 1. Start-up and offline guard

1. Launch from the launcher. **Expect** the game to start normally.
2. Open the console (~): `iw4iris_status`. **Expect** "capture idle".
3. Open `<environment>\iw4iris\logs\iw4iris.log`. **Expect** "Offline guard
   installed: engine sends blocked, N Winsock and M HTTP imports guarded"
   and "Render clock: ... virtualised".
4. Try to join any server (server browser or `connect <ip>`). **Expect**
   no connection; if a connection starts, the log says "tried to connect to
   a network server" and the editor is disabled. *(fail-closed guard)*

### 2. Editor overlay and input

1. Press F1. **Expect** the editor (menu bar, toolbar, docked panels) with
   the game visible in the middle; the mouse moves an editor cursor; the
   game's view does not turn. *(RawMouse gating, input routing)*
2. Press F1 again. **Expect** normal game input.
3. Open the console with ~ while the editor is shown and type. **Expect**
   the console receives the keys.
4. `vid_restart` with the editor shown. **Expect** the editor to come back
   after the restart. *(device recovery)*

### 3. Free camera — the first milestone

1. Play a demo (Demos panel, double-click).
2. Hold the right mouse button over the game view; move the mouse and use
   WASD / Space / Ctrl / Q / E / Shift / wheel. **Expect** the view to fly
   freely. *(view hook: COMPATIBILITY.md, "Needs runtime verification")*
3. Release the button. **Expect** the view to stay where it is (the Free
   camera is now the active camera).
4. Note anything rendered from the wrong place (effects, the gun, model
   detail) — see "Known limitations".

### 4. Acceptance workflow (v0.1 release criteria)

1. Launch IW4Iris from the launcher.
2. The session is isolated and offline (step 1 above).
3. Load a demo.
4. Open the editor (F1).
5. Move the free camera.
6. Add at least three camera keys at different times (K).
7. Make "Dolly 1" active and preview (Space): **expect** a smooth path
   matching the overlay.
8. Save the project (Ctrl+S).
9. File → New project, then File → Open the saved project: **expect** the
   same keys and the demo to load.
10. Recording panel → PNG sequence → Start recording.
11. **Expect** an image sequence in `iw4iris\recordings\` and a
    `.capture.json` report with `"syncVerified": true`, `"unalignedFrames": 0`,
    `"clockDriftMs": 0`.
12. Compare the frames with the preview: the camera must follow the path.
    Export the camera (Export → Blender), import it in Blender with the
    add-on over the image sequence: **expect** the Blender camera to line up
    with the footage.

### 5. Demo control

1. Pause (Space) and step frames (→). **Expect** the scene to freeze and
   advance one frame per step. Note any jitter. *(realtime hold)*
2. Click far ahead on the ruler. **Expect** "seeking", then the demo at the
   new time. *(forward seek)*
3. Click back before the current time. **Expect** the demo to restart and
   arrive at the earlier time. *(restart-and-replay seek)*
4. Add playback-speed keys (clock button) with a dip to 0.25; play.
   **Expect** slow motion in the dip.

### 5a. Angles (two demos of one match)

Needs two players' demos of the same match (same map, same session).

1. Open demo A, play a few seconds, **Find all moments**. Right-click demo
   B in the Demos panel, **Add as an angle of this match**. Open B from the
   Angles panel (**Open here**), **Find all moments**.
2. **Suggest from kills**. **Expect** "Same server clock (+0.00 s)" or a
   small offset, with most kills matched. *(obituaries reach every client;
   one session shares the server clock)*
3. **Use**, then with B playing go to a kill marker of A: **expect** the
   kill to happen at the marker in B too. Nudge if it does not; note the
   offset needed.
4. **Cut to it here** on B, a cut back to A a few seconds later; play
   across the cut. **Expect** the toolbar to say the next cut is in the
   other recording, and **Open it** to land at the same moment.
5. Render queue: **Queue per angle**, **Render queue** with PNG.
   **Expect** one numbered sequence without gaps or duplicates, B's part
   showing B's own view.

### 6. Recording determinism

1. Record the same range twice at 60 FPS. **Expect** identical frame
   counts and visually identical frames.
2. Record at 24 and at 120 FPS. **Expect** frame counts of exactly
   range × FPS, and motion that plays at the same speed in both.
3. Record H.264 and ProRes 4444 with FFmpeg. **Expect** files that play;
   on failure the error shows FFmpeg's output.
4. Disconnect / `vid_restart` during a capture. **Expect** "failed" with a
   clear message, never a crash or a half-written file without a report.

### 7. Look

For each Look section, toggle Override and change values; **expect** a
visible change, and "Reset the game's look to defaults" to restore the
original look exactly. Record which dvars Diagnostics reports as missing.

### 8. Director Mode — Milestone B (the gate for scene creation)

1. Launcher → Setup: **expect** "Director Mod: installed and up to date".
2. In the game, F1 → Director → Create new scene → Highrise → Empty film
   set → Create. **Expect** the map to load, the Director panel to say
   "mp_highrise is ready" and the Director Mod log to show
   "precached ... models" and "Director Mod ready". *Verifies script
   loading from userraw, the builtins, the precache manifest.*
3. Characters panel: add a character. **Expect** a body with its head where
   the camera looks, standing in its idle animation. *Verifies spawn,
   head link, `scriptModelPlayAnim`.*
4. Move it with the gizmo (G, drag an axis) and by keys at two times; play.
   **Expect** smooth movement, no jitter at 60 FPS. *Verifies per-frame
   poses and the timing prediction.*
5. Animations panel: add a clip at the playhead. **Expect** it to play.
6. Object inspector → Weapon → Intervention. **Expect** the rifle in the
   right hand, attachments hidden. *Verifies `getWeaponModel`, tags,
   `hidePart`.*
7. Delete the actor, undo. **Expect** it to disappear and come back.
8. Save, quit the game, start again, open the project, Director → Reload or
   create the scene on the same map. **Expect** the same scene.
9. Report any "request failed" lines of the Director log.

### 9. Motion graphics — Milestones 1–3

1. In a film set (or a demo), Motion Graphics → Shape → Box. **Expect** a
   lit box where the camera looks. Fly around it: it stays fixed in the
   world (Test A).
2. Place it half inside a wall. Motion Graphics → Renderer **expect**
   "Depth occlusion: verified"; the wall hides the part behind it (Test
   C). If it says "unavailable: ...", report the text.
3. 3D Text "oGinStyle", orbit around it: front, back, sides, bevels, the
   holes of o / G (Test B).
4. Logo → an SVG and a transparent PNG with holes; extruded, metallic.
5. Cinematic title → "Montage title" with "Generate a camera orbit", record
   6 seconds: graphics in the frames, no editor UI (Test G).
6. Scrub back and forth over the title's fly-in: identical frames at the
   same time (Test E).
7. Floating player name on an actor's head, actor walking: the name follows
   (Test D).

### Extended (later releases)

* Rewind correctness: a frame reached by seeking backwards must equal the
  same frame reached by playing forwards (capture both and diff).
* Actor determinism: the same actor pose at the same time after any seek.
* Render-pass alignment: every pass of a frame shares camera, time and
  scene state (diff edges of the beauty and matte passes).
