# Building IW4Iris

IW4Iris consists of

* the **cinematic client** — the IW4x client at the pinned revision with the
  IW4Iris component (`iw4x.dll`), built with IW4x's own premake build;
* the **launcher** and the **tests** — built with CMake;
* the **core library** — used by both.

Clone with submodules:

```
git clone --recursive https://github.com/efrot/iw4iris.git
cd iw4iris
```

(`git submodule update --init --recursive` if you cloned without.)

## Windows (Visual Studio 2022)

Requirements: Visual Studio 2022 with *Desktop development with C++*
(MSVC v143, Windows SDK), CMake ≥ 3.20 (included with VS), Git, Python 3
(for the add-on tests). IW4x's build fetches its own dependencies and ships
`premake5.exe`. The free Build Tools are enough; on a fresh machine:

```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Kitware.CMake
winget install Python.Python.3.12
```

The script runs in Windows PowerShell 5.1 and PowerShell 7.

```
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Configuration Release -Package
```

The script

1. clones the pinned IW4x revision into `build\iw4x-cinematic` (from the
   `external\iw4x-client` submodule) with IW4x's submodules,
2. installs IW4Iris into it with `tools\iw4x-overlay.ps1` (applies
   `engine\iw4x\patches\*.patch`, copies the component and the editor
   libraries),
3. runs `tools\premake5.exe vs2022` and builds `build\iw4x.sln` with MSBuild
   (exactly as IW4x's CI does),
4. builds the launcher and tests with CMake (`-A Win32`) and runs the tests,
5. with `-Package`, writes `dist\IW4Iris-v0.5.0-Windows-x86.zip`.

The cinematic client is `build\iw4x-cinematic\build\bin\Win32\Release\iw4x.dll`.

To work on the component in Visual Studio, open
`build\iw4x-cinematic\build\iw4x.sln` after the first run; the component's
files are under `src\Components\Modules\IW4Iris` (copies — edit the
originals in `engine\iw4x\src` and re-run the overlay, or the build script).

> The Windows build path is scripted but has **not been run** by the authors
> yet (the development environment is Linux); please report problems.

## Linux (MinGW-w64 cross build)

This is how the v0.1.0 to v0.5.0 binaries were produced and checked. Requirements:

* `i686-w64-mingw32-g++` with POSIX threads, GCC ≥ 13
  (Ubuntu 24.04: `g++-mingw-w64-i686-posix`)
* **mingw-w64 ≥ 13 headers and import libraries.** Ubuntu 24.04 ships
  mingw-w64 11, which lacks APIs IW4x uses (`CM_Get_Device_Interface_PropertyW`,
  `HREPORT`, ...). The architecture-independent Ubuntu packages
  `mingw-w64-common_13.0.0-2ubuntu1_all.deb` and
  `mingw-w64-i686-dev_13.0.0-2ubuntu1_all.deb` from the Ubuntu archive work
  with the 24.04 compiler (`dpkg -i --force-depends`).
* [premake5](https://github.com/premake/premake-core/releases) ≥ 5.0.0-beta8,
  [UASM](https://github.com/Terraspace/UASM) (build with
  `make -f Makefile-Linux-GCC-64.mak`), GNU make, CMake ≥ 3.20, Python 3.

```
tools/cross/build-all.sh
```

builds the cinematic client (`tools/cross/build-iw4x.sh`), the launcher and
the Windows test binary, and packages `dist/IW4Iris-v0.5.0-Windows-x86.zip`.

With GCC 13, `tools/cross/build-iw4x.sh` applies build-environment
workarounds that do not change the program: it force-includes
`tools/cross/compat/iw4iris_compat.h` (declares `std::ceilf` and friends,
added to libstdc++ in GCC 14), builds IW4x's precompiled header with the
"#pragma once in main file" warning non-fatal, and silences GCC 12/13's
known middle-end false positives (`-Warray-bounds` on mingw's TEB
intrinsics, `-Wmaybe-uninitialized`, `-Wstringop-*`, `-Wrestrict`,
`-Wdangling-pointer` inside libstdc++). With GCC ≥ 14 none of this is
applied.

## Tests

```
tools/run-tests.sh            # Linux: core + launcher rules + Blender add-on
ctest --test-dir build/...    # any CMake build
```

`iw4iris_tests` uses IW4x's own Huffman coder from the submodule for the
demo tests. The Win32 test binary cross-compiles; it could not be run in
the development container (Wine crashes there), so run it on Windows.

## Layout of a release package

```
IW4Iris Launcher.exe
client\iw4x.dll                    the cinematic build of the IW4x client
resources\fonts\                   Rubik, Font Awesome Free (OFL 1.1)
resources\tools\blender\           Blender importer add-on
mods\iw4iris_director\             the Director Mod (GSC), installed by the launcher
samples\  presets\  docs\
README.md INSTALL.md TROUBLESHOOTING.md CHANGELOG.md FEATURE_MATRIX.md
COMPATIBILITY.md LICENSE THIRD-PARTY-NOTICES.md
```

The package contains no game files. As `iw4x.dll` is a modified IW4x
(GPL-3.0), its complete corresponding source is this repository at the same
tag plus the pinned IW4x revision.

## Prebuilt binaries in the repository

So that GitHub's **Code → Download ZIP** is ready to run, the repository
also carries the binaries of the release package at its root:
`IW4Iris Launcher.exe` and `client\` (`iw4x.dll`, `sentry-crash.exe`,
`sentry-wer.dll`), with their checksums in `PREBUILT-SHA256SUMS.txt`. They
are built by the MinGW-w64 cross build above, Release, stripped. After
changing the client or the launcher, rebuild and refresh them:

```
tools/cross/build-all.sh        # builds and runs tools/package.sh
tools/update-prebuilt.sh        # copies dist/IW4Iris-v*/ binaries to the root
```

`client\` must contain only the client's files, because the launcher
installs every file in it into the cinematic environment.

The client is built without a Sentry DSN (premake's `--sentry-dsn` is not
passed), so IW4x's crash reporting stays off: it never starts
`sentry-crash.exe` and never sends reports.
