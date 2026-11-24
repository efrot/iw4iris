# Compatibility

The machine-readable summary is `compatibility/manifest.json`.

## Targeted IW4x revision

| | |
| --- | --- |
| Repository | https://github.com/iw4x/iw4x-client |
| Commit | `912099b7c405c8083df3c4bac049693915086dae` (2026-11-22) |
| Base executable | `iw4x.exe` from the iw4x-rawfiles (`iw4mp_ceg.exe` marker at `0x6F9358`, checked by IW4x's `DllMain`) |
| Pinned as | the `external/iw4x-client` submodule |

The cinematic client is IW4x at this commit plus `engine/iw4x/patches/` and
the IW4Iris component. Another IW4x revision may move or remove the
bindings below; the overlay scripts warn when the checkout differs.

**Runtime status: none of the engine integration has been tested in the
game by the IW4Iris authors yet** (the development environment has no
copy of the game). Everything below compiles and links against the pinned
IW4x; the "needs runtime verification" column says what has to be confirmed
in the game (procedures in `docs/TESTING.md`).

## Engine bindings used

IW4Iris adds **no engine address of its own**. Each binding is declared by
IW4x at the pinned revision; the "IW4x reference" column shows where IW4x
declares and uses it.

| Binding | IW4x reference | Used for | Needs runtime verification |
| --- | --- | --- | --- |
| `Game::cgArray` (`cg_s`, 0x7F0F78) `.refdef.view` | `Game/Client.cpp`; `Structs.hpp` `static_assert(offsetof(cg_s, refdef) == 0x6A778) // CONFIRMED CORRECT`; read by `Rumble.cpp` | camera read / override | that the override at the hook below reaches the main scene render |
| `cg_s::clientNum`, `predictedPlayerState.origin` | `Structs.hpp` | demo player, presets | — |
| Call site 0x486DEC (`R_EndDobjScene` in `CG_DrawActiveFrame`) | hooked by IW4x `Rumble::CG_SetRumbleReceiver`, which reads `refdef.view.org` | where the view is overridden (via the patch) | see above; FX culling and view-model placement happen earlier and still use the game's view |
| `Game::clients` (`clientActive_t`, 0xB2C698) `.serverTime`, `.snap.serverTime`, `.mapname` | `Game/Client.cpp`, `Theatre.cpp` (`AdjustTimeDelta`) | demo time, map name | — |
| `Game::cls` (`clientStatic_t`, 0xA7FE90) `.realtime` | `Game/Client.cpp`, `Theatre.cpp` | pause (hold) and forward seek (advance) | that holding / advancing it pauses / skips demo playback |
| `Game::clientConnections` (0xA1E878) `.demoplaying`, `.demoName`, `.serverAddress` | `Theatre.cpp` (`AssertOffset(... demoplaying, 0x40194)`) | demo state, offline session check | — |
| `Game::cg_entitiesArray` (0x8F3CA8) `.entity.pose.origin`, `.nextValid`, `.nextState.eType` | `Game/Client.cpp` (`CG_GetEntity`) | orbit / follow targets, player list | that `pose.origin` is the interpolated origin |
| `Game::CL_GetClientName` (0x4563D0) | `Game/Client.cpp` | player names | — |
| `Game::CL_IsCgameInitialized` (0x43EB20), `CL_GetLocalClientConnectionState` | `Game/Client.cpp`, `Discord.cpp` | readiness | — |
| `Game::Sys_SendPacket` (0x60FDC0) | `Game/System.cpp`; used by `Network::SendRaw` | **replaced** by the offline guard | that nothing else sends UDP (Winsock guards cover the rest) |
| `Game::Sys_Milliseconds` (0x42A660) | `Game/System.cpp`, `Theatre.cpp` | render-clock self check | `Clock::Verify` checks at run time |
| `Game::com_timescale` (0x1AD7920) | `Game/Dvars.cpp` | playback speed | — |
| `Game::Dvar_FindVar`, `Dvar_SetBool/Float/Int/String` | `Game/Dvars.cpp` | all dvar access | — |
| `Game::fs_basepath` | `Game/Dvars.cpp`, `FileSystem.cpp` | IW4Iris folder | — |
| `Game::Key_IsCatcherActive` (`KEYCATCH_CONSOLE`) | `Game/Functions.cpp` | console passthrough | — |
| `Game::CL_DrawStretchPicPhysical`, `sharedUiInfo->assets.whiteMaterial` | `Game/Client.cpp`; used by `Toast.cpp` from the `RENDERER` pipeline | capture sync tags | that the tag reaches the back buffer unmodified (measured each capture) |
| `Utils::Huffman::Decompress` | `Utils/Huffman.cpp`; used by `Huffman.cpp` for `CL_ParseServerMessage` | demo index | — |
| `Renderer::OnBackendFrame`, `OnDeviceRecoveryBegin/End`, `Width/Height` | `Renderer.cpp` (stub in `RB_SwapBuffers` before `Present`) | editor drawing, back-buffer capture, device reset | that the back buffer holds the finished frame |
| `Window::OnCreate`, `Window::GetWindow` | `Window.cpp` | editor input | — |
| `Scheduler` `MAIN`, `RENDERER`, `OnGameShutdown` | `Scheduler.cpp` | per-frame logic, 2D tags, shutdown | — |
| `Command::Add`, `Command::Execute` | `Command.cpp` | console commands, `demo`, `disconnect` | — |
| `Dedicated::IsEnabled`, `ZoneBuilder::IsEnabled` | | the component stays off on servers / ZoneBuilder | — |
| `RawMouse::IN_Frame`, `IN_MouseMove`, `IN_RecenterMouse` | `RawMouse.cpp` (wraps every call site) | gated by the patch while the editor has the mouse | — |

IW4x's Sentry crash reporting (`src/Sentry`) only starts when the build
carries a DSN (`--sentry-dsn`). IW4Iris builds pass none, so the reporter
is off and `sentry-crash.exe` is never started.

Address-free techniques (no game addresses involved):

* import-table patching of the executable's `timeGetTime`,
  `QueryPerformanceCounter` (render clock) and of the executable's and the
  DLL's `connect`, `sendto`, `WSAConnect`, `WSASendTo`, `listen`, `InternetOpenA/W`,
  `URLDownloadToFileA/W`, `WinHttpOpen` (offline guard);
* Direct3D 9 interfaces for back-buffer readback and drawing;
* window subclassing for input.

## Dvars

Used only when present; the editor checks every 2 s and lists them in
Help > Diagnostics. Controls whose dvars are missing say so.

| Purpose | Dvars |
| --- | --- |
| Playback / capture | `timescale`, `com_maxfps` |
| Cinematic cameras | `cg_thirdPerson`, `cg_thirdPersonRange`, `cg_thirdPersonAngle`, `cg_drawGun`, `cg_draw2D`, `cg_drawCrosshair`, `r_lodBiasRigid`, `r_lodBiasSkinned` |
| Colour | `r_filmUseTweaks`, `r_filmTweakEnable`, `r_filmTweakBrightness`, `r_filmTweakContrast`, `r_filmTweakDesaturation`, `r_filmTweakLightTint`, `r_filmTweakDarkTint`, `r_filmTweakInvert` |
| Depth of field | `r_dof_tweak`, `r_dof_enable`, `r_dof_nearStart`, `r_dof_nearEnd`, `r_dof_farStart`, `r_dof_farEnd`, `r_dof_nearBlur`, `r_dof_farBlur`, `r_dof_bias` |
| Sun (experimental) | `r_lightTweakSunColor`, `r_lightTweakSunLight`, `r_lightTweakSunDirection` |
| Fog | `r_fog` |

## Renderer capabilities

| Capability | Status |
| --- | --- |
| Back-buffer formats | `X8R8G8B8`, `A8R8G8B8` (others: the capture refuses with an error) |
| Multisampled back buffers | resolved with `StretchRect` before readback |
| Device reset during capture | the capture fails cleanly (frames would be missing) |
| Render size | the game's resolution (`r_mode`); off-screen rendering is not implemented |
| Render passes | beauty only |

## Demo formats

| Format | Support |
| --- | --- |
| IW4x `.dm_13` + `.dm_13.json` | listing, metadata, playback through the engine, record scan (snapshot times / offsets) |
| Retail / Steam demos | IW4x's `-retaildemo` / `-steamdemo` flags apply as in IW4x; not tested with IW4Iris |
| Black Ops / BO2 theater files | not applicable |

## Known limitations

* Backward seeks restart the demo and replay to the target (exact, but as
  slow as parsing up to the target).
* Freeze frames (speed 0) cannot be rendered offline yet: every output
  frame must advance the scene by at least 1 ms.
* Kill / event markers are manual; obituaries are delta-coded entity events
  in snapshots, which the record scanner does not decode.
* Things the engine computes before the view hook still follow the game's
  view: effect culling, the view model's placement (hidden in cinematic
  cameras), and model LOD (forced to maximum).
* Actors, lights beyond the sun, render passes other than beauty, bone
  cameras and ReShade are not implemented (see FEATURE_MATRIX.md).
