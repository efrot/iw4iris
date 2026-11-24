# Third-party notices

IW4Iris is licensed under the GNU General Public License v3.0 or later
(LICENSE). It contains or is built with the following components.

## Built into the cinematic client (`client\iw4x.dll`)

| Component | Version | Licence | Notes |
| --- | --- | --- | --- |
| [IW4x client](https://github.com/iw4x/iw4x-client) | commit `912099b7c405` | GPL-3.0 | The cinematic client is IW4x with the IW4Iris component and `engine/iw4x/patches/`. IW4x's own third-party components (protobuf, sentry-native, libtomcrypt, libtommath, zlib, mongoose, sqlite, udis86, discord-rpc, pdcurses, rapidjson, GSL, tracy, iw4-open-formats, stb_truetype, ...) are included under their licences; see the IW4x repository and its `deps/` folder. |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) | v1.93.0-docking | MIT | Editor and launcher UI, with its Win32 and Direct3D 9 backends. Copyright (c) 2014-2026 Omar Cornut. |
| [JSON for Modern C++](https://github.com/nlohmann/json) | v3.12.0 (launcher, tests); IW4x's copy in the client | MIT | Copyright (c) 2013-2025 Niels Lohmann. |
| [stb_image_write](https://github.com/nothings/stb) | commit `fe3a2a73bfd5` | MIT or public domain (choice) | Built-in PNG / JPEG writer. Sean Barrett. |

## Shipped files

| File | Licence | Source |
| --- | --- | --- |
| `resources/fonts/Rubik-Regular.ttf`, `Rubik-SemiBold.ttf` | SIL Open Font License 1.1 (`resources/fonts/OFL-1.1.txt`) | Rubik by Hubert & Fischer; the copies come from IWXMVM's `core/resources` |
| `resources/fonts/fa-solid-900.ttf` | SIL Open Font License 1.1 | Font Awesome Free 6.4.2 by Fonticons, Inc.; the copy comes from IWXMVM's `core/resources` |

## Design references (no code copied)

IW4Iris' editor model follows [IWXMVM](https://github.com/reallyluckyy/IWXMVM)
(reallyluckyy, Caball009, kruumy, lxkast, mallgrab, imgemmy, mrks2 and
contributors), [Iris Studio](https://github.com/yerbisonline/IrisStudio) and
[T6Iris](https://github.com/Censoreeddd/T6Iris), all GPL-3.0. Their
campath file format is read by IW4Iris' importer. iw4cine (sortileges) was
studied for its GSC approach to actors.

## Build-time only (not shipped)

* mingw-w64 headers (public domain / ZPL) for cross builds; the compat shim
  in `tools/cross/compat` is IW4Iris' own.
* premake5 (BSD-3-Clause), UASM (Sybase Open Watcom Public License),
  CMake (BSD-3-Clause).

## Trademarks

Call of Duty and Modern Warfare are trademarks of Activision Publishing,
Inc. IW4Iris is not affiliated with or endorsed by Activision or the IW4x
team, and contains no game files.
