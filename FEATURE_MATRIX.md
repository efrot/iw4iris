# Feature matrix

Status labels (the same verification states as [docs/BACKLOG.md](docs/BACKLOG.md)):

* **Unit-tested** — game-independent logic covered by the automated tests (Linux CI job).
* **Windows-built** — compiled into the cinematic client by the Windows (MSVC) CI job and the MinGW cross build, **not yet run in the game**.
* **Implemented** — compiled into the cinematic client (MinGW cross build), **not yet run in the game**.
* **Experimental** — implemented, but depends on engine behaviour that is still unconfirmed.
* **Planned** — designed, not implemented; the project format already stores it where noted.
* **Blocked** — needs engine research or bindings that are not available yet.

Nothing in this matrix is **Runtime-tested** or **End-to-end verified** yet:
no IW4x installation is available to the development and CI environments.

## Core (game independent)

| Feature | Status | Notes |
| --- | --- | --- |
| Engine-convention angles / axes, quaternions, slerp, squad | Unit-tested | tested against id Tech 3 `AngleVectors` |
| Keyframe curves: hold, linear, smooth (auto-clamped), Bézier handles, eases | Unit-tested | |
| Camera paths: linear, smooth, centripetal Catmull-Rom, Bézier tangents, hold | Unit-tested | |
| Quaternion and Euler (multi-turn) rotation | Unit-tested | |
| Constant-speed traversal, reverse, retime, shift, smooth | Unit-tested | |
| Lens: focal length ↔ FOV, sensors, anamorphic, `cg_fov` conversion, thin-lens DoF | Unit-tested | |
| Rational frame rates, timecode, frame ranges | Unit-tested | 23.976 / 29.97 / 59.94 exact |
| Speed ramps, slow motion (time remap) | Unit-tested | |
| Camera cuts, markers, loop / in / out | Unit-tested | |
| `.iw4iris` project format, migrations, atomic save, relative paths, autosave paths | Unit-tested | |
| Undo / redo with merged steps | Unit-tested | |
| IWXMVM / T6Iris campath import | Unit-tested | |
| Recording validation, FFmpeg arguments, command-line quoting, disk estimate | Unit-tested | |
| Exact per-frame clock steps | Unit-tested | |
| Blender / After Effects / CSV exporters | Unit-tested | Blender add-on tested against the exporter's output; AE script not run in AE |
| Demo metadata, record scanning | Unit-tested | against IW4x's own Huffman coder and record layout; not against real demos |
| Shot presets (12) | Unit-tested | keys stay editable |
| Scene model: actors, clips, props, effects, graphics, evaluation at time t | Unit-tested | |
| Directions (wait, walk / run to, turn, face actor / camera, play animation, crouch, aim, enter, exit, follow) compiled to tracks | Unit-tested | deterministic, warnings for missing targets |
| Asset library: readable names and categories from engine asset names, body-head pairing, search | Unit-tested | against representative names; real lists come from the game |
| Shot generator (17 framed shot types from a subject) | Unit-tested | the subject lands in the frame centre at the lens' distance |
| Scene and title templates | Unit-tested | build ordinary editable content |
| Project schema 2 (scene section), schema 1 migration | Unit-tested | |
| 2D geometry: contours, nesting (even-odd / non-zero), earcut triangulation | Unit-tested | |
| Extrusion with round / chamfer bevels, oversize bevel reduction | Unit-tested | closed, outward-wound solids |
| Fonts: TrueType / OpenType (CFF) outlines, kerning, alignment, lines, vertical, circular text | Unit-tested | no complex-script shaping (one glyph per code point) |
| SVG logos (fills, fill rules, colours, transforms) | Unit-tested | strokes are not converted (warning); gradients become solid colours |
| Transparent PNG / JPEG / TGA / BMP tracing into silhouettes with holes | Unit-tested | WebP not supported |
| OBJ model import (polygons, normals, groups, MTL colours, limits) | Unit-tested | glTF / FBX planned |
| Graphic animation presets (18) and easings, seeded randomness | Unit-tested | same time, same pose |
| Graphic tracking: world, actor attach point, billboard, camera lens, parenting | Unit-tested (maths) | in-game bone reports unverified |
| View / projection maths for renderer, gizmos and picking | Unit-tested | both depth conventions |
| Deterministic shake | Unit-tested | |

## Editor and engine integration

| Feature | Status | Notes |
| --- | --- | --- |
| Native IW4x component, compiles and links | Windows-built | MinGW-w64 cross build and the MSVC CI job (client DLL built with MSVC since CI run 3) |
| Offline guard (engine sends, Winsock, HTTP, session check) | Implemented | fail closed when the packet hook cannot be installed |
| Editor overlay toggle (F1), docking UI, theme, workspaces | Implemented | |
| Free camera (WASD, vertical, roll, speed, acceleration, smoothing, FOV wheel, gamepad) | Implemented | |
| Camera override through the view hook | Experimental | see COMPATIBILITY.md |
| Dolly cameras with keys, eases, tangents | Implemented | |
| Orbit, follow, static (look-at), game first / third person | Implemented | |
| Bone camera, weapon-mounted, projectile follow | Blocked | needs DObj bone bindings |
| Path / key / frustum overlay (never recorded) | Implemented | |
| Viewport gizmo for scene objects (move along X / Y / Z, rotate, size; Ctrl snaps) | Implemented | camera keys are still moved with "update from view" and the inspector |
| Multitrack timeline: keys, box select, drag, snap, zoom, pan, markers, cuts, speed | Windows-built | |
| Timeline navigation: scrub on ruler or empty rows, magnet (Alt: off), typed times, in/out bar, overview scroll bar, row scrolling, follow, edit-point stepping | Windows-built | arithmetic unit-tested (`iris/timeline_view`) |
| Timeline fits a loaded demo's length | Windows-built | `FitTimelineToSource` unit-tested |
| Free view (navigation camera) separate from the shot; camera from view; move camera to view | Windows-built | flying never changes the project's cameras |
| Command palette (Ctrl+K) | Windows-built | fuzzy matching unit-tested |
| Start screen | Windows-built | |
| Diagnostics: capability states, guided self-test, redacted support bundle | Windows-built | model and redaction unit-tested |
| Graph editor: values, Bézier handles, tangent modes | Implemented | |
| Track lock / hide | Implemented | stored per track |
| Ripple editing, nested track groups | Planned | |
| Look: film tweaks, DoF (manual / from lens), HUD | Implemented | dvar presence detected at run time |
| Sun override | Experimental | live effect unconfirmed |
| Fog | Implemented (on/off only) | IW4 fog parameters come from map scripts |
| Lift / gamma / gain, curves, LUTs, grain, vignette, bloom controls | Planned | need a post-process pass |
| Reset to defaults (restore every changed dvar) | Implemented | |
| Demo browser: folders, search, favourites, labels, metadata | Implemented | |
| Demo playback, speed | Implemented | |
| Pause, frame step | Experimental | holds `clientStatic_t::realtime` |
| Forward seek | Experimental | advances `realtime` in steps |
| Backward seek | Experimental | restarts the demo and replays (checkpoint = first snapshot) |
| Snapshot-checkpoint rewinding | Planned | the record scanner already indexes snapshot offsets |
| Kill / event markers from demo data | Planned | |
| Recording: PNG / JPEG sequences (built-in) | Implemented | |
| Recording: H.264, H.265, ProRes 422 / HQ / 4444 / 4444 XQ, FFV1 (FFmpeg) | Implemented | needs FFmpeg |
| Deterministic offline clock | Experimental | self-verified before every capture |
| Frame-sync calibration | Implemented | |
| Render queue | Implemented | |
| Custom resolutions / above monitor resolution | Planned | needs off-screen rendering research |
| Temporal supersampling / motion blur | Planned | |
| Render passes: beauty | Implemented | |
| World only, players only, no weapons, weapons only, green screen | Blocked | need renderer research (entity / surface filtering) |
| Depth, normals, object ID, motion vectors | Blocked | need access to IW4's depth / G-buffer |
| Actors, animations, weapons, attachments, camos | Implemented | Director Mod (GSC script models); see *Director Mode* below |
| Props and effects placement | Implemented | Director Mode film sets; killstreak vehicles are planned |
| Dynamic lights | Blocked | IW4 primary lights are baked per map |
| Lighting presets (sun + colour) | Implemented | |
| ReShade | Planned | to be checked with IW4x's D3D9Ex wrapper |
| Export: Blender, After Effects, CSV | Implemented | |
| glTF / FBX / Alembic | Planned | |
| Autosave, crash recovery files, recent projects | Implemented | |
| Missing-asset (demo) detection | Implemented | |
| Onboarding, shortcuts window, tooltips | Implemented | |

## Director Mode (film sets)

| Feature | Status | Notes |
| --- | --- | --- |
| Film set: a map loaded as a local, offline listen server (no timer, score limit, countdown) | Implemented | not run in the game yet |
| Director Mod (GSC) installed into the environment by the launcher, inactive outside film sets | Implemented | `scripts/mp` loading is IW4x's; GSC not compiled here (no game) |
| Editor ↔ mod protocol: GSC builtins, requests, responses, capability report | Implemented | IW4x `Script::AddFunction`; docs/DIRECTOR_PROTOCOL.md |
| Asset discovery from the loaded fast files (models, animations, weapons, effects) | Implemented | `DB_EnumXAssets` |
| Precache manifest validated against the loaded assets; "Reload set" for new ones | Implemented | assets must be precached when the map starts (engine rule) |
| Map browser: installed maps, names, descriptions, search, favourites, recent | Implemented | preview images planned |
| Scene wizard (map, template, create) | Implemented | |
| Character library by faction, search, favourites, add where the camera looks | Implemented | |
| Actors: script-model body + linked head, transform / path, visibility | Implemented | |
| Animation clips (start, duration) and idle animation | Implemented | clips play from their start at engine speed; no scrubbing, offset, speed or blending for script models |
| Weapons on tags with hidden attachment parts, camo index, extra attachments | Implemented | |
| Directions applied to actors | Implemented | in-place animations moved along the path; foot sliding depends on the chosen speed |
| Props on the map or on an actor's bone | Implemented | |
| Effects: one-shot / repeating plays, on actor bones | Implemented | particles run on the engine's clock: not scrubbable |
| Click-to-select, outlines, actor paths in the view | Implemented | |
| Click-to-place / snap to ground (world trace) | Implemented | `G_TraceCapsule` with `CONTENTS_SOLID`, film sets only |
| Shots from an actor (17 types) | Implemented | |
| Composition guides: thirds, centre, safe areas, horizon, aspect masks | Implemented | never recorded |
| Beginner / professional mode | Implemented | professional shows raw names and advanced parameters |
| Scene objects on the timeline: clips (move / trim / delete), keys, graphic animations, effect plays | Implemented | |
| Recording film sets | Implemented | server frames are 50 ms: clip and effect starts snap to them |
| Bot actors, performance recording | Planned | bots give real movement animations but are not deterministic |
| Pose editor, IK, look-at constraints, bone overrides | Blocked | no verified bone-control path for script models |
| Vehicles / killstreak sequences, campaign maps and characters | Planned | |
| Dynamic / custom lights | Blocked | IW4 primary lights are baked per map |

## Motion graphics

| Feature | Status | Notes |
| --- | --- | --- |
| Native renderer: text, logos, shapes, models, image planes in world space | Implemented | D3D9 fixed function, drawn before the capture reads the frame |
| Frame sync: draw list matched to the presented frame's scene time | Implemented | engine source state at 0x6CAF080 (used by IW4x); reported in the panel |
| Engine view / projection when they match the frame's camera, else own | Implemented | validated per frame |
| Depth occlusion by world and characters | Experimental | only with a depth buffer that matches the back buffer and an occlusion probe confirms holds the scene; otherwise the panel says why and graphics draw on top |
| Render modes: depth tested, always on top, X-ray | Implemented | |
| Materials (19 presets, colour, specular, gloss, metallic tint, emission, opacity, per part) | Implemented | Blinn–Phong approximations, not physically based |
| Lighting of graphics | Implemented (own key light) | the sun override drives it; the map's lights, shadows and fog do not affect graphics |
| Depth of field, motion blur, colour grading on graphics | Blocked | graphics are drawn after the game's post-processing |
| Shadows cast by graphics | Planned | |
| Motion Graphics panel, cinematic title generator, camera orbit around a graphic | Implemented | |
| Graphics-only / mask / tracking-reference passes | Planned | |
| Native XModel assets via ZoneBuilder | Planned | for shadows / native lighting; needs a map reload per change |
| Audio waveform, beats, BPM | Planned | |

## Launcher

| Feature | Status | Notes |
| --- | --- | --- |
| Steam library detection, manual folder, validation | Implemented | rules unit-tested |
| Isolated environment (junctions, copies, own players / userraw) | Implemented | |
| Repair, uninstall (only what it created, never through junctions) | Implemented | |
| Setup rollback: a failed setup removes what it created; atomic manifest | Windows-built | not run on Windows yet |
| Install & Launch / Update & Launch (one click), quick actions | Windows-built | client and Director Mod compared with the package |
| Diagnostics page with support report (home folder and account name removed) | Windows-built | report model and redaction unit-tested |
| Launch offline, launch with a project | Implemented | |
| FFmpeg: locate, choose, open download page with consent | Implemented | no automatic download |
| Logs, projects, recordings | Implemented | |
| Director Mod: install, verify against the package, repair | Implemented | manifest paths restricted to relative `.gsc` files (unit-tested) |
| Third-party extensions | Planned | format documented (docs/EXTENSIONS.md); never enabled without explicit approval |
| No administrator rights | Implemented | `asInvoker` manifest |
| Automatic updates | Not planned | deliberate: a verified client is never replaced silently |
