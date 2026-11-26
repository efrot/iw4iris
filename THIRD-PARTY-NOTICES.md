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
| [stb_truetype, stb_image](https://github.com/nothings/stb) | commit `fe3a2a73bfd5` | MIT or public domain (choice) | Font outlines and logo images for motion graphics; compiled `static` so they cannot clash with IW4x's own stb_truetype. Sean Barrett. |
| [nanosvg](https://github.com/memononen/nanosvg) | commit `239e102ec2c6` | zlib | SVG logo parsing. Copyright (c) 2013-14 Mikko Mononen. |
| [earcut.hpp](https://github.com/mapbox/earcut.hpp) | v3.2.4 | ISC | Polygon triangulation with holes. Copyright (c) 2015, Mapbox. |

## Built into the launcher (`IW4Iris Launcher.exe`)

Dear ImGui and JSON for Modern C++ as above, and:

| Component | Version | Licence | Notes |
| --- | --- | --- | --- |
| [miniz](https://github.com/richgel999/miniz) | 3.0.2 (amalgamated release, `third_party/miniz`) | MIT | Reads update packages. Copyright 2013-2014 RAD Game Tools and Valve Software; Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. |

## Shipped files

| File | Licence | Source |
| --- | --- | --- |
| `resources/fonts/Rubik-Regular.ttf`, `Rubik-SemiBold.ttf` | SIL Open Font License 1.1 (`resources/fonts/OFL-1.1.txt`) | Rubik by Hubert & Fischer; the copies come from IWXMVM's `core/resources` |
| `resources/fonts/fa-solid-900.ttf` | SIL Open Font License 1.1 | Font Awesome Free 6.4.2 by Fonticons, Inc.; the copy comes from IWXMVM's `core/resources` |

## Design references (no code copied)

Beat tracking follows D. P. W. Ellis, "Beat Tracking by Dynamic
Programming", Journal of New Music Research 36(1), 2007; implemented from
the paper (no code taken from any library).

IW4Iris' editor model follows [IWXMVM](https://github.com/reallyluckyy/IWXMVM)
(reallyluckyy, Caball009, kruumy, lxkast, mallgrab, imgemmy, mrks2 and
contributors), [Iris Studio](https://github.com/yerbisonline/IrisStudio) and
[T6Iris](https://github.com/Censoreeddd/T6Iris), all GPL-3.0. Their
campath file format is read by IW4Iris' importer.

[iw4cine](https://github.com/sortileges/iw4cine) (sortileges) was studied for
its GSC technique for actors (script-model body playing multiplayer
animations, head linked to `j_spine4`, weapons on tags). The repository
publishes no licence, so **none of its code is used**; the Director Mod is an
independent implementation of the technique.

The Director Mod (`mods/iw4iris_director`) is IW4Iris' own GPL-3.0 code and
uses only the stock MW2 GSC builtins and IW4Iris' builtins.

## Build-time only (not shipped)

* mingw-w64 headers (public domain / ZPL) for cross builds; the compat shim
  in `tools/cross/compat` is IW4Iris' own.
* premake5 (BSD-3-Clause), UASM (Sybase Open Watcom Public License),
  CMake (BSD-3-Clause).

## Trademarks

Call of Duty and Modern Warfare are trademarks of Activision Publishing,
Inc. IW4Iris is not affiliated with or endorsed by Activision or the IW4x
team, and contains no game files.
