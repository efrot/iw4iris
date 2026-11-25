# IW4Iris — Cinematic Studio for IW4x

A native cinematic editor and offline renderer for **Call of Duty: Modern
Warfare 2 (2009)** on the [IW4x](https://github.com/iw4x/iw4x-client)
client. You get keyframed cameras over your demos, a multitrack timeline
with a graph editor, speed ramps, lenses and depth of field, colour
grading, and frame-exact recording to image sequences, ProRes, H.264 or
H.265. It runs inside the game, and it is made for trickshot montages and
cinematics.

> **Offline only.** The cinematic build blocks all network traffic and
> refuses to connect to servers. Use it for demos and local scenes. Your
> normal IW4x installation stays as it is, and you keep playing online with
> that one.

> **Status: v0.2.0 preview, not yet tested in the game.** The editor, the
> camera system, demo control, the recorder, Director Mode (actors, props,
> effects on film sets) and native motion graphics (3D text and logos) are
> implemented, and the cinematic client builds. None of the engine
> integration has been run in IW4x by the authors yet. The game-independent
> code (scene, directions, shots, geometry, fonts, logos, ...) is covered by
> 115 automated tests.
> [FEATURE_MATRIX.md](FEATURE_MATRIX.md) lists exactly what is verified,
> implemented, experimental or planned. [docs/TESTING.md](docs/TESTING.md)
> shows how to help verify it.

---

## Contents

1. [Features](#1-features)
2. [Requirements](#2-requirements)
3. [Installation tutorial](#3-installation-tutorial)
4. [Your first cinematic: step by step](#4-your-first-cinematic-step-by-step)
   - [4a. Director Mode: a scene from scratch](#4a-director-mode-a-scene-from-scratch)
   - [4b. Motion graphics: 3D titles and logos](#4b-motion-graphics-3d-titles-and-logos)
5. [The editor](#5-the-editor)
6. [Cameras](#6-cameras)
7. [Timeline, keys and the graph editor](#7-timeline-keys-and-the-graph-editor)
8. [Demos](#8-demos)
9. [Look: colour, depth of field, sun, HUD](#9-look-colour-depth-of-field-sun-hud)
10. [Recording](#10-recording)
11. [Export to Blender and After Effects](#11-export-to-blender-and-after-effects)
12. [Shot presets](#12-shot-presets)
13. [Projects and where your files are](#13-projects-and-where-your-files-are)
14. [Keyboard shortcuts](#14-keyboard-shortcuts)
15. [Console commands](#15-console-commands)
16. [Updating and uninstalling](#16-updating-and-uninstalling)
17. [Troubleshooting](#17-troubleshooting)
18. [FAQ](#18-faq)
19. [Building from source](#19-building-from-source)
20. [Kurzanleitung auf Deutsch](#20-kurzanleitung-auf-deutsch)
21. [Documentation index](#21-documentation-index)
22. [Credits and licence](#22-credits-and-licence)

---

## 1. Features

| Area | What you get |
| --- | --- |
| **Cameras** | Free camera (WASD, vertical movement, roll, speed, acceleration, smoothing, mouse-wheel zoom, gamepad). Dolly cameras with keys: smooth, linear, Catmull-Rom, Bézier or hold per key, ease in / out, constant speed, quaternion or multi-turn Euler rotation. Orbit, follow and tripod (look-at) rigs with player targets. Deterministic handheld shake. Camera cuts. |
| **Lenses** | Focal lengths from 10 to 300 mm on real sensor sizes (Full Frame, Super 35, APS-C, MFT, Super 16, 65 mm), anamorphic squeeze, aperture and focus distance, thin-lens depth-of-field readout, depth of field driven by the lens. |
| **Timeline** | Keys on tracks, box selection, dragging with frame snapping, markers, camera cuts, a playback-speed track for slow motion and speed ramps, keyframeable look properties, a graph editor with Bézier handles, undo / redo, in / out points, loop region. |
| **Demos** | A browser for IW4x demos with metadata (map, mode, length, author), search, favourites and labels. Playback speed, pause, frame stepping and seeking. |
| **Look** | Film tweaks (brightness, contrast, desaturation, light and dark tint), depth of field, sun (experimental), fog on / off, hide HUD / gun / crosshair, lighting presets, and a reset that restores every setting IW4Iris changed. |
| **Recording** | PNG / JPEG sequences with no extra software. H.264, H.265, ProRes 422 / HQ / 4444 / 4444 XQ and lossless FFV1 through FFmpeg. 23.976 to 240 FPS or any custom rate. A virtual clock times every output frame exactly, however long it takes to render. Recordings are always clean (no editor, no overlays). Render queue. |
| **Compositing** | Camera export to Blender (importer add-on included) and After Effects (a script that builds the camera), plus CSV. |
| **Shot presets** | Twelve presets such as trickshot orbit, slow-motion jump, killcam transition, scope close-up, weapon reveal and rooftop establishing. Each one builds ordinary keys you can edit. |
| **Projects** | `.iw4iris` files (versioned JSON) you can share. They reference maps and demos, never game files. Autosave, recent projects, IWXMVM / T6Iris campath import. |
| **Director Mode** | Build a scene from scratch on any installed map: a scene wizard, characters from the map by faction, animation clips, weapons and attachments, simple directions ("walk to", "turn towards", "aim at", ...), props, effects, 17 framed shots from an actor, scene templates. Click to select, move / rotate / size gizmo, composition guides, beginner and professional mode. |
| **Motion graphics** | Extruded 3D text (any TrueType / OpenType font, kerning, multiple lines, vertical or circular), logos from SVG or transparent PNG (extruded or flat), image planes, primitives, OBJ models from Blender. Materials, bevels, tracking (fixed, on an actor's bone, facing the camera, locked to the lens), 18 animation presets, title templates and a one-click camera orbit. Drawn into the game frame and recorded with the shot. |
| **Launcher** | Finds your IW4x installation, builds an isolated cinematic copy (links to the game data, nothing in your install is changed), installs the Director Mod into it, launches it offline, repairs and uninstalls. No administrator rights. |

Not in v0.2 (see [ROADMAP.md](ROADMAP.md)): bot actors and performance
recording, pose editing, vehicles and killstreak sequences, dynamic lights
beyond the sun, shadows and depth of field on graphics, render passes
other than the normal image (green screen, depth, masks), bone cameras,
recording above the game resolution, true rewinding, kill markers read from
demos, audio sync, ReShade. The editor marks these as unavailable instead
of showing controls that do nothing.

---

## 2. Requirements

* Windows 10 or 11, 64- or 32-bit.
* **Call of Duty: Modern Warfare 2 (2009)** from Steam (Microsoft Store
  copies do not work with IW4x).
* **IW4x** installed in that game folder and working. Get it from
  https://iw4x.io/install. The folder must contain `iw4x.exe`, `main\` and
  `zone\`.
* A drive with NTFS (normal for Windows drives) and about 50 MB free for
  IW4Iris, plus room for your recordings.
* Optional: **FFmpeg** for video formats. PNG and JPEG sequences work
  without it.

---

## 3. Installation tutorial

### Step 1: install IW4x (skip this if it already works)

1. Install Modern Warfare 2 in Steam.
2. Install IW4x as described on https://iw4x.io/install.
3. Start IW4x once normally to make sure it runs. Then close it.

### Step 2: unzip IW4Iris

IW4Iris comes ready to run in two ways; both contain the launcher:

* **From GitHub:** on the repository page click the green **Code** button,
  then **Download ZIP**. The ZIP holds one folder (named after the
  repository and branch) with `IW4Iris Launcher.exe` at its top, next to
  `client\` and `resources\`, plus the source code.
* **The release package** `IW4Iris-v0.2.0-Windows-x86.zip`: only the
  program, documentation, samples and presets.

1. Download one of them.
2. Right-click the ZIP, then **Extract All...** and choose a folder
   **outside** the game folder, for example `C:\Tools\IW4Iris`.
3. Open the extracted folder (for the GitHub ZIP, the folder inside it).
   `IW4Iris Launcher.exe` is right there. Keep the files together: the
   launcher needs `client\` and `resources\fonts\` next to it.

   ```
   IW4Iris Launcher.exe     <- start this
   client\iw4x.dll          (+ sentry-crash.exe, sentry-wer.dll)
   resources\fonts\ ...
   mods\iw4iris_director\   the Director Mod (installed by the launcher)
   docs\  samples\  presets\  README.md  LICENSE  ...
   ```

   The Blender add-on is `resources\tools\blender\iw4iris_import.py` in
   the release package and `tools\blender\iw4iris_import.py` in the GitHub
   ZIP. `PREBUILT-SHA256SUMS.txt` in the GitHub ZIP lists the checksums of
   the binaries.

### Step 3: set up the cinematic environment

1. Double-click **IW4Iris Launcher.exe**. It does not need administrator
   rights. If Windows SmartScreen asks, choose *More info* and then *Run
   anyway* (the program is not signed).
2. Click **Setup** on the left.
3. Click **Detect installations**. Your Modern Warfare 2 folder appears in
   the list; click it. If it is not found, type the folder that contains
   `iw4x.exe` into **Game folder**, for example
   `C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare 2`.
4. Check that the launcher shows **"Valid IW4x installation"** in green. If
   it lists something as missing, fix your IW4x installation first.
5. Leave **Cinematic environment** at its default,
   `%LOCALAPPDATA%\IW4Iris\game`, unless you want it somewhere else (any
   NTFS folder outside the game folder).
6. Leave **Copy my game settings** ticked if you want your graphics and
   key settings in the cinematic copy.
7. Click **Set up**. The log shows what was linked and copied, and the
   status says **"Ready. Your normal IW4x installation was not changed."**

What setup did:

* It created **directory junctions** to the game's data folders (`main`,
  `zone`, `iw4x`, ...). The game reads them; nothing writes to them.
* It **copied** (or hard-linked) the small files of the game folder, such
  as `iw4x.exe`.
* It installed the **IW4Iris build of `iw4x.dll`**.
* It created its **own** `players\`, `userraw\` and `iw4iris\` folders for
  settings, projects, recordings and logs.
* It installed the **Director Mod** scripts into its own `userraw\scripts\`
  (the Setup page shows "Director Mod: installed and up to date"). They only
  run when the editor starts a film set.

### Step 4: FFmpeg (optional, for video formats)

1. In the launcher, open **FFmpeg**. It shows whether an `ffmpeg.exe` was
   found (on `PATH` or in `<environment>\iw4iris\ffmpeg\`).
2. To add one: tick the consent box and click **Open the FFmpeg download
   page**. Download a Windows build (for example a "release essentials"
   build) and unzip it.
3. Either copy `ffmpeg.exe` into
   `%LOCALAPPDATA%\IW4Iris\game\iw4iris\ffmpeg\`, or type its full path into
   the **ffmpeg.exe** box and click **Save FFmpeg path**.

IW4Iris never downloads anything by itself.

### Step 5: launch

1. Click **Home**, then **Launch IW4Iris (offline)**.
2. The game starts from the cinematic copy. Press **F1** in the game to
   show the editor. The **Getting Started** window walks you through the
   first steps.

To check that everything works, open the game console with **~** and type
`iw4iris_status`; it answers with the recorder's state. The log file
`%LOCALAPPDATA%\IW4Iris\game\iw4iris\logs\iw4iris.log` starts with
"Offline guard installed".

---

## 4. Your first cinematic: step by step

This is the full workflow from demo to video file.

1. **Load a demo.** Press **F1**. In the **Demos** panel (left), double-click
   a demo. The list shows the demos from your installation's demo folders;
   add more folders at the bottom of the panel. The demo file is copied
   into the cinematic copy to play it and never modified.
2. **Fly.** Hold the **right mouse button** over the game view and move the
   mouse. Use **W A S D** to move, **Space** / **Ctrl** for up and down,
   **Q** / **E** to roll, **Shift** for faster, **Alt** for slower and the
   **mouse wheel** to zoom. Release the button and the view stays where you
   are. Press **F** to keep flying without holding the button; **F** or
   **Esc** stops it again. Flying looks through a separate *navigation
   camera*: the banner **FREE VIEW** shows that you are not looking through
   the shot, and your cameras are not changed. **V** (or the toolbar's
   *back to the shot* button) switches back; pressing **Space** to play
   does it too. While in the free view, the toolbar's **+** button creates
   a camera from the view and the arrows button moves the selected camera
   there.
3. **Pick moments.** Click or drag the ruler (or empty space) in the
   **Timeline** (bottom panel) to move the playhead, or type a time such as
   `1:05` into the time field. Press **Space** to play or pause the demo.
4. **Add keys.** At each moment you want, fly to a good spot and press
   **K** (this also works while you fly). Add at least three keys at
   different times. Each key appears as a
   diamond on the *Dolly 1* track and as a point of the path drawn in the
   view.
5. **Preview.** Double-click **Dolly 1** in the **Scene** panel to make it
   the active camera (or pick it in the toolbar's camera box). Move the
   playhead back and press **Space**: the camera follows the path.
6. **Refine.** Click a key on the timeline and change it in the
   **Inspector** (right): time, position, angles, field of view, focus,
   aperture, interpolation, ease in / out. Fly somewhere and press **U** to
   move the selected key to the current view. Drag keys along the timeline
   to change their timing. Tick **Constant speed** for an even camera move.
7. **Slow motion (optional).** Click the clock button in the timeline's
   header to add playback-speed keys, then select the *Playback speed* track
   and press **G** to edit the curve: drag a key down to 0.25 for quarter
   speed.
8. **Look (optional).** In the **Look** panel, tick **Override** for colour
   or depth of field and adjust. Click the diamond next to a slider to
   animate it.
9. **Save.** Press **Ctrl+S** and give the project a name. Projects are
   saved in `iw4iris\projects\`.
10. **Set the range.** Move the playhead to the start and press **B**, then
    to the end and press **N**.
11. **Record.** Open the **Recording** panel (toolbar's red button). Choose a
    format (PNG sequence needs nothing else; ProRes or H.264 need FFmpeg)
    and a frame rate, and check that there are no red messages. Click
    **Start recording**. The game first seeks to the start of the range,
    then renders frame by frame; it is slower than real time on purpose, so
    every frame is exactly timed.
12. **Collect the result.** When it says *done*, click **Open folder**. Next
    to the frames or the video there is a `.capture.json` report; it should
    say `"syncVerified": true`.

---

## 4a. Director Mode: a scene from scratch

No demo needed: Director Mode loads a map as a private, offline *film set*
and lets you place characters, props, effects and graphics on it.

1. **F1** → **Director** panel (or the **Director** workspace) → **Create
   new scene**.
2. **Map:** pick an installed map (search, star favourites; recent maps come
   first). **Template:** "Empty film set", "Sniper showcase", "Trickshot
   sequence", "Two-character confrontation", "Squad introduction", ...
   **Create scene.** The map loads (a few seconds); the Director panel says
   *ready*, and the template is built where the camera looks.
3. **Characters** panel: double-click a character (grouped by faction:
   Task Force 141, Rangers, Navy SEALs, Spetsnaz, Militia, OpFor, Ghillie
   suits) to place it where the camera looks.
4. Click the character in the view to select it. **G** move, **R** rotate,
   **S** size, drag the gizmo handles (hold **Ctrl** to snap). **F** frames
   it with the free camera.
5. **Object** panel (inspector): character, **Weapon** (e.g.
   *Intervention*), idle animation, **Animate (key here)** to key its
   position at the playhead, **Snap to ground**, **Face the camera**.
6. **Animations** panel: pick an animation and click **Clip** to add it at
   the playhead (it plays right away), or **Idle**.
7. **Directions** (Object panel): chain "Stand still", "Walk to a point",
   "Turn towards another actor", "Aim at a point" ... and click **Apply
   directions**; they become movement keys and animation clips you can
   still edit on the timeline.
8. **Shots** (Object panel): choose *Close-up*, *Orbit*, *Low-angle hero*,
   ... and **Create shot**: a framed camera, made active.
9. **Props & Effects** panel: place crates, barrels, vehicles, smoke, fire
   ... ; props can sit on an actor's bone.
10. Record as usual (section 10).

Good to know:

* Only what the map itself loads is offered (characters, animations,
  weapons, props, effects differ per map). Assets you use for the first time
  appear after **Reload set** in the Director panel (the engine loads assets
  when a map starts).
* Animations start from their beginning when their clip is reached; they
  cannot be scrubbed. Effects (particles) run on the game's clock.
* The film set is a local listen server under the offline guard; nobody can
  join and nothing is sent.

## 4b. Motion graphics: 3D titles and logos

1. **Motion Graphics** panel (or workspace).
2. **3D Text**, **Logo ...** (SVG or a transparent PNG), **Image ...**,
   **3D Model ...** (OBJ), **Shape** or **Tracking anchor**. New graphics
   face the camera where it looks.
3. Or the **Cinematic title** box: type the text, pick a template
   ("Montage title", "Player introduction", "Team introduction", ...),
   tick **Generate a camera orbit**, **Create title**.
4. **Object** panel: text, font (shipped Rubik, your Windows fonts or **...**
   to import), size, spacing, alignment, layout (straight, vertical, on a
   circle); depth and bevel; **Material** (matte, brushed / polished metal,
   chrome, gold, glass, neon, ...); **Visibility** (hidden behind walls and
   characters, always on top, X-ray); **Follows** (fixed, an actor's bone
   such as the head, facing the camera, the lens); **Animation** presets
   (fly in, typewriter, assemble, pop in, float, rotate, pulse, glitch, ...)
   with start, duration and stagger.
5. Graphics are part of the frame and are recorded; outlines and gizmos
   are not.

The **Renderer** section of the panel tells you whether occlusion by walls
and characters is available on your system ("verified") or why not; when
not, graphics are drawn on top. Graphics have their own key light (the Look
panel's sun override drives it): the map's shadows, fog and depth of field
do not apply to them.

---

## 5. The editor

Press **F1** to show or hide it. The game keeps running in the middle; the
panels are docked around it and can be moved, resized, tabbed or floated by
dragging their title bars. **View → Reset layout** puts them back.

| Panel | What it is for |
| --- | --- |
| **Toolbar** (top) | save, undo / redo, transport (frame back, play / pause, frame forward), preview speed, current time, active camera, add key, fly mode, record and progress, current demo or map |
| **Scene** (left) | cameras (add, make active, duplicate, add cut, delete), markers, keyed property tracks |
| **Demos** (left) | demo browser |
| **Inspector** (right) | the selected camera: mode, lens, path options, the selected key, rig and target, shake |
| **Look** (right) | colour, depth of field, sun, fog, HUD |
| **Timeline** (bottom) | ruler, markers, cuts, camera keys, playback speed, property tracks, graph editor |
| **Director**, **Characters**, **Animations**, **Props & Effects**, **Motion Graphics**, **Object** | Director Mode and motion graphics (sections 4a and 4b) |
| **Recording**, **Render Queue**, **Export**, **Shot Presets**, **Lighting**, **Settings**, **Diagnostics**, **History** | as named; open them from **View** |

**Workspaces** (icons in the menu bar, or the **Workspace** menu) switch
between panel sets: Cinematic (demo editing), Timeline, Cameras, Director,
Animation, Motion Graphics, Lighting, Environment, Effects, Recording,
Render Queue and Settings.

**Settings** has the UI scale, the accent colour, free-camera speed,
acceleration, smoothing, roll speed, mouse sensitivity, autosave, the
timeline's frame rate and time display (timecode, seconds or frames), and
frame snapping.

The camera path, its keys and the selected key's view frustums are drawn
over the game view. They are drawn after the frame was captured, so they
never appear in recordings. Turn them off in **View**.

---

## 6. Cameras

A project has any number of cameras. The **active camera** is the one you
see; with **Use camera cuts** enabled, the cuts on the timeline switch
between cameras over time.

| Mode | Use |
| --- | --- |
| **Game view** (first person) | the demo's own view, unchanged (the default) |
| **Third person** | the game's third-person camera; range and angle adjustable |
| **Free** | a shot flown by hand (flying itself never changes the active camera: it uses the free view, **V**) |
| **Dolly** | the keyframed path, the main shot camera |
| **Orbit** | circles a player or a point: distance, height, speed, start angle, roll |
| **Follow** | keeps an offset to a player or point and looks at it |
| **Static** | a tripod: fixed position, optionally looking at a target |

**Lens.** Tick *Field of view from lens* to work in millimetres on a real
sensor; the horizontal FOV follows. Aperture and focus distance set the
depth of field; the Inspector shows the sharp range (thin-lens model,
1 unit = 1 inch). *Look → Depth of field → From the camera's lens* makes
the game's blur follow it.

**Path options (dolly).** Constant speed, quaternion rotation (shortest
turn) or Euler rotation (allows full spins over several keys), reverse,
smooth, slower / faster, start at the playhead, clear.

**Key interpolation.** *Smooth* (automatic tangents that never overshoot),
*Linear*, *Catmull-Rom*, *Bézier* (with editable in / out tangents), *Hold*
(a cut to the next key). *Ease in / out* slows the camera into and out of a
key; *Tension* tightens a smooth key.

**Shake.** Handheld shake with position and rotation amplitude, frequency
and a seed. It is computed from the timeline time, so the same frame always
shakes the same way, also in recordings.

---

## 7. Timeline, keys and the graph editor

* **Move the playhead:** click or drag the ruler, or anywhere on empty
  track space. The playhead sticks to keys, markers, cuts, clip edges and
  in / out while you drag (*Magnet*; hold **Alt** to move freely). Or type a
  time into the time field at the top left: `12.5` (seconds), `1:05`
  (minutes:seconds), `90f` (frames) or `00:00:12:30` (timecode). **↑ / ↓**
  and the arrow buttons jump to the previous / next edit point. In a demo
  the picture follows while you drag forward; dragging backwards restarts
  the demo, so that happens when you pause on a spot or let go.
* **Look around:** mouse wheel zooms around the mouse, **Shift+wheel** or
  middle-drag scrolls sideways, **+ / -** zoom, **\\** or the expand button
  shows everything. The bar at the bottom shows the whole timeline: drag
  its bright box to scroll, drag its ends to zoom, click to jump. With many
  tracks, scroll the rows with the wheel over the track names (or
  Ctrl+wheel). *Follow* keeps the playhead in view while playing.
* **Demo length:** when a demo is loaded the timeline covers all of it
  (it used to stay at 10 seconds). *Length* changes it; content dragged
  past the end extends it.
* **Select:** click a key; Ctrl+click toggles; Shift+click adds; drag in an
  empty part of a track for a box selection.
* **Edit:** drag keys to retime them (snapped to frames when *Snap* is on);
  right-click for interpolation, ease and delete; Ctrl+C / Ctrl+V copy and
  paste keys at the playhead; Del deletes; double-click a key to jump to it.
* **Markers:** M adds one at the playhead; drag to move; right-click in the
  Scene panel to rename or set the kind (kill, trickshot, cut, music).
* **Camera cuts:** the scissors button adds a cut to the active camera at
  the playhead; right-click a cut to change its camera or delete it.
* **Playback speed:** the clock button adds a speed key; the track shows
  the speed curve. Speed 1 is real time, 0.5 is half speed.
* **Graph editor (G):** select a property track or the playback-speed
  track in the track names, then drag keys in time and value, drag Bézier
  handles, and right-click for interpolation, tangent mode (auto, aligned,
  free, flat), add or delete.
* **In / out (what is rendered):** the bar under the ruler. Drag its ends
  to set in and out, drag the middle to move the range, drag on empty
  space to draw a new one, double-click it for the whole timeline,
  right-click for more. **I / O** set in / out at the playhead (B / N also
  work), or type them into the *In* / *Out* fields. *Loop* repeats the
  loop region while playing (right-click the bar: *Loop this range*).

---

## 8. Demos

* Demos are listed from the folders at the bottom of the **Demos** panel
  (your installation's demo folders are added at setup). Search by name,
  map, mode or author; star favourites; right-click to add a label.
* **Play:** double-click. **Pause / play:** Space. **Step:** ← / → (Shift:
  one second). **Speed:** the toolbar's speed slider (and the playback-speed
  track).
* **Seek:** click the ruler. Forward seeks skip ahead quickly. Backward
  seeks restart the demo and play it forward to the target, which is exact
  but takes as long as the game needs to read the demo up to there.
* Pause and seek are **experimental** in v0.1 (they work through the
  client's demo clock).

---

## 9. Look: colour, depth of field, sun, HUD

Each block only changes the game when its **Override** is ticked.

* **Colour (film tweaks):** brightness, contrast, desaturation, light tint,
  dark tint, invert.
* **Depth of field:** from the camera's lens, or manual near / far ranges;
  near and far blur, bias.
* **Sun (experimental):** colour, intensity, elevation, direction.
* **Fog:** on or off (IW4 fog distances come from the map).
* **HUD:** hide the HUD, the gun in cinematic cameras, the crosshair.
* **Lighting presets** (Lighting panel): golden hour, blue hour, moonlight,
  warm sunset, cold winter, dark and moody, high contrast.
* **Reset the game's look to defaults** restores every setting IW4Iris
  changed to its original value.

A control whose engine setting does not exist in your game says so;
**Help → Diagnostics** lists what was found.

---

## 10. Recording

| Setting | Notes |
| --- | --- |
| **Format** | PNG / JPEG sequence (built in, or through FFmpeg), H.264, H.265, ProRes 422, 422 HQ, 4444, 4444 XQ, FFV1 lossless |
| **Frame rate** | 23.976, 24, 25, 29.97, 30, 50, 59.94, 60, 120, 240 or custom |
| **Resolution** | the game's resolution; set it in the game options before recording |
| **Range** | in / out points, the whole timeline, or custom |
| **Quality** | CRF and preset for H.264 / H.265, 4:4:4 chroma, JPEG quality |
| **Output** | folder (default `iw4iris\recordings\`) and name pattern: `{project}`, `{camera}`, `{pass}`, `{date}`, `{fps}`, `{w}`, `{h}` |
| **Advanced** | frame queue (memory vs speed), warm-up frames, FFmpeg path |

How it works: the recorder seeks to the start of the range, switches the
game to a virtual clock that advances by exactly one output frame per game
frame, measures the renderer's delay with a few warm-up frames, then reads
every frame before the editor is drawn. The game waits whenever the encoder
is behind, so the output is exactly timed even on slow machines.

The panel shows the frame count, a disk-space estimate and any problems
before you start. Each recording writes a `.capture.json` report (frames,
latency, sync check, clock drift) and, for FFmpeg formats, a `.ffmpeg.log`.

The **Render Queue** records several jobs one after another, each with its
own settings and camera.

---

## 11. Export to Blender and After Effects

**Export** panel → choose the format → **Export**. The shot's camera is
sampled at every frame of the recording range, so it matches a recording
of the same range frame for frame. Files go to `iw4iris\exports\`.

* **Blender:** install `resources\tools\blender\iw4iris_import.py`
  (`tools\blender\` in the GitHub ZIP) (Edit →
  Preferences → Add-ons → Install), then File → Import → **IW4Iris camera
  (.iw4cam.json)**. You get a camera with position, rotation, lens, focus
  and f-stop keyed on every frame, and the scene's frame rate, resolution
  and frame range set.
* **After Effects:** File → Scripts → **Run Script File...** and pick the
  `.jsx`. It creates a composition with a 3D camera (position, point of
  interest, roll, zoom, focus). Import your recorded sequence into it.
* **CSV:** frame, time, position, angles, FOV, focus and f-stop per frame.

Coordinate systems and details: [docs/EXPORT.md](docs/EXPORT.md).

---

## 12. Shot presets

**Shot Presets** panel: pick a preset, a subject (what the view looks at,
or a player), a duration, and click **Build shot**. The shot starts at the
playhead; slow-motion presets centre their ramp on the next marker.

Presets: smooth cinematic introduction, sniper scope close-up, Intervention
weapon showcase, third-person trickshot orbit, rooftop establishing shot,
slow-motion jump sequence, killcam transition, player-to-player move,
dramatic weapon reveal, over-the-shoulder, character close-up, map
flythrough. Everything a preset creates is ordinary keys, speed keys and
markers that you can edit.

---

## 13. Projects and where your files are

* **File → New / Open / Save / Save as / Save a copy**, **Open recent**.
  Projects autosave every 60 s to `<project>.iw4iris.autosave` (configurable).
* A project stores cameras, keys, cuts, markers, look settings, recording
  settings, the render queue, the editor layout and a reference to its
  demo. It never contains game files, so you can share it; whoever opens it
  needs the same demo (or map).
* **File → Open** also imports IWXMVM / Iris Studio / T6Iris campath files
  (`.json`) into a dolly.
* A sample project is in `samples\`.

| What | Where |
| --- | --- |
| Cinematic environment | `%LOCALAPPDATA%\IW4Iris\game\` (default) |
| Projects | `...\game\iw4iris\projects\` |
| Recordings | `...\game\iw4iris\recordings\` |
| Exports | `...\game\iw4iris\exports\` |
| Editor settings, layout | `...\game\iw4iris\settings.json`, `imgui.ini` |
| Log | `...\game\iw4iris\logs\iw4iris.log` (previous run: `iw4iris.log.previous`) |
| Launcher settings and log | `%LOCALAPPDATA%\IW4Iris\launcher.json`, `launcher.log` |
| FFmpeg (optional) | `...\game\iw4iris\ffmpeg\ffmpeg.exe` |

---

## 14. Keyboard shortcuts

| Key | Action |
| --- | --- |
| **F1** | show / hide the editor |
| **Right mouse** (hold) | fly the free camera |
| **F** | toggle flying (**Esc** also stops it) |
| **W A S D** | move |
| **Space / Ctrl** (flying) | up / down |
| **Q / E** | roll |
| **Shift / Alt** | faster / slower |
| **Mouse wheel** (flying) | zoom (field of view) |
| **K** | add a camera key from the view (also while flying) |
| **U** | update the selected key from the view |
| **Space** | play / pause |
| **← / →** | previous / next frame (Shift: one second) |
| **Home / End** | go to the in / out point (Shift: timeline start / end) |
| **↑ / ↓** | previous / next edit point (key, marker, cut, clip, in / out) |
| **I / O** (or B / N) | set the in / out point at the playhead |
| **+ / -** | zoom the timeline in / out |
| **\\** | show the whole timeline |
| **Alt** (while dragging) | ignore the magnet |
| **M** | add a marker |
| **V** | free view (navigation camera) / back to the shot camera |
| **Del** | delete the selected keys |
| **Ctrl+C / Ctrl+V** | copy / paste keys |
| **Ctrl+Z / Ctrl+Y** (or Ctrl+Shift+Z) | undo / redo |
| **Ctrl+S / Ctrl+O / Ctrl+N** | save / open / new project |
| **C** | next camera |
| **1 – 9** | make camera 1 – 9 active |
| **G** | graph editor |
| **?** | list of shortcuts |
| **G / R / S** (object selected) | move / rotate / size gizmo |
| **F** (object selected) | frame the object with the free camera |
| **Ctrl+D / Del / Esc** (object selected) | duplicate / delete / deselect |
| **~** | game console (works while the editor is shown) |

Gamepad while flying: left stick moves, right stick looks, triggers move up
and down, left stick click is faster.

---

## 15. Console commands

Open the game console with **~**.

| Command | Action |
| --- | --- |
| `iw4iris_toggle` | show / hide the editor |
| `iw4iris_key` | add a camera key from the view |
| `iw4iris_save [file]` | save the project (to its file, or to `file`) |
| `iw4iris_open <file>` | open a project |
| `iw4iris_record` | start recording with the project's settings |
| `iw4iris_status` | print the recorder's state |
| `iw4iris_resetlook` | restore every setting IW4Iris changed |

The launcher's **Projects** page can start the game with a project
(`+iw4iris_open "<file>"`).

---

## 16. Updating and uninstalling

**Update:** unzip the new version (over the old one or anywhere), start the
launcher and click **Setup → Repair / update**. Projects, recordings and
settings stay.

**Uninstall:** launcher → **Setup → Uninstall**. Only what the launcher
created is removed; the junctions are removed as links, and the game
folders they point to are left alone. Tick *Also delete projects,
recordings and exports* to remove those too. Then delete the launcher's
folder.

---

## 17. Troubleshooting

Look at `iw4iris\logs\iw4iris.log` first, and at **Help → Diagnostics** in
the editor (offline guard, engine state, the settings the editor found,
the log). The most common problems:

| Problem | What to do |
| --- | --- |
| No "Valid IW4x installation" | pick the folder that contains `iw4x.exe`, `main\` and `zone\`; install IW4x first |
| "client\iw4x.dll is missing" | keep the release package's files together |
| "Could not link main" | choose an environment folder on an NTFS drive |
| F1 does nothing | start the game from the launcher; check the log for "offline guard" |
| "tried to connect to a network server" | expected: the cinematic copy never goes online; restart it, and play online with your normal IW4x |
| The view does not change | make a cinematic camera active (a Dolly with keys, or Free); if it still does not change, please report it with your log |
| Smoke disappears at some angles | known: the game culls effects against the player's view |
| A Look control says "unavailable" | your game lacks that setting; see Diagnostics |
| "needs FFmpeg" | install FFmpeg (step 4) or record PNG / JPEG |
| "Custom render sizes are not available yet" | set the game resolution instead |
| "Freeze frames cannot be rendered offline yet" | raise the playback speed above 0 in the range |
| "Frame sync NOT verified" | check the first frames of the recording, and report it |
| Recording is slow | expected; output timing is still exact. Lower the frame queue at 4K if memory runs out |
| "The Director Mod did not start on this map" | launcher → Setup → **Repair / update**; check the console (~) for a script error and report it |
| A character / animation is missing in a film set | that map does not load it, or it appears after **Reload set** |
| "Depth occlusion: unavailable ..." | your system presents frames without a usable depth buffer; graphics are drawn on top. Report the text |

More: [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## 18. FAQ

**Does IW4Iris change my game or my IW4x installation?** No. The launcher
only reads the game folder. The cinematic copy links to the game data and
has its own client, settings and folders.

**Can I play online with it?** No, deliberately. The cinematic build blocks
all network traffic and disconnects if it ever connects to a server. Play
online with your normal IW4x.

**Can I get banned?** IW4Iris never connects to servers, so it cannot
affect anyone's game. It contains no cheats and no anti-cheat bypass.

**Which demos work?** IW4x demos (`.dm_13`, with their `.dm_13.json`). Older
retail or Steam demos depend on IW4x's own support for them.

**Do I need FFmpeg?** Only for video formats. PNG and JPEG sequences are
written by IW4Iris itself.

**Can I share projects?** Yes. Send the `.iw4iris` file; the other person
needs the same demo or map.

**Is it tested?** The game-independent parts are covered by automated
tests, and the client builds. The in-game integration has not been tested
yet; see [docs/TESTING.md](docs/TESTING.md) if you want to help.

---

## 19. Building from source

```
git clone --recursive https://github.com/efrot/iw4iris.git
```

* **Windows (Visual Studio 2022):**
  `powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Configuration Release -Package`
* **Linux (MinGW-w64 cross build):** `tools/cross/build-all.sh`
* **Tests only:** `tools/run-tests.sh`

The script checks out the pinned IW4x revision, installs the IW4Iris
component into it, builds it with IW4x's own build, builds the launcher and
tests with CMake, and packages `dist\IW4Iris-v0.2.0-Windows-x86.zip`.
Requirements and details: [BUILD.md](BUILD.md).

---

## 20. Kurzanleitung auf Deutsch

**Voraussetzungen:** Windows 10/11, Call of Duty: Modern Warfare 2 (2009)
über Steam, darin IW4x installiert und lauffähig (https://iw4x.io/install).
Optional FFmpeg für Videoformate.

**Installation**

1. Auf der GitHub-Seite **Code → Download ZIP** klicken (oder das
   Release-Paket `IW4Iris-v0.2.0-Windows-x86.zip` nehmen) und **außerhalb**
   des Spielordners entpacken (z. B. `C:\Tools\IW4Iris`). Im entpackten
   Ordner (beim GitHub-ZIP im Ordner darin) liegt **IW4Iris Launcher.exe**
   direkt oben, daneben `client\` und `resources\`. Die Dateien zusammen
   lassen.
2. **IW4Iris Launcher.exe** starten (keine Administratorrechte nötig; bei
   SmartScreen „Weitere Informationen" → „Trotzdem ausführen").
3. Links auf **Setup** → **Detect installations** → den MW2-Ordner
   anklicken (oder den Ordner mit `iw4x.exe` eintippen). Es muss
   **„Valid IW4x installation"** erscheinen.
4. **Cinematic environment** auf dem Standard lassen
   (`%LOCALAPPDATA%\IW4Iris\game`) und **Set up** klicken. Deine normale
   IW4x-Installation wird dabei nicht verändert.
5. Optional **FFmpeg**: `ffmpeg.exe` nach
   `%LOCALAPPDATA%\IW4Iris\game\iw4iris\ffmpeg\` kopieren oder den Pfad im
   Launcher eintragen.
6. **Home** → **Launch IW4Iris (offline)**.

**Erste Kamerafahrt**

1. Im Spiel **F1** drücken; im Panel **Demos** eine Demo doppelklicken.
2. **Rechte Maustaste** halten und mit **WASD** fliegen (Leertaste/Strg
   hoch/runter, Q/E rollen, Shift schneller, Mausrad Zoom). **F** schaltet
   Dauerfliegen ein, **F** oder **Esc** wieder aus.
3. Auf dem Zeitlineal klicken, um die Zeit zu wählen, und an mindestens drei
   Zeitpunkten mit **K** einen Kamera-Key setzen (geht auch im Flug).
4. Im Panel **Scene** auf **Dolly 1** doppelklicken und mit **Leertaste**
   abspielen.
5. **Strg+S** speichert das Projekt.
6. Mit **B** und **N** Anfang und Ende festlegen, im Panel **Recording**
   ein Format wählen und **Start recording** klicken. Die Dateien landen in
   `%LOCALAPPDATA%\IW4Iris\game\iw4iris\recordings\`.

**Eigene Szene ohne Demo (Director Mode)**

1. **F1** → Panel **Director** → **Create new scene** → Map wählen (z. B.
   Highrise) → Vorlage wählen (z. B. „Sniper showcase“) → **Create scene**.
2. Panel **Characters**: Figur doppelklicken, sie erscheint dort, wo die
   Kamera hinschaut. Anklicken, mit **G/R/S** bewegen, drehen, skalieren.
3. Panel **Object**: Waffe (z. B. *Intervention*), Animation, Regie-Aktionen
   („Walk to a point“, „Aim at a point“ …) → **Apply directions**; unter
   **Shots** eine Einstellung erzeugen (Close-up, Orbit …).
4. Panel **Props & Effects** für Kisten, Fahrzeuge, Rauch, Feuer.

**3D-Titel und Logos (Motion Graphics)**

Panel **Motion Graphics**: **3D Text**, **Logo ...** (SVG oder transparentes
PNG) oder **Cinematic title** mit Vorlage und „Generate a camera orbit“.
Material, Fase, Animation und Tracking (z. B. Name über dem Kopf eines
Actors) im Panel **Object**.

**Wichtig:** Die Cinematic-Version ist **nur offline** nutzbar und blockiert
jeden Netzwerkverkehr. Version 0.2.0 ist eine Vorschau und wurde im Spiel
noch nicht getestet; Probleme bitte mit der Logdatei
`iw4iris\logs\iw4iris.log` melden.

**Deinstallieren:** Launcher → **Setup** → **Uninstall**. Entfernt nur, was
der Launcher angelegt hat; der Spielordner bleibt unberührt.

---

## 21. Documentation index

| Document | Contents |
| --- | --- |
| [INSTALL.md](INSTALL.md) | installation (short version) |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | problems and fixes |
| [FEATURE_MATRIX.md](FEATURE_MATRIX.md) | what is verified, implemented, experimental, planned |
| [COMPATIBILITY.md](COMPATIBILITY.md) | IW4x revision, every engine binding, limitations |
| [ROADMAP.md](ROADMAP.md) | what comes next |
| [ARCHITECTURE.md](ARCHITECTURE.md) | how it is put together |
| [RESEARCH.md](RESEARCH.md) | analysis of T6Iris, IWXMVM and IW4x |
| [BUILD.md](BUILD.md) | building from source |
| [docs/PROJECT_FORMAT.md](docs/PROJECT_FORMAT.md) | the `.iw4iris` format |
| [docs/EXPORT.md](docs/EXPORT.md) | Blender and After Effects |
| [docs/TESTING.md](docs/TESTING.md) | automated tests and the in-game test procedure |
| [docs/DIRECTOR_PROTOCOL.md](docs/DIRECTOR_PROTOCOL.md) | how the editor drives the Director Mod |
| [docs/MOTION_GRAPHICS.md](docs/MOTION_GRAPHICS.md) | 3D text, logos, rendering, tracking |
| [docs/EXTENSIONS.md](docs/EXTENSIONS.md) | the extension manifest (`mod.json`) |
| [CHANGELOG.md](CHANGELOG.md) | version history |
| [DEVELOPMENT_STATUS.md](DEVELOPMENT_STATUS.md) | where development stands |
| [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) | third-party components and licences |

---

## 22. Credits and licence

IW4Iris is free software under the GNU GPL v3 or later ([LICENSE](LICENSE)).
It is built into the [IW4x client](https://github.com/iw4x/iw4x-client)
(GPL-3.0) and owes its design to [IWXMVM](https://github.com/reallyluckyy/IWXMVM)
by reallyluckyy and contributors, [Iris Studio](https://github.com/yerbisonline/IrisStudio)
and [T6Iris](https://github.com/Censoreeddd/T6Iris) (all GPL-3.0). It uses
Dear ImGui (MIT), nlohmann/json (MIT), stb_image_write (MIT / public
domain), and the Rubik and Font Awesome Free fonts (SIL OFL 1.1); see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Call of Duty and Modern Warfare are trademarks of Activision Publishing.
IW4Iris is not affiliated with Activision or with the IW4x team, and ships
no game files.
