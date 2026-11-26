# Installing IW4Iris

## Requirements

* Windows 10 or 11.
* A Steam copy of Call of Duty: Modern Warfare 2 (2009) with
  [IW4x](https://iw4x.io/install) installed and working.
* Optional: [FFmpeg](https://ffmpeg.org/download.html#build-windows) for
  video formats (PNG and JPEG sequences work without it).

## Install

Download GitHub's **Code → Download ZIP** (it contains the prebuilt
launcher and client next to the source code; use the folder inside the
ZIP) or the release package `IW4Iris-v0.5.0-Windows-x86.zip`.

1. Unzip it into a folder of your choice
   (not inside the game folder). Keep its contents together:
   `IW4Iris Launcher.exe`, `client\`, `resources\`.
2. Run **IW4Iris Launcher.exe**. It needs no administrator rights.
3. **Setup**: the launcher lists the Modern Warfare 2 installations it found
   in your Steam libraries. Pick one (or type the folder that contains
   `iw4x.exe`). It must show "Valid IW4x installation".
4. Leave **Cinematic environment** at its default
   (`%LOCALAPPDATA%\IW4Iris\game`) unless you want it elsewhere, and click
   **Set up**.

The launcher now builds the cinematic environment:

* directory junctions to the game's data folders (`main`, `zone`, `iw4x`,
  ...) — the game reads them, nothing writes to them;
* copies (or hard links) of the small files of the game folder, such as
  `iw4x.exe`;
* the IW4Iris build of `iw4x.dll`;
* its own `players\`, `userraw\` and `iw4iris\` folders (settings,
  projects, recordings, logs). Your `players\*.cfg` are copied once if you
  leave that box ticked.

Your normal IW4x installation is not modified, and keeps working as before.

## FFmpeg (optional)

The launcher's **FFmpeg** page shows whether an `ffmpeg.exe` was found (on
`PATH` or in `<environment>\iw4iris\ffmpeg\`). To add one, download a
Windows build yourself (the page opens the official download page after you
tick the consent box) and either copy `ffmpeg.exe` to
`<environment>\iw4iris\ffmpeg\` or enter its path and click **Save FFmpeg
path**.

## First cinematic

1. **Home → Launch IW4Iris (offline)**.
2. Press **F1** in the game. The Getting Started window tracks the steps.
3. **Demos** panel: double-click a demo. Demos from your installation's
   demo folders are listed; the file is copied into the environment to play
   it, never modified.
4. Hold the **right mouse button** in the view and fly with **WASD**
   (Space / Ctrl up / down, Q / E roll, Shift faster, wheel zoom).
5. Move the playhead (click the timeline ruler) and press **K** to add a
   key at different times — at least three.
6. Double-click **Dolly 1** in the Scene panel to make it the active
   camera; press **Space** to preview.
7. **Ctrl+S** to save the project.
8. **Recording** panel: pick a format and click **Start recording**. Files
   go to `<environment>\iw4iris\recordings\`.

## Updating

**Update:** nothing to do. The launcher looks for a new version each time
it starts and installs it by itself (the launcher, the IW4Iris client, the
Director Mod, presets and documentation together), then restarts; the next
**Update & Launch** brings the game environment up to date. Updates come
only from this project's GitHub Releases and are checked against their
published SHA-256 before anything is replaced; a failed update puts every
file back. To decide yourself: launcher → **Setup → Updates**, untick
*Install new versions automatically* (an **Update now** button appears
instead) or *Look for a new version*. A package downloaded by hand can be
installed with `"IW4Iris Launcher.exe" --install-package <file.zip>`, or
unzipped over the old one. Projects, recordings and settings always stay.
A folder that is a git checkout is not updated by the launcher (use git).

## Uninstalling

Launcher → **Setup → Uninstall**. Only what the launcher created is removed;
the junctions are removed as links (the game folders they point to stay).
Tick "Also delete projects, recordings and exports" to remove those too.
Then delete the launcher's folder.

## Where things are

| What | Where |
| --- | --- |
| Projects | `<environment>\iw4iris\projects\` |
| Recordings | `<environment>\iw4iris\recordings\` |
| Exports (Blender / AE / CSV) | `<environment>\iw4iris\exports\` |
| Editor settings, layout | `<environment>\iw4iris\settings.json`, `imgui.ini` |
| Logs | `<environment>\iw4iris\logs\iw4iris.log` (previous run: `.previous`) |
| Launcher settings and log | `%LOCALAPPDATA%\IW4Iris\launcher.json`, `launcher.log` |
| Blender add-on | `<environment>\iw4iris\tools\blender\iw4iris_import.py` |
