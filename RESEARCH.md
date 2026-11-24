# Research — T6Iris, IW4x and the MW2 cinematic tool landscape

Phase 0 of IW4Iris. Everything below comes from reading the source of the
repositories at the revisions listed, not from their READMEs alone.

| Repository | Revision analysed | Licence |
| --- | --- | --- |
| [Censoreeddd/T6Iris](https://github.com/Censoreeddd/T6Iris) | `507549887e21` (2026-11-23, "T6Iris 1.3.4") | GPL-3.0 |
| [yerbisonline/IrisStudio](https://github.com/yerbisonline/IrisStudio) | `e4af3550d3bf` (2026-11-21, 1.0.3) | GPL-3.0 |
| [reallyluckyy/IWXMVM](https://github.com/reallyluckyy/IWXMVM) | `9758c12fc6a9` (2026-06-28) | GPL-3.0 |
| [IW4x/iw4x-client](https://github.com/IW4x/iw4x-client) | **`912099b7c405`** (2026-11-22) — the pinned target | GPL-3.0 |
| [sortileges/iw4cine](https://github.com/sortileges/iw4cine) | `19e7b0870fdc` (2023-11-12) | see repository |
| IW4MVM (codmvm.com) | closed source; not analysed | — |

## 1. Lineage of the Iris editors

T6Iris (Black Ops II, Redacted plugin) is a fork of Iris Studio (Black Ops,
Plutonium T5), which is a fork of IWXMVM (CoD4 `iw3`, MW3 `iw5`). All three
share the layout `core/` (game-independent editor) + one folder per game
(`iw3/`, `iw5/`, `t5/`, `t6/`) that implements `IWXMVM::GameInterface`.

* IWXMVM's `GameInterface` is small (~40 virtuals: camera, dvars, sun, DoF,
  film tweaks, HUD, entities, bones, rewinding hooks). Its IW3 module is the
  closest relative of IW4: the camera is overridden in
  `R_SetViewParmsForScene` / `AnglesToAxis` / `FX_SetupCamera` hooks,
  playback time by patching the `msec` passed to `SV_Frame`, rewinding by
  re-reading the demo through an `FS_Read` hook.
* T6Iris' `GameInterface` grew to well over a hundred virtuals (actors, live
  games, killstreaks, content packs, campaign maps, texture packs, surface
  depth, ReShade...). Its core is ~186k lines and tightly coupled to
  singletons (`Mod`, `KeyframeManager::Get()`, `CameraManager::Get()`),
  MinHook-based hooking, signature scanning, its own injection lifecycle
  (`Mod::Initialize` on a thread from `DllMain`), and D3D9 **and** D3D11
  renderers.
* All game modules locate engine functions by **signature scanning** of the
  game executable (`Signatures.hpp`, `Addresses.hpp`).

## 2. IW4x

IW4x is a full open-source client for MW2 multiplayer: a DLL (`iw4x.dll`)
imported by `iw4x.exe` (the iw4x-rawfiles build of `iw4mp`). `DllMain`
checks the base binary (`"iw4mp_ceg.exe"` at `0x6F9358`), then hooks the CRT
entry point and runs `Components::Loader::Initialize()`, which constructs
~100 `Component`s. Relevant findings:

* **Bindings are fixed addresses**, declared once in `src/Game/*.cpp`
  (`Game::cgArray = 0x7F0F78`, `Game::clients = 0xB2C698`, ...) with struct
  layouts in `src/Game/Structs.hpp`, many guarded by `static_assert`s
  (`offsetof(cg_s, refdef) == 0x6A778 // CONFIRMED CORRECT`). IW4x targets one
  executable, so there is nothing to scan for: an address IW4x uses at a
  revision is, by construction, valid for the binary IW4x accepts.
* **Extension points IW4x already provides**: `Renderer::OnBackendFrame`
  (called in `RB_SwapBuffers` right before `Present`, with the device),
  `Renderer::OnDeviceRecoveryBegin/End` (device loss and `vid_restart`),
  `Window::OnCreate` / `Window::GetWindow`, `Scheduler` pipelines (`MAIN`
  once per `Com_Frame`, `RENDERER` at `ScrPlace_EndFrame` — used by `Toast`
  to draw 2D, `CLIENT`, `SERVER`, `ASYNC`), `Command::Add`, `Dvar` helpers,
  `Logger`.
* **Demo support** (`Theatre.cpp`): `.dm_13` demos with a `.dm_13.json`
  metadata sidecar (`mapname`, `gametype`, `author`, `length` in ms,
  `timestamp` as a string), `cl_autoRecord`, `sv_cheats` forced on during
  playback, a server-command backlog fix "likely to happen when fast
  forwarding, very likely when rewinding", `AdjustTimeDelta` that skips the
  engine's time nudging during playback, and the exact record layout it
  writes (`WriteBaseline`, `CL_WriteDemoClientArchive_Hk`). There is no
  built-in camera control, pause or seek.
* **Networking**: everything goes through `Game::Sys_SendPacket` (IW4x's own
  `Network::SendRaw` too); loopback traffic is delivered inside
  `NET_SendPacket` and never reaches it.
* **View**: `cg_s::refdef` (`RefdefView { tanHalfFovX, tanHalfFovY, org[3],
  axis[3][3], zNear }`). IW4x itself hooks one call inside
  `CG_DrawActiveFrame` that runs after the view is computed:
  `Rumble::CG_SetRumbleReceiver` replaces the `R_EndDobjScene` call at
  `0x486DEC` and reads `refdef.view.org` there. No other camera hook exists
  in IW4x.
* **Input**: `Window::MessageHandler` wraps the game's window procedure;
  `RawMouse` wraps `IN_Frame`, `IN_MouseMove` and `IN_RecenterMouse` at all
  their call sites.
* **Build**: premake (`premake5.lua`, targets in `deps/premake/*.lua`), MSVC
  via `tools/premake5 vs2022` + MSBuild, and a MinGW-w64 cross build
  (`premake5 --mingw gmake`) used by IW4x's CI on Fedora. C++20, warnings as
  errors, RTTI off in Release.
* **Huffman**: IW4x replaces the engine's message compression with its own
  static-tree coder (`Utils::Huffman`), which is also what demo messages use.

## 3. Other references

* **iw4cine** is a GSC mod: cinematic features (bots as actors, model
  spawning, camera paths) are implemented in script inside a private match.
  It shows what the engine offers without native code (script models,
  `scriptModelPlayAnim`, `setOrigin`), which is the most promising route for
  actors in IW4 (see ROADMAP).
* **IW4MVM** is closed source, so nothing was taken from it.

## 4. Strategy decided

1. **Native IW4x component, not an injected plugin.** IW4Iris is built into
   a dedicated *cinematic build* of the IW4x client as
   `Components::Cinematic`. This gives source-level access to IW4x's
   verified bindings and extension points and needs no injector, no
   signature scanning and no hook library.
2. **Reuse at the level of models and algorithms, not files.** T6Iris' core
   cannot be lifted into an IW4x component: its singletons, lifecycle,
   hooking, D3D11 path and T5/T6-specific `GameInterface` would bring
   thousands of lines of unrelated, game-specific assumptions. IW4Iris'
   `iris::core` re-implements the reusable ideas from IWXMVM/T6Iris
   (keyframed campaths with per-key interpolation, constant-speed retiming,
   eases, keyframeable visual properties, the dolly/free/orbit/bone camera
   set, capture to image sequences / FFmpeg, multi-pass ideas) as a clean,
   dependency-light, fully unit-tested library. Where IWXMVM/T6Iris file
   formats exist, IW4Iris reads them (campath import).
3. **Smallest reliable integration.** A 30-line patch to IW4x
   (`engine/iw4x/patches/`) registers the component, calls the view hook
   from IW4x's existing `CG_SetRumbleReceiver` hook, gates `RawMouse` while
   the editor has the mouse, and links the editor libraries. Everything else
   lives in IW4Iris' own files.
4. **No new addresses.** IW4Iris introduces **no** engine address of its own:
   it only uses bindings IW4x declares and uses at the pinned revision (see
   COMPATIBILITY.md). Where it needs something IW4x does not bind, it uses
   address-free techniques (import-table patching of `timeGetTime` /
   `QueryPerformanceCounter` / Winsock / WinINet, D3D9 interfaces) or
   documents the gap.

## 5. Classification of T6Iris features for IW4

| T6Iris system | Classification for IW4 | IW4Iris status |
| --- | --- | --- |
| Timeline, keyframes, interpolation, eases, constant speed | Directly reusable (as concepts) | Re-implemented in `iris::core`, tested |
| Graph editor, undo history | Directly reusable | Re-implemented |
| Free / dolly / orbit camera, lens, shake | Requires adaptation (camera hook) | Implemented; hook not runtime-verified |
| Bone camera | Requires engine research (DObj bone bindings: IW4x binds `CG_GetBoneIndex` only) | Planned |
| Demo playback / pause / speed | Requires new implementation (no theater in IW4) | Implemented via `timescale` + `cls.realtime` |
| Rewinding | Requires engine research (no snapshot restore in IW4x) | Restart-and-replay checkpoint seeking |
| Kill / event markers | Requires engine research (obituaries are delta-coded entity events) | Manual markers; event parsing planned |
| Recording, ProRes / FFmpeg | Directly reusable (as concept) | Implemented |
| Multi-pass (green screen, depth, normals) | Requires engine research (IW4 renderer) | Beauty pass only; others listed as unavailable |
| Actors, animations, weapons | Requires engine research (XAnim/DObj, or GSC script models) | Data model only |
| Killstreaks / vehicles | Requires engine research | Planned |
| Lights | Requires engine research (IW4 primary lights are baked per map) | Sun + colour presets only |
| Look (film tweaks, DoF, sun, fog) | Requires adaptation (IW4 dvars) | Implemented with runtime dvar detection |
| ReShade integration | Currently unsupported (needs investigation with IW4x's D3D9Ex wrapper) | Not implemented |
| Texture packs, campaign maps, content packs | T5/T6-specific | Out of scope |
| Launcher | Requires new implementation (IW4x install layout) | Implemented |

## 6. Open questions that need the game

These cannot be settled without running IW4x and are tracked in
COMPATIBILITY.md and docs/TESTING.md:

1. Does writing `refdef` at the `R_EndDobjScene` call reach the main
   `R_RenderScene` (world, models) — and which systems (FX culling, LOD,
   view model) still use the player's view computed earlier?
2. Is `Sys_Milliseconds` built on the executable's `timeGetTime` /
   `QueryPerformanceCounter` imports? (`Clock::Verify` checks this at run
   time and refuses offline captures otherwise.)
3. Does holding / advancing `clientStatic_t::realtime` pause / skip demo
   playback cleanly?
4. Which tweak dvars (`r_filmTweak*`, `r_dof_*`, `r_lightTweakSun*`) exist
   and take effect live? (Detected at run time and shown in Diagnostics.)
5. How many frames does the renderer present after the frame was built?
   (Measured at the start of every capture by the sync tags.)
