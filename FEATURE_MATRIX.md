# Feature matrix

Status labels:

* **Verified** — implemented and covered by automated tests (game-independent code) or by the build.
* **Implemented** — implemented and compiled into the cinematic client, **not yet tested in the game**.
* **Experimental** — implemented, but known to depend on engine behaviour that is still unconfirmed.
* **Planned** — designed, not implemented; the project format already stores it where noted.
* **Blocked** — needs engine research or bindings that are not available yet.

Nothing marked Implemented or Experimental has been runtime-verified in IW4x by the authors yet.

## Core (game independent)

| Feature | Status | Notes |
| --- | --- | --- |
| Engine-convention angles / axes, quaternions, slerp, squad | Verified | tested against id Tech 3 `AngleVectors` |
| Keyframe curves: hold, linear, smooth (auto-clamped), Bézier handles, eases | Verified | |
| Camera paths: linear, smooth, centripetal Catmull-Rom, Bézier tangents, hold | Verified | |
| Quaternion and Euler (multi-turn) rotation | Verified | |
| Constant-speed traversal, reverse, retime, shift, smooth | Verified | |
| Lens: focal length ↔ FOV, sensors, anamorphic, `cg_fov` conversion, thin-lens DoF | Verified | |
| Rational frame rates, timecode, frame ranges | Verified | 23.976 / 29.97 / 59.94 exact |
| Speed ramps, slow motion (time remap) | Verified | |
| Camera cuts, markers, loop / in / out | Verified | |
| `.iw4iris` project format, migrations, atomic save, relative paths, autosave paths | Verified | |
| Undo / redo with merged steps | Verified | |
| IWXMVM / T6Iris campath import | Verified | |
| Recording validation, FFmpeg arguments, command-line quoting, disk estimate | Verified | |
| Exact per-frame clock steps | Verified | |
| Blender / After Effects / CSV exporters | Verified | Blender add-on tested against the exporter's output; AE script not run in AE |
| Demo metadata, record scanning | Verified | against IW4x's own Huffman coder and record layout; not against real demos |
| Shot presets (12) | Verified | keys stay editable |
| Deterministic shake | Verified | |

## Editor and engine integration

| Feature | Status | Notes |
| --- | --- | --- |
| Native IW4x component, cross-compiles and links | Verified (build) | MinGW-w64; MSVC build path scripted, not run |
| Offline guard (engine sends, Winsock, HTTP, session check) | Implemented | fail closed when the packet hook cannot be installed |
| Editor overlay toggle (F1), docking UI, theme, workspaces | Implemented | |
| Free camera (WASD, vertical, roll, speed, acceleration, smoothing, FOV wheel, gamepad) | Implemented | |
| Camera override through the view hook | Experimental | see COMPATIBILITY.md |
| Dolly cameras with keys, eases, tangents | Implemented | |
| Orbit, follow, static (look-at), game first / third person | Implemented | |
| Bone camera, weapon-mounted, projectile follow | Blocked | needs DObj bone bindings |
| Path / key / frustum overlay (never recorded) | Implemented | |
| 3D gizmos | Planned | keys are moved with "update from view" and the inspector |
| Multitrack timeline: keys, box select, drag, snap, zoom, pan, markers, cuts, speed | Implemented | |
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
| Actors, animations, weapons, attachments, camos | Blocked | data model stored; route: GSC script models or XAnim bindings |
| Props, effects placement, killstreak vehicles | Blocked | |
| Dynamic lights | Blocked | IW4 primary lights are baked per map |
| Lighting presets (sun + colour) | Implemented | |
| ReShade | Planned | to be checked with IW4x's D3D9Ex wrapper |
| Export: Blender, After Effects, CSV | Implemented | |
| glTF / FBX / Alembic | Planned | |
| Autosave, crash recovery files, recent projects | Implemented | |
| Missing-asset (demo) detection | Implemented | |
| Onboarding, shortcuts window, tooltips | Implemented | |

## Launcher

| Feature | Status | Notes |
| --- | --- | --- |
| Steam library detection, manual folder, validation | Implemented | rules unit-tested |
| Isolated environment (junctions, copies, own players / userraw) | Implemented | |
| Repair, uninstall (only what it created, never through junctions) | Implemented | |
| Launch offline, launch with a project | Implemented | |
| FFmpeg: locate, choose, open download page with consent | Implemented | no automatic download |
| Logs, projects, recordings | Implemented | |
| No administrator rights | Implemented | `asInvoker` manifest |
| Automatic updates | Not planned | deliberate: a verified client is never replaced silently |
