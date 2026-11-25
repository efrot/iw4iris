# Extensions

IW4Iris extensions are folders with a `mod.json` manifest. The one shipped
today is the Director Mod (`mods/iw4iris_director`). The format is designed
so that template packs, preset packs, camera paths and further GSC mods can
follow without changing the filmmaking workflow.

## Manifest

```json
{
  "format": "iw4iris-extension",
  "id": "iw4iris_director",
  "name": "IW4Iris Director Mod",
  "version": "0.3.0",
  "author": "IW4Iris contributors",
  "license": "GPL-3.0-or-later",
  "protocol": 1,
  "requiresIW4Iris": ">=0.3.0",
  "requiresCapabilities": ["gsc-builtins:iw4iris", "script-model-animation"],
  "operations": ["spawnActor", "..."],
  "install": { "target": "userraw", "files": ["scripts/mp/iw4iris_director.gsc", "scripts/iw4iris/director.gsc"] },
  "assets": "none: uses only assets of the maps the user has installed"
}
```

| Field | Meaning |
| --- | --- |
| `id`, `name`, `version`, `author`, `license` | identity |
| `protocol` | Director protocol version the scripts speak |
| `requiresIW4Iris`, `requiresCapabilities` | what the client must provide |
| `operations` | the requests the extension answers |
| `install.target` | only `userraw` (the environment's raw folder) is accepted |
| `install.files` | relative `.gsc` paths; absolute paths, `..`, drive letters and other file types are refused (unit-tested) |
| `assets` | included assets; extensions must not contain game files |

## Rules

* The launcher installs only the extension shipped in its own package, and
  verifies every installed file against it ("Repair / update" restores
  them).
* Third-party extensions are **planned**: they will never run without the
  user enabling each one explicitly, after seeing its manifest (scripts,
  capabilities, operations). Nothing downloaded is executed automatically.
* One unified Director Mod is loaded at a time; extensions add templates,
  presets and operations through it rather than stacking unrelated mods.
* Extensions never contain proprietary game assets.
