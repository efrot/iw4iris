# The `.iw4iris` project format

UTF-8 JSON. Reader: `iris::ParseProject` (`core/src/project.cpp`); writer:
`iris::SerializeProject`. A project only *references* maps and demos (by
name and path); it never contains game files.

## Versioning

```json
{ "format": "iw4iris-project", "schemaVersion": 1, ... }
```

* `schemaVersion` is an integer. A reader accepts every version up to its
  own and migrates older documents on load (`LoadResult::migrated`); it
  refuses newer ones with an explanatory error.
* Version 0 (the pre-release prototype layout: `version`, a top-level
  `campath` with `tickMs`, `fps` as a number) migrates to 1.
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
| `map` | string | map for scene work (`mp_rust`) |
| `demo` | object | `path` (relative to the project file when possible), `name`, `map`, `gametype`, `author`, `size` (bytes, for change detection), `sceneStart` |
| `timeline` | object | `fps`, `start`, `end`, `in`, `out`, `loop {enabled,start,end}`, `display` (`timecode`/`seconds`/`frames`), `snap` |
| `timeRemap` | object | `timelineStart`, `sceneOffset`, `speed` (curve, default 1) |
| `cameras` | array | camera objects |
| `activeCamera` | string | id |
| `useCameraCuts`, `cuts` | bool, array | `{t, camera}` |
| `markers` | array | `{id, t, name, color, kind}` |
| `tracks` | array | keyframed properties `{property, enabled, curve}` |
| `trackStates` | object | per track id: `{locked, hidden, collapsed, group}` |
| `visuals` | object | `film`, `dof`, `sun`, `fog`, `hud` blocks, each with `override` |
| `lights`, `actors` | array | stored for the planned lighting / actor systems |
| `recording` | object | recording settings (below) |
| `renderQueue` | array | `{id, name, camera, status, message, settings}` |
| `export` | object | `unitsPerMetre`, `blenderKeysOnly`, `aeUnitsPerPixel` |
| `editor` | object | `layout` (Dear ImGui ini text), `workspace` |

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
  "orbit": { "distance": 160, "height": 40, "speed": 20, "startAngle": 0 },
  "shake": { "enabled": false, "position": 0.6, "rotation": 0.35, "frequency": 1.4, "seed": 1 } }
```

`mode`: `firstPerson`, `thirdPerson`, `free`, `dolly`, `orbit`, `follow`,
`static`. `rotation`: `quaternion` or `euler`. `fov` values are the true
horizontal field of view of the output. `target.kind`: `none`, `point`,
`player` (client number), `entity` (entity number).

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

## Related files

* `<project>.iw4iris.autosave` — autosave, offered for recovery when newer
  than the project.
* IWXMVM / Iris Studio / T6Iris campath JSON can be imported into a dolly
  (File → Open, pick the `.json`).
