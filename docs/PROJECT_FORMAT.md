# The `.iw4iris` project format

UTF-8 JSON. Reader: `iris::ParseProject` (`core/src/project.cpp`); writer:
`iris::SerializeProject`. A project only *references* maps and demos (by
name and path); it never contains game files.

## Versioning

```json
{ "format": "iw4iris-project", "schemaVersion": 2, ... }
```

* `schemaVersion` is an integer. A reader accepts every version up to its
  own and migrates older documents on load (`LoadResult::migrated`); it
  refuses newer ones with an explanatory error.
* Version 0 (the pre-release prototype layout: `version`, a top-level
  `campath` with `tickMs`, `fps` as a number) migrates to 1.
* Version 2 (IW4Iris 0.2) adds `scene`. Version 1's top-level `actors`
  placeholders (body, head, animation, weapon, position, angles) become
  scene actors (the animation becomes the idle animation).
* Unknown members are ignored; unknown enum values fall back to a default
  and produce a warning; invalid numbers fall back to defaults and are
  clamped to their ranges.

## Units and conventions

* Times are **cinematic seconds** (the timeline), except `timeRemap`,
  which maps them to scene (demo) seconds.
* Positions are IW4 world units (inches), angles are `[pitch, yaw, roll]`
  in degrees with the engine's conventions (positive pitch looks down,
  positive yaw turns left).
* Frame rates are `[numerator, denominator]` (e.g. `[24000, 1001]`).
* Colours are `[r, g, b]` floats; marker / camera colours are `"#RRGGBB"`.

## Top level

| Member | Type | |
| --- | --- | --- |
| `format`, `schemaVersion` | string, int | see above |
| `generator`, `name`, `created`, `modified` | string | ISO 8601 UTC timestamps |
| `map` | string | the film set's map in Director Mode (`mp_highrise`) |
| `scene` | object | Director Mode and motion graphics objects, below |
| `demo` | object | `path` (relative to the project file when possible), `name`, `map`, `gametype`, `author`, `size` (bytes, for change detection), `sceneStart`; optional (0.5): `firstServerTime` (int, its first snapshot's server time once played), `duration` (seconds from its first to its last snapshot, once its index was read), `killFeed` (below) |
| `angles` | array | optional (0.5): other recordings of the main demo's match: `{id, name, demo (as above), recorder, offsetMs, sync, confidence}`. `offsetMs`: its server time minus the main demo's at the same moment; `sync`: `unsynced` / `suggested` / `manual` / `confirmed`; `confidence`: 0..1 of the suggestion it came from |
| `timeline` | object | `fps`, `start`, `end`, `in`, `out`, `loop {enabled,start,end}`, `display` (`timecode`/`seconds`/`frames`), `snap` |
| `timeRemap` | object | `timelineStart`, `sceneOffset`, `speed` (curve, default 1) |
| `cameras` | array | camera objects |
| `activeCamera` | string | id |
| `useCameraCuts`, `cuts` | bool, array | `{t, camera, angle?}`; `angle` (0.5): the id of the recording the cut shows, absent for the main demo |
| `markers` | array | `{id, t, name, color, kind}` |
| `tracks` | array | keyframed properties `{property, enabled, curve}` |
| `trackStates` | object | per track id: `{locked, hidden, collapsed, group}` |
| `visuals` | object | `film`, `dof`, `sun`, `fog`, `hud` blocks, each with `override` |
| `lights`, `actors` | array | stored for the planned lighting / actor systems |
| `recording` | object | recording settings (below) |
| `renderQueue` | array | `{id, name, camera, angle, status, message, settings}`; `angle` (0.5): the recording the job needs loaded, empty for the main demo |
| `export` | object | `unitsPerMetre`, `blenderKeysOnly`, `aeUnitsPerPixel` |
| `editor` | object | `layout` (Dear ImGui ini text), `workspace` |

### Kill feeds

`killFeed` keeps the kills a demo was seen to contain, for synchronising
angles: `{"seen": [[first, last], ...], "kills": [[serverTime, "killer",
"victim"], ...]}`. Times are the demo's own server times (ms); `seen` lists
the spans that were played (a kill outside them says nothing). An empty
killer is the world. Feeds are merged across plays, never invented.

## Curves

```json
{ "default": 1.0,
  "keys": [ { "t": 0.0, "v": 1.0, "interp": "smooth", "tangent": "auto",
              "ease": {"in": 0, "out": 0.5},
              "inHandle": [-0.25, 0.0], "outHandle": [0.25, 0.0] } ] }
```

`interp`: `hold`, `linear`, `smooth`, `catmullRom`, `bezier` (the outgoing
segment). `tangent`: `auto`, `aligned`, `free`, `flat`. Handles are
`[seconds, value]` relative to the key and are written for Bézier and
free / aligned keys. Property names: `film.brightness`, `film.contrast`,
`film.desaturation`, `dof.nearBlur`, `dof.farBlur`, `sun.intensity`.

## Cameras

```json
{ "id": "cam-1", "name": "Dolly 1", "mode": "dolly", "color": "#8B7CF6",
  "path": { "rotation": "quaternion", "constantSpeed": false,
            "keys": [ { "t": 0.0, "pos": [0,0,0], "ang": [0,0,0], "fov": 90,
                        "focus": 256, "fstop": 2.8, "interp": "smooth",
                        "ease": {"in": 0, "out": 0}, "tension": 0,
                        "inTangent": [0,0,0], "outTangent": [0,0,0] } ] },
  "position": [0,0,0], "angles": [0,0,0],
  "lens": { "focalLength": 35, "sensor": {"name": "Super 35", "width": 24.89, "height": 18.66},
            "fstop": 2.8, "focus": 256, "anamorphic": 1.0, "breathing": 0.0 },
  "useLensFov": false, "fov": 90,
  "target": { "kind": "player", "index": 3, "point": [0,0,0], "offset": [0,0,60] },
  "orbit": { "distance": 160, "height": 40, "speed": 20, "startAngle": 0, "shoulder": 0 },
  "shake": { "enabled": false, "position": 0.6, "rotation": 0.35, "frequency": 1.4, "seed": 1 } }
```

`mode`: `firstPerson`, `thirdPerson`, `free`, `dolly`, `orbit`, `follow`,
`static`, `playerEyes` (a demo player's eyes; 0.4), `chase` (behind a
player, turning with them; uses `orbit.distance`, `orbit.height`,
`orbit.startAngle` and `orbit.shoulder`; 0.4). Older versions read the two
new modes as `dolly` with a warning. `rotation`: `quaternion` or `euler`.
`fov` values are the true horizontal field of view of the output.
`target.kind`: `none`, `point`, `player`, `entity` (entity number). A player
target has `index` (the client slot when it was chosen) and, since 0.4,
`name`: the player is found by name, the slot only decides between two
players of the same name; without a name (older projects) the slot is
used. A camera on a player who is not in the demo at a moment has no
picture there (the recorded first-person view is shown instead), never
another player.

## Recording settings

```json
{ "format": "prores422hq", "useFfmpeg": false, "ffmpegPath": "", "outputDirectory": "",
  "namePattern": "{project}_{camera}_{pass}", "width": 0, "height": 0, "fps": [60, 1],
  "range": "inOut", "customStart": 0, "customEnd": 5, "crf": 16, "preset": "slow",
  "chroma444": false, "jpegQuality": 95, "queueDepth": 6, "warmupFrames": 4,
  "hideEditorOverlays": true, "passes": [ {"kind": "beauty", "enabled": true, "background": "#00FF00"} ] }
```

`format`: `png`, `jpeg`, `h264`, `h265`, `prores422`, `prores422hq`,
`prores4444`, `prores4444xq`, `ffv1`. `range`: `inOut`, `full`, `custom`.
`width`/`height` 0 = the game's resolution. Pass kinds: `beauty`,
`worldOnly`, `playersOnly`, `noWeapons`, `weaponsOnly`, `greenscreen`,
`depth`, `normals`, `objectId` (only `beauty` renders in v0.1).

## Scene (schema 2)

```json
"scene": {
  "filmSet": { "gametype": "dm" },
  "actors": [ { "id": "actor-1", "name": "Sniper", "character": "Ghillie Urban Sniper",
                "body": "mp_body_...", "head": "head_...", "idleAnim": "pb_stand_alert",
                "weapon": "cheytac_mp", "weaponTag": "tag_weapon_right", "camo": 0,
                "attachments": [ {"model": "m4_mp", "tag": "tag_stowed_back", "camo": 0} ],
                "transform": { "position": [x,y,z], "angles": [p,y,r], "scale": [1,1,1] },
                "track": { "x": <curve>, "y": <curve>, "z": <curve>, "pitch": ..., "yaw": ..., "roll": ... },
                "visibility": <curve>,
                "clips": [ {"id": "clip-1", "anim": "pb_walk_forward_mg", "start": 1.0, "duration": 2.0} ],
                "actions": [ {"kind": "walkTo", "duration": 2, "target": [x,y,z], "targetActor": "", "anim": "", "speed": 0} ],
                "actionsStart": 0.0, "visible": true, "locked": false, "group": "" } ],
  "props":   [ { "id", "name", "model", "transform", "track", "visibility", "attachActor", "attachTag", "visible", "locked", "group" } ],
  "effects": [ { "id", "name", "fx", "transform", "start", "repeat", "count", "attachActor", "attachTag", "enabled" } ],
  "graphics": [ { "id", "name", "kind": "text|logo|image|shape|model|anchor",
                  "text", "font", "textSettings": {size, letterSpacing, wordSpacing, lineSpacing, align, verticalAlign, layout, radius, kerning},
                  "separateWords", "source", "logoMode": "extruded|flat", "width", "alphaThreshold", "simplify", "smoothing",
                  "useLogoColors", "modelScale", "modelUp": "y|z", "shape", "shapeSize",
                  "geometry": {depth, bevel, bevelSegments, detail},
                  "materials": {front, side, bevel, back, separate}, "doubleSided",
                  "transform", "track", "opacity": <curve>,
                  "tracking": {mode: "world|actor|billboard|camera", actor, attach, inheritRotation, yawOnly, distance},
                  "renderMode": "depth|onTop|xray",
                  "animations": [ {preset, start, duration, stagger, intensity, direction, easing, seed, enabled} ],
                  "visible", "locked", "castShadow", "group", "parent" } ]
}
```

* Action kinds: `wait`, `walkTo`, `runTo`, `turnTo`, `faceActor`,
  `faceCamera`, `playAnimation`, `crouch`, `aimAt`, `enter`, `exit`,
  `followActor`. Directions are kept as the source; "Apply directions"
  writes `track` and `clips`.
* A material is `{preset, color, opacity, specular, gloss, metallic,
  emission, emissionStrength}`.
* Graphic `source` and `font` are paths, relative to the project file when
  possible. Fonts are referenced, never embedded.
* Asset names are engine names; a project shared with someone whose map
  lacks an asset opens with the asset reported as "not on this map" in the
  Director panel.

## Related files

* `<project>.iw4iris.autosave` — autosave, offered for recovery when newer
  than the project.
* IWXMVM / Iris Studio / T6Iris campath JSON can be imported into a dolly
  (File → Open, pick the `.json`).
