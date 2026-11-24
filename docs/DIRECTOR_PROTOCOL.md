# Director protocol (version 1)

How the editor (native, `engine/iw4x/src/IrisDirector.cpp`) and the IW4Iris
Director Mod (GSC, `mods/iw4iris_director`) talk. Everything is local: the
mod runs in the film set's own listen server inside the same process, and
the calls are GSC builtins the cinematic client registers with IW4x's
`Components::GSC::Script::AddFunction`. Builtin names are case-insensitive
(IW4x lower-cases them).

## Lifecycle

1. The editor starts a film set: it sets `g_gametype`, removes the time and
   score limits and the pre-match countdown, and runs `map <name>`.
2. IW4x loads `scripts/mp/iw4iris_director.gsc` from the environment's
   `userraw` (installed by the launcher) and calls its `init()`.
3. `init()` returns at once unless `IW4IrisDirectorActive()` is 1, which it
   only is for a film set the editor started. Ordinary maps and demos in
   the cinematic copy are unaffected.
4. The mod precaches the manifest (`IW4IrisManifest()`) before its first
   wait, as the engine requires, starts its frame loop and calls
   `IW4IrisReady(capabilities)`.
5. Every server frame (50 ms): requests, poses, effects, bone reports.
6. "Reload set" runs `map_restart`: the level restarts, the mod precaches
   a new manifest (new assets of the scene), the editor respawns every
   object from the project.

## Builtins

| Builtin | Returns | Purpose |
| --- | --- | --- |
| `IW4IrisDirectorActive()` | int | 1 for an editor film set with the offline guard installed |
| `IW4IrisManifest()` | array of strings | `"model:<name>"`, `"anim:<name>"`, `"weapon:<name>"`, `"fx:<name>"` |
| `IW4IrisReady(<capabilities>)` | — | the mod is running (`"protocol 1 actors props effects tags"`) |
| `IW4IrisNextRequest()` | array of strings, empty when none | `[op, request id, arguments...]` |
| `IW4IrisRespond(<id>, <"ok" or "error">, <message>)` | — | answers a request |
| `IW4IrisRegister(<object>, <part>, <entity number>)` | — | diagnostics |
| `IW4IrisPoses(<server time>)` | flat array | `id, x, y, z, pitch, yaw, roll, visible` per object |
| `IW4IrisEffects()` | flat array | `fx, origin xyz, forward xyz, up xyz` per effect due |
| `IW4IrisTagRequests()` | flat array | `object id, tag` pairs to report |
| `IW4IrisReportTag(<object>, <tag>, x, y, z, pitch, yaw, roll)` | — | a bone's world transform |
| `IW4IrisLog(<message>)` | — | into the Director log (editor) and `iw4iris.log` |

## Requests

| Operation | Arguments | Effect |
| --- | --- | --- |
| `spawnActor` | object, body, head (may be empty) | script model body, head linked to `j_spine4` |
| `setWeapon` | object, weapon (empty: none), tag, camo | weapon world model on the tag, unused attachment parts hidden |
| `attach` | object, `"model"` or `"weapon"`, name, tag, camo | extra model on a tag |
| `clearAttachments` | object | removes the extra models |
| `anim` | object, animation | `scriptModelPlayAnim` on body and head |
| `spawnProp` | object, model | script model |
| `linkProp` / `unlinkProp` | object, actor, tag / object | attach a prop to an actor's bone |
| `delete` | object | removes every entity of the object |
| `clear` | — | removes every object |
| `ping` | — | answers `ok` with the protocol version |

Every request is answered. Failures arrive in the editor's Director panel
("request failed: ...") and the log.

## Safety rules

* Only assets the editor found in the loaded fast files and put in the
  manifest are sent; the mod precaches exactly the manifest. A missing
  asset never reaches `setModel`.
* Bone queries are limited to tags every multiplayer body has (`j_head`,
  `j_neck`, `j_spine4`, `j_mainroot`, `tag_weapon_right`,
  `tag_weapon_left`, `tag_stowed_back`), `tag_flash` of an attached weapon,
  and `origin`.
* The film set is a listen server on the loopback connection under the
  offline guard: packets to anything but the local client are dropped.

## Timing

Transforms are evaluated natively at the timeline time the server frame
will be shown at: `playhead + (server time - client time) * playback rate`.
Clients interpolate entities between server frames, so actors move
smoothly at any output frame rate. Animation and effect starts happen on
server frames (50 ms steps). Script-model animations cannot be set to a
time: after a seek they restart from the beginning when their clip is
reached.

## Status

Implemented and compiled; not yet run in the game. See docs/TESTING.md for
the in-game acceptance procedure.
