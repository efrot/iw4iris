# Changelog

## 0.6.1 — 2026-11-27 (film sets keep to the standard game files)

### Fixed

* **A film set no longer inherits the mod of the last demo.** While a demo
  recorded on a modded server plays, IW4x switches to that server's mod
  and leaves it active; a film set started afterwards got the mod's weapon
  files, skins and menus, and its server log was written into the mod's
  folder in the game installation. Film sets now use the standard game
  files unless a mod is chosen in *Create new scene → Game files* (for
  custom camos or skins); the choice is saved with the project, and film
  sets write no server log.
* **Film sets no longer record demos of themselves.** IW4x records every
  session (`cl_autoRecord`); film sets filled the Demos list with
  recordings of themselves, and with a mod active those went into the
  game installation. Auto-recording is off while a film set runs and comes
  back afterwards (IW4Iris records with its own Recording panel).

## 0.6.0 — 2026-11-26 (automatic updates; the first release on GitHub Releases)

### Added

* **Automatic updates.** The launcher looks for a new version when it
  starts and installs it by itself: the launcher, the IW4Iris client, the
  Director Mod, presets and documentation together, then it restarts and
  says "Updated to IW4Iris x.y.z" (with *What's new*). The next **Update &
  Launch** brings the game environment up to date.
  * Updates come only from this project's GitHub Releases, over HTTPS (TLS
    1.2 or newer), and every redirect must stay on GitHub's release hosts.
  * The package must match the SHA-256 published with the release, and
    every file in it is checked before anything is written: nothing outside
    the IW4Iris folder can be written, links and junctions are never
    followed, and a package without the launcher, the client and the
    Director Mod is refused.
  * Files are replaced one by one with a backup; any failure (a file in
    use, a full disk) puts every file back, and an update interrupted by a
    crash or power cut is undone at the next start.
  * Projects, recordings and settings are never touched; files that are
    not part of the package are left alone; a git checkout is not updated.
  * *Setup → Updates*: look for updates at start (on), install them
    automatically (on), *Check now*; with automatic installing off, Home
    shows **Update now**. A package downloaded by hand installs with
    `--install-package <file.zip>`.
* Version tags publish the release (`.github/workflows/build.yml`, job
  *release*): the Windows package and `SHA256SUMS.txt` from the MSVC build,
  only when both build jobs pass and the tag matches `CMakeLists.txt`.

### Also in this release

Everything under 0.5.1 below: film sets load again (they stayed on the
loading screen for everyone), *Reload set*, film sets started after a
demo, the editor opening with the game, the readable Demos list, templates
dressed for the map, docked panels after an update.

## 0.5.1 — 2026-11-26 (film sets load again; first fixes found in the game)

### Fixed

* **Film sets stayed on the loading screen on every map** (reported by two
  users, reproduced in the game with 0.5.0). The offline guard dropped every
  packet passed to the engine's packet sender, including packets to the
  game's own local server: IW4x sends some of those (the client's stats
  while it connects) through that sender directly, so the client never
  joined its own film set. Local (loopback) packets now reach the engine;
  packets to network addresses are still dropped, and sockets still refuse
  any non-loopback destination. Help > Diagnostics shows "Local (loopback)
  packets: delivered".
* **Reload set** (after adding a weapon or model the map has not loaded)
  lost the film set with "Server connection timed out": the engine's map
  restart made the game reconnect to itself through a network address,
  which the offline guard blocks. Reload now leaves the level and loads the
  map again, the same way a new film set starts.
* **Creating a film set while a demo played** (a demo recorded on a modded
  server, as most community demos are) left a black screen: leaving the
  demo makes IW4x switch the mod folder back, and the map command queued
  behind it was lost. The film set now waits until the game has left the
  demo, then loads the map.
* The editor gave a film set up 10 s after its server was ready while the
  game was still loading the map; a joining game is now waited for (about
  a minute), then the film set fails with the stage it stopped at.
* Opening a demo that had been opened before failed with "File exists".

### Easier to use

* **Panels no longer pile up in a corner after an update.** A layout saved
  by an earlier version did not know the panels added since (Director,
  Object, Players, Moments, Angles, Music); they opened as floating windows
  a few pixels wide over the Scene and Demos panels (one letter per line).
  An outdated layout is now rebuilt once, a panel the layout has never seen
  docks beside its siblings, and floating panels keep a readable size.
* **Demos panel:** click a demo to select it, then **Play** (button under
  the list, Enter, or *Play* at the top of the right-click menu);
  double-click still works.
* **Help > Start screen** opens the start screen at any time; it used to
  close again at once while a demo or map was loaded.
* **The editor opens with the game** (start screen included), so nobody has
  to know about F1 first; *Settings > Open the editor when the game
  starts* turns it off, F1 still shows and hides it.
* **Demos are listed by map, mode, length and when they were recorded**
  ("Scrapyard · S&D · 3:14 · yesterday 21:05") instead of file names like
  `auto_1795633595`; sortable by any column; file name and label are
  optional columns (right-click a header), the tooltip has the author and
  the path. The search also finds map names.
* **Templates dress characters for the map:** every map loads the ghillie
  suits of all environments, and the Sniper showcase put an arctic ghillie
  on Afghan. Characters whose environment matches the map's own soldiers
  (desert, arctic, forest; urban for city maps) are chosen first.
* The game's spectator HUD is hidden in film sets (it stays in demos).
* **The template chosen in "Create a cinematic on a map" is built again.**
  It was built by the Director panel's drawing code, which does not run
  while that panel is a tab behind another one (the docked layout), so the
  map loaded empty. It is now built every frame once the game is in the
  level, the Object panel shows the new actor, and the Director tab comes
  to the front while the set loads.

### Build

* `tools\build.ps1` works on a stock Windows with only the free Visual
  Studio 2022 **Build Tools** (vswhere now searches every product, not just
  full Visual Studio) and in Windows PowerShell 5.1 (the IW4x patch check
  no longer stops the script there); the Python tests use the `py`
  launcher when it exists.

### Diagnostics

* iw4iris.log keeps the last five sessions (`.previous` to `.previous5`)
  instead of one, uses local time, and now includes the lines logged before
  the game's folder was known (offline guard, render clock, window).
* While a film set loads, every connection stage of the game, every packet
  the offline guard drops and a status line every 300 frames are logged;
  an active IW4x mod folder (`fs_game`) is logged.
* The launcher's Diagnostics list a copy of the Director Mod in the game's
  own `mods` folder (IW4Iris does not use it; choosing it in IW4x's Mods
  menu replaces the installed scripts).

## 0.5.0 — 2026-11-26 (Angles and BeatSync, not yet tested in the game)

### Added

* **BeatSync (music):** a Music panel and a Music row on the timeline with
  the waveform. WAV is read directly, MP3 / FLAC / OGG / M4A and others
  with FFmpeg. *Find beats* finds the tempo and beats locally
  (spectral-flux onsets, an autocorrelation tempo estimate with a clarity
  value, dynamic-programming beat tracking after Ellis 2007, implemented
  from the paper); beats are corrected on the row (drag, double-click to
  add, right-click to remove), x2 / /2, or a typed BPM grid from the
  playhead. Corrected beats are never replaced without asking. Beats are
  edit points (snapping, Up / Down).
* **Beat actions:** cuts on every N-th beat round chosen cameras, markers
  on beats, slow motion from a beat to a later one; all ordinary, editable
  content.
* **Music preview** along with the playhead (waveOut, mono mix; it follows
  seeks and stops while scrubbing). **Music in recordings:** in the video
  (AAC / PCM / FLAC by container) or `<name>_music.wav` next to an image
  sequence, through FFmpeg; per-angle sequences get one music file for the
  whole range. Project format: `audio`, `recording.includeAudio`.

* **Angles:** other players' demos of the main demo's match in one
  project. Each gives its recorder's exact first-person view to cut to,
  where a single demo only allows a reconstruction. Add them from the
  Demos panel (right-click) or the new **Angles** panel; a demo of another
  map or mode is refused.
* **Synchronisation:** an angle's offset against the main demo's server
  clock. *Suggest from kills* compares the kill feeds both recordings were
  seen to contain (the game's obituaries) and proposes the offset at which
  they agree, with the evidence (kills matched, kills only one shows in the
  time both cover) and a confidence. It is applied only on request, as a
  *suggestion* until confirmed; offsets can be set and nudged by hand.
  Kill feeds are kept in the project and merged across plays.
* **Switching:** *Open here* plays an angle at the playhead's moment of
  the match (the angle's clock is moved onto the main demo's, so the
  timeline, cuts and moments line up). Camera cuts carry their recording;
  new cuts belong to the recording playing; the timeline shows and edits a
  cut's recording; the toolbar says when the cut at the playhead is in a
  recording that is not playing and offers to open it.
* **Rendering across angles:** a recording refuses a range that needs a
  recording other than the one playing (it would show the wrong player's
  view). *Queue per angle* makes one render job per part, each opening its
  recording first; image sequences from all parts form one sequence.
* **Coverage:** each recording covers only the stretch its player
  recorded. Its span on the timeline is learnt when it is opened, shown in
  the Angles panel and as a red band on cuts it does not cover.
* **Render preflight:** cuts to angles that were removed, are not
  synchronised, come from another map or mode, or fall outside their
  recording's coverage block recording; an unchecked suggested sync and a
  recording not opened yet are warnings.
* Project format: `angles`, `demo.firstServerTime`, `demo.duration`, `demo.killFeed`,
  `cuts[].angle`, `renderQueue[].angle` (all optional; older files load
  unchanged).

### Fixed

* Saving a project while an angle played would have replaced the main
  demo with the angle (the project's demo was always set to the loaded
  file). The main demo is kept; only a project without angles takes the
  demo played as its main demo, as before.
* `tools/cross/msvc-lint.sh` only checked the component: the core library
  is its own premake project and was skipped. It checks both now (53
  files instead of 22).
* MSVC C4459 (a local hid a global) broke the Windows build since 0.4.0's
  render preflight; the lint now uses GCC's full `-Wshadow` like MSVC.

### Not verified

* Nothing of this has run in the game. Assumptions to check with real
  demos of one match: obituary events reach every client (so kill feeds
  match), recordings of one server session share the server clock.

## 0.4.0 — 2026-11-26 (Demo Director: players and perspectives, not yet tested in the game)

### Added

* **Players panel** for demos: everyone the demo records, by team, with
  what is known right now (in view, dead, not in view, not connected),
  search, favourites, previous / next (**[** / **]**), names over the
  players in the view (click to select, double-click to look through
  their eyes).
* **Player perspectives:** *their eyes* (the recording player's exact
  view; for everybody else reconstructed from position, stance and view
  angles, and labelled so), *behind them* (a chase camera that turns with
  the player: distance, height, shoulder, angle), follow and orbit. Each
  can be added as a camera cut at the playhead to build multi-player
  sequences.
* Players are followed by identity: through death and respawn, out of
  view, leaving and coming back; a reused slot is a different player, and
  two players of the same name are told apart. Cameras save the player's
  name (project format: `target.name`; new camera modes `playerEyes`,
  `chase`).
* When the shot camera has no picture (its player is not in the demo at
  that moment) the toolbar says so and a recording counts those frames,
  instead of silently showing something else.
* **Moments panel:** kills (from the game's own kill events), deaths,
  spawns, joins and leaves, filled in while the demo plays or by one
  analysis pass over the whole demo; click to jump, double-click a kill
  to clip it.
* **Render preflight** in the Recording panel: camera cuts to deleted
  cameras and dollies without keys block recording; cameras on players
  this demo does not have, single-key dollies and unusually fast camera
  moves are warnings with the time.
* **Trickshot candidates:** airborne kills with a turn or a long jump, with
  their evidence and a confidence (a local analysis of the recorded
  movement). *Clip* or *Make cinematic*: slow motion around the kill and
  chase / eyes cuts on the shooter, all editable.

### Fixed

* A camera aimed at the recording player found no target (their entity is
  not part of the snapshot); they are now resolved from the player state.
* A late background scan of a previous demo could replace the current
  demo's index (length, seek origin).

## 0.3.1 — 2026-11-26 (fixes from the first in-game reports, not yet tested in the game)

### Fixed

* **A film set stayed on the loading screen.** Pausing (or slowing) a demo
  set the engine's timescale near 0 and it was never given back, so a map
  started afterwards ran about 1000 times too slow. The timescale now
  belongs to demo control only while a demo plays, a film set always
  starts at normal speed, and the loading watchdog repairs it. Each step
  of a film set's start is written to `iw4iris.log`.
* "script compile error / bad syntax" when creating a scene (Director Mod
  0.3.1; see 0.3.0 F-35 in docs/BACKLOG.md).

### Added

* **The game fits between the panels.** With the editor shown (F1), the
  game picture is scaled, with its aspect ratio, into the free space
  between the panels instead of being hidden under them. Camera paths,
  the gizmo, guides and click selection follow the scaled picture;
  recordings are unaffected (full resolution, no editor). View → *Fit the
  game between the panels* switches back to the full-screen picture.

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
