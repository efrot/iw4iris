// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Runtime.hpp"
#include "Capture.hpp"
#include "Director.hpp"
#include "Graphics.hpp"
#include "Clock.hpp"
#include "DemoControl.hpp"
#include "Editor.hpp"
#include "Engine.hpp"
#include "Hooks.hpp"
#include "Log.hpp"
#include "OfflineGuard.hpp"
#include "Session.hpp"
#include "../Window.hpp"

#include <iris/rig.hpp>
#include <iris/version.hpp>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace IW4Iris::Runtime
{
  std::atomic<bool> editorVisible{false};
  std::recursive_mutex uiMutex;

  namespace
  {
    bool initialized = false;      // first main frame done
    bool guardOk = false;
    HWND subclassedWindow = nullptr;
    WNDPROC previousWndProc = nullptr;

    // render thread
    IDirect3DDevice9* uiDevice = nullptr;
    HWND uiWindow = nullptr;
    bool imguiContext = false;

    std::mutex cameraMutex;
    std::optional<iris::CameraState> lastApplied;

    std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastAutosave = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastDvarCheck{};
    double fpsAverage = 0.0;

    // Mouse capture for flying the free camera.
    std::atomic<bool> mouseCaptured{false};
    POINT mouseCenter{};

    bool IsMouseMessage(UINT msg)
    {
      return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_INPUT || msg == WM_MOUSELEAVE;
    }

    bool IsKeyboardMessage(UINT msg)
    {
      return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR ||
             msg == WM_SYSCHAR;
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
      if (msg == WM_KEYDOWN && wParam == VK_F1 && !(lParam & (1 << 30)) && guardOk && OfflineGuard::SessionAllowed())
      {
        ToggleEditor();
        return 0;
      }

      if (editorVisible)
      {
        {
          std::lock_guard _(uiMutex);
          if (imguiContext)
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
        }
        const bool consoleOpen = Game::Key_IsCatcherActive(0, Game::KEYCATCH_CONSOLE);
        if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
          return TRUE;  // the editor draws its own cursor
        if (IsMouseMessage(msg))
          return 0;
        // The console key and an open console go to the game.
        const bool consoleKey = (msg == WM_KEYDOWN || msg == WM_KEYUP) && wParam == VK_OEM_3;
        if (IsKeyboardMessage(msg) && !consoleOpen && !consoleKey)
          return 0;
      }
      return CallWindowProcA(previousWndProc, hwnd, msg, wParam, lParam);
    }

    void Subclass(HWND hwnd)
    {
      if (!hwnd || hwnd == subclassedWindow)
        return;
      previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
      subclassedWindow = hwnd;
      Log::Info("Editor input attached to the game window");
    }

    void FirstFrame()
    {
      auto& s = GetSession();
      {
        std::lock_guard _(s.mutex);
        s.baseDirectory = std::filesystem::path(Engine::BasePath()) / "iw4iris";
      }
      Log::Open(s.baseDirectory / "logs" / "iw4iris.log");
      Log::Info("{} starting (IW4x component); offline guard {}", iris::kVersionString, guardOk ? "active" : "FAILED");
      std::lock_guard _(s.mutex);
      LoadSettings(s);
      std::error_code ec;
      std::filesystem::create_directories(s.ProjectsDirectory(), ec);
      std::filesystem::create_directories(s.RecordingsDirectory(), ec);
      std::filesystem::create_directories(s.ExportsDirectory(), ec);
      if (s.settings.demoFolders.empty())
      {
        const auto base = std::filesystem::path(Engine::BasePath());
        for (const auto& f : {base / "userraw" / "demos", base / "demos", base / "players" / "demos",
                              base / "iw4x" / "demos", base / "mods"})
          if (std::filesystem::is_directory(f, ec))
            s.settings.demoFolders.push_back(f.string());
      }
      s.freeCamera.Settings() = s.settings.freeCamera;
      s.ResetHistory();
      // Crash recovery: an autosave newer than its project is offered by the
      // editor's File menu (Editor::RecoveryPrompt).
    }

    // ---------------------------------------------------------- free camera input

    // XInput is loaded at run time, as IW4x's own controller code does.
    using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    XInputGetState_t LoadXInput()
    {
      static XInputGetState_t fn = [] {
        for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"})
          if (const HMODULE m = LoadLibraryA(dll))
            if (const auto f = reinterpret_cast<XInputGetState_t>(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState"))))
              return f;
        return static_cast<XInputGetState_t>(nullptr);
      }();
      return fn;
    }

    bool KeyDown(int vk)
    {
      return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    void UpdateFlying(Session& s, double dt)
    {
      const HWND window = Components::Window::GetWindow();
      const bool focused = window && GetForegroundWindow() == window;
      // Fly while the right mouse button is held in the viewport, or when
      // fly mode was toggled with F.
      const bool want = focused && editorVisible && (s.flying || (KeyDown(VK_RBUTTON) && (mouseCaptured || Editor::ViewportHovered())));
      if (want != mouseCaptured)
      {
        mouseCaptured = want;
        if (want)
        {
          RECT rc{};
          GetClientRect(window, &rc);
          mouseCenter = {(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
          ClientToScreen(window, &mouseCenter);
          SetCursorPos(mouseCenter.x, mouseCenter.y);
          // Start from wherever the camera is now. Flying looks through the
          // navigation camera; the project's shot cameras stay as they are
          // (audit F-02: flying used to switch the active camera).
          if (const auto cam = LastAppliedCamera())
          {
            s.freeCamera.SetState(*cam);
            s.freeCameraPlaced = true;
          }
          s.freeView = true;
        }
        else
        {
          s.freeCamera.Stop();
        }
      }
      iris::FreeCameraInput in;
      if (mouseCaptured)
      {
        POINT p{};
        GetCursorPos(&p);
        const double sens = s.settings.mouseSensitivity;
        in.lookYaw = -(p.x - mouseCenter.x) * sens;
        in.lookPitch = (p.y - mouseCenter.y) * sens;
        SetCursorPos(mouseCenter.x, mouseCenter.y);
        in.forward = (KeyDown('W') ? 1.0 : 0.0) - (KeyDown('S') ? 1.0 : 0.0);
        in.right = (KeyDown('D') ? 1.0 : 0.0) - (KeyDown('A') ? 1.0 : 0.0);
        in.up = (KeyDown(VK_SPACE) ? 1.0 : 0.0) - (KeyDown(VK_CONTROL) ? 1.0 : 0.0);
        in.roll = (KeyDown('E') ? 1.0 : 0.0) - (KeyDown('Q') ? 1.0 : 0.0);
        in.fast = KeyDown(VK_SHIFT);
        in.slow = KeyDown(VK_MENU);
        in.fovDelta = Editor::TakeWheelFov();
        // Gamepad: the left stick moves, the right stick looks.
        XINPUT_STATE pad{};
        const auto getState = LoadXInput();
        if (getState && getState(0, &pad) == ERROR_SUCCESS)
        {
          auto axis = [](SHORT v) {
            const double f = v / 32767.0;
            return std::abs(f) < 0.18 ? 0.0 : f;
          };
          in.forward += axis(pad.Gamepad.sThumbLY);
          in.right += axis(pad.Gamepad.sThumbLX);
          in.lookYaw -= axis(pad.Gamepad.sThumbRX) * 140.0 * dt;
          in.lookPitch -= axis(pad.Gamepad.sThumbRY) * 110.0 * dt;
          in.up += (pad.Gamepad.bRightTrigger - pad.Gamepad.bLeftTrigger) / 255.0;
          in.fast = in.fast || (pad.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
        }
      }
      s.freeCamera.Settings() = s.settings.freeCamera;
      s.freeCamera.Update(in, dt);
    }

    // ---------------------------------------------------------- look (dvars)

    void ApplyCameraDvars(bool modCamera, const iris::CameraDef* camera, const iris::VisualSettings& v)
    {
      if (modCamera)
      {
        Engine::OverrideBool("cg_thirdPerson", true);  // draw the followed player's body
        if (v.hideGunInModCameras)
          Engine::OverrideBool("cg_drawGun", false);
        else
          Engine::Restore("cg_drawGun");
        Engine::OverrideFloat("r_lodBiasRigid", -40000.0f);  // the camera is not where the engine picked LODs from
        Engine::OverrideFloat("r_lodBiasSkinned", -40000.0f);
      }
      else if (camera && camera->mode == iris::CameraMode::ThirdPerson)
      {
        Engine::OverrideBool("cg_thirdPerson", true);
        Engine::OverrideFloat("cg_thirdPersonRange", static_cast<float>(camera->orbitDistance));
        Engine::OverrideFloat("cg_thirdPersonAngle", static_cast<float>(camera->orbitStartAngle));
        Engine::Restore("cg_drawGun");
      }
      else
      {
        Engine::Restore("cg_thirdPerson");
        Engine::Restore("cg_thirdPersonRange");
        Engine::Restore("cg_thirdPersonAngle");
        Engine::Restore("cg_drawGun");
        Engine::Restore("r_lodBiasRigid");
        Engine::Restore("r_lodBiasSkinned");
      }
      // HUD hiding applies to cinematic cameras and recordings; the game's own
      // view keeps its HUD unless a capture runs.
      const bool clean = modCamera || Capture::IsActive();
      if (v.hideHud && clean)
        Engine::OverrideBool("cg_draw2D", false);
      else
        Engine::Restore("cg_draw2D");
      if (v.hideCrosshair && clean)
        Engine::OverrideBool("cg_drawCrosshair", false);
      else
        Engine::Restore("cg_drawCrosshair");
    }

    void ApplyLook(const iris::Project& p, double t, const iris::CameraState* camera)
    {
      const auto& v = p.visuals;
      auto value = [&](const char* property, double fallback) { return p.PropertyValue(property, t, fallback); };

      if (v.filmOverride)
      {
        Engine::OverrideBool("r_filmUseTweaks", true);
        Engine::OverrideBool("r_filmTweakEnable", true);
        Engine::OverrideFloat("r_filmTweakBrightness", static_cast<float>(value("film.brightness", v.filmBrightness)));
        Engine::OverrideFloat("r_filmTweakContrast", static_cast<float>(value("film.contrast", v.filmContrast)));
        Engine::OverrideFloat("r_filmTweakDesaturation", static_cast<float>(value("film.desaturation", v.filmDesaturation)));
        Engine::OverrideVec3("r_filmTweakLightTint", v.filmLightTint);
        Engine::OverrideVec3("r_filmTweakDarkTint", v.filmDarkTint);
        Engine::OverrideBool("r_filmTweakInvert", v.filmInvert);
      }
      else
      {
        for (const char* d : {"r_filmUseTweaks", "r_filmTweakEnable", "r_filmTweakBrightness", "r_filmTweakContrast",
                              "r_filmTweakDesaturation", "r_filmTweakLightTint", "r_filmTweakDarkTint", "r_filmTweakInvert"})
          Engine::Restore(d);
      }

      if (v.dofOverride)
      {
        Engine::OverrideBool("r_dof_tweak", true);
        Engine::OverrideBool("r_dof_enable", true);
        double nearStart = v.dofNearStart, nearEnd = v.dofNearEnd, farStart = v.dofFarStart, farEnd = v.dofFarEnd;
        if (v.dofFromLens && camera)
        {
          // The engine's depth of field is a near and a far blur ramp, not a
          // lens: the ramps are placed at the thin-lens limits of acceptable
          // sharpness for the camera's focal length, aperture and focus.
          const double focal = iris::HorizontalFovToFocalLength(camera->horizontalFov, 36.0);
          const auto dof = iris::ComputeDepthOfField(focal, camera->fNumber, camera->focusDistance);
          nearEnd = std::max(0.0, dof.nearLimit);
          nearStart = std::max(0.0, nearEnd * 0.5);
          farStart = dof.farIsInfinite ? 100000.0 : dof.farLimit;
          farEnd = dof.farIsInfinite ? 100000.0 : dof.farLimit * 2.0;
        }
        Engine::OverrideFloat("r_dof_nearStart", static_cast<float>(nearStart));
        Engine::OverrideFloat("r_dof_nearEnd", static_cast<float>(nearEnd));
        Engine::OverrideFloat("r_dof_farStart", static_cast<float>(farStart));
        Engine::OverrideFloat("r_dof_farEnd", static_cast<float>(farEnd));
        Engine::OverrideFloat("r_dof_nearBlur", static_cast<float>(value("dof.nearBlur", v.dofNearBlur)));
        Engine::OverrideFloat("r_dof_farBlur", static_cast<float>(value("dof.farBlur", v.dofFarBlur)));
        Engine::OverrideFloat("r_dof_bias", static_cast<float>(v.dofBias));
      }
      else
      {
        for (const char* d : {"r_dof_tweak", "r_dof_enable", "r_dof_nearStart", "r_dof_nearEnd", "r_dof_farStart",
                              "r_dof_farEnd", "r_dof_nearBlur", "r_dof_farBlur", "r_dof_bias"})
          Engine::Restore(d);
      }

      if (v.sunOverride)
      {
        const double intensity = value("sun.intensity", v.sunIntensity);
        Engine::OverrideColor("r_lightTweakSunColor", v.sunColor);
        Engine::OverrideFloat("r_lightTweakSunLight", static_cast<float>(intensity));
        Engine::OverrideVec3("r_lightTweakSunDirection", {v.sunAngles.pitch, v.sunAngles.yaw, 0.0});
      }
      else
      {
        for (const char* d : {"r_lightTweakSunColor", "r_lightTweakSunLight", "r_lightTweakSunDirection"})
          Engine::Restore(d);
      }

      if (v.fogOverride)
        Engine::OverrideBool("r_fog", v.fogOpacity > 0.0);
      else
        Engine::Restore("r_fog");
    }

    iris::RigContext MakeRigContextImpl(const Session& s)
    {
      iris::RigContext ctx;
      ctx.gameView = s.status.gameView;
      ctx.resolveTarget = [](const iris::TargetRef& t) -> std::optional<iris::Vec3> {
        if (t.kind == iris::TargetKind::Player || t.kind == iris::TargetKind::Entity)
          if (const auto o = Engine::EntityOrigin(t.index))
            return *o + t.offset;
        return std::nullopt;
      };
      return ctx;
    }
  }

  iris::RigContext MakeRigContextFor(const Session& s)
  {
    return MakeRigContextImpl(s);
  }

  void Initialize()
  {
    guardOk = OfflineGuard::Install();
    Clock::Install();
    if (!guardOk)
      Log::Error("IW4Iris stays disabled: the offline guard could not be installed");
  }

  void Shutdown()
  {
    if (Capture::IsActive())
      Capture::Cancel();
    Engine::RestoreAll();
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    if (!s.baseDirectory.empty())
      SaveSettings(s);
  }

  void ToggleEditor()
  {
    editorVisible = !editorVisible;
    if (!editorVisible)
    {
      auto& s = GetSession();
      std::lock_guard _(s.mutex);
      s.flying = false;
    }
  }

  std::optional<iris::CameraState> LastAppliedCamera()
  {
    std::lock_guard _(cameraMutex);
    return lastApplied;
  }

  void SetLastApplied(const iris::CameraState& camera)
  {
    std::lock_guard _(cameraMutex);
    lastApplied = camera;
  }

  bool IsFlying()
  {
    return mouseCaptured;
  }

  void WindowCreated()
  {
    Subclass(Components::Window::GetWindow());
  }

  void MainFrame()
  {
    if (!initialized)
    {
      if (Engine::BasePath().empty())
        return;
      FirstFrame();
      initialized = true;
    }
    if (!subclassedWindow)
      Subclass(Components::Window::GetWindow());

    OfflineGuard::VerifySession();
    if (!guardOk || !OfflineGuard::SessionAllowed())
    {
      editorVisible = false;
      if (Capture::IsActive())
        Capture::Cancel();
      Engine::RestoreAll();
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - lastFrame).count(), 0.0, 0.25);
    lastFrame = now;
    fpsAverage = fpsAverage * 0.95 + (dt > 0 ? 1.0 / dt : 0.0) * 0.05;

    auto& s = GetSession();
    for (auto& work : s.TakePosted())
      work();

    DemoControl::Update();
    Capture::MainFrame();

    std::lock_guard _(s.mutex);
    auto& st = s.status;
    st.cgameReady = Engine::IsCgameReady();
    st.demoPlaying = Engine::IsDemoPlaying();
    st.connectionState = Engine::ConnectionState();
    st.demoName = st.demoPlaying ? Engine::DemoName() : std::string();
    st.mapName = st.cgameReady ? Engine::MapName() : std::string();
    st.clientServerTime = Engine::ClientServerTime();
    st.snapshotServerTime = Engine::SnapshotServerTime();
    st.renderWidth = Engine::RenderWidth();
    st.renderHeight = Engine::RenderHeight();
    st.fps = fpsAverage;
    st.seeking = DemoControl::IsSeeking();
    st.paused = DemoControl::IsPaused();
    st.sceneTime = DemoControl::SceneTime();
    st.demoDuration = s.demoIndex ? s.demoIndex->DurationSeconds() : 0.0;
    if (st.demoDuration <= 0.0)
      s.demoLengthFitted = 0.0;  // the next demo is fitted again
    if (std::chrono::duration<double>(now - lastDvarCheck).count() > 2.0)
    {
      lastDvarCheck = now;
      static const char* used[] = {"timescale", "com_maxfps", "cg_thirdPerson", "cg_thirdPersonRange", "cg_thirdPersonAngle",
                                   "cg_drawGun", "cg_draw2D", "cg_drawCrosshair", "cg_fov", "r_lodBiasRigid",
                                   "r_lodBiasSkinned", "r_filmUseTweaks", "r_filmTweakEnable", "r_filmTweakBrightness",
                                   "r_filmTweakContrast", "r_filmTweakDesaturation", "r_filmTweakLightTint",
                                   "r_filmTweakDarkTint", "r_filmTweakInvert", "r_dof_tweak", "r_dof_enable",
                                   "r_dof_nearStart", "r_dof_nearEnd", "r_dof_farStart", "r_dof_farEnd", "r_dof_nearBlur",
                                   "r_dof_farBlur", "r_dof_bias", "r_lightTweakSunColor", "r_lightTweakSunLight",
                                   "r_lightTweakSunDirection", "r_fog", "sv_cheats"};
      st.checkedDvars.assign(std::begin(used), std::end(used));
      st.availableDvars.clear();
      for (const char* d : used)
        if (Engine::HasDvar(d))
          st.availableDvars.insert(d);
    }
    if (st.cgameReady)
    {
      st.players.clear();
      for (const auto& p : Engine::Players())
        st.players.emplace_back(p.clientNum, p.name.empty() ? "client " + std::to_string(p.clientNum) : p.name);
    }

    Director::MainFrame(s);
    Graphics::MainFrame(s);

    auto& project = s.project;
    if (!Capture::IsActive())
    {
      if (DemoControl::HasScene())
      {
        // The demo is the clock: the playhead shows where the demo is.
        if (!st.seeking && !s.scrubbing)
        {
          const double end = std::max(project.timeline.end, project.timeRemap.timelineStart + 1.0);
          s.playhead = project.timeRemap.CinematicTime(st.sceneTime, end);
          s.playing = !DemoControl::IsPaused();
          if (s.playing)
            DemoControl::SetSpeed(project.timeRemap.SpeedAt(s.playhead) * s.playbackRate);
          // Loop region: jump back to its start.
          const auto& loop = project.timeline.loop;
          if (s.playing && loop.enabled && loop.end > loop.start && s.playhead >= loop.end)
          {
            DemoControl::Seek(project.timeRemap.SceneTime(loop.start));
            s.playhead = loop.start;
          }
        }
        // Once the demo's length is known the timeline covers all of it
        // (it used to stay at the default 10 s, so most of a demo could not
        // be reached with the playhead).
        if (st.demoDuration > 0.0 && s.demoLengthFitted != st.demoDuration)
        {
          s.demoLengthFitted = st.demoDuration;
          const double searchEnd = project.timeRemap.timelineStart + st.demoDuration * 50.0 + 60.0;
          const double length = project.timeRemap.CinematicTime(st.demoDuration, searchEnd) - project.timeline.start;
          if (iris::FitTimelineToSource(project.timeline, length, project.ContentEnd()))
          {
            s.Commit("Fit the timeline to the demo");
            Log::Info("Timeline fitted to the demo: {:.2f} s", length);
          }
          s.timelineFitRequested = true;
        }
      }
      else if (s.playing)
      {
        s.playhead = project.timeline.Advance(s.playhead, dt * s.playbackRate);
      }
    }

    if (editorVisible || s.flying)
      UpdateFlying(s, dt);

    const iris::CameraDef* camera = project.CameraAt(s.playhead);
    const bool modCamera = mouseCaptured || (s.freeView && editorVisible && !Capture::IsActive()) ||
                           (camera && camera->mode != iris::CameraMode::FirstPerson &&
                                             camera->mode != iris::CameraMode::ThirdPerson);
    if (st.cgameReady)
    {
      ApplyCameraDvars(modCamera, camera, project.visuals);
      const auto applied = LastAppliedCamera();
      ApplyLook(project, s.playhead, applied ? &*applied : nullptr);
    }

    // Autosave
    if (s.settings.autosave && s.dirty &&
        std::chrono::duration<double>(now - lastAutosave).count() > s.settings.autosaveSeconds)
    {
      lastAutosave = now;
      const auto file = s.projectFile.empty() ? s.ProjectsDirectory() / "untitled.iw4iris" : s.projectFile;
      std::string error;
      if (iris::SaveProjectFile(iris::AutosavePathFor(file), project, &error))
        Log::Debug("Autosaved to {}", iris::AutosavePathFor(file).string());
      else
        Log::Warn("Autosave failed: {}", error);
    }
  }

  void RendererFrame()
  {
    Capture::DrawSyncTag();
  }

  void BackendFrame(IDirect3DDevice9* device)
  {
    if (!device || !guardOk)
      return;
    Graphics::Draw(device);         // world-space graphics are part of the frame
    Capture::BackendFrame(device);  // before the editor: recordings stay clean
    if (!editorVisible)
      return;

    std::lock_guard _(uiMutex);
    const HWND window = Components::Window::GetWindow();
    if (!window)
      return;
    if (!imguiContext)
    {
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      Editor::Initialize();
      imguiContext = true;
    }
    if (window != uiWindow)
    {
      if (uiWindow)
        ImGui_ImplWin32_Shutdown();
      ImGui_ImplWin32_Init(window);
      uiWindow = window;
    }
    if (device != uiDevice)
    {
      if (uiDevice)
        ImGui_ImplDX9_Shutdown();
      ImGui_ImplDX9_Init(device);
      uiDevice = device;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Editor::Draw();
    ImGui::Render();

    IDirect3DSurface9* backBuffer = nullptr;
    IDirect3DSurface9* previousTarget = nullptr;
    device->GetRenderTarget(0, &previousTarget);
    if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
      device->SetRenderTarget(0, backBuffer);
    const bool began = SUCCEEDED(device->BeginScene());
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    if (began)
      device->EndScene();
    if (previousTarget)
    {
      device->SetRenderTarget(0, previousTarget);
      previousTarget->Release();
    }
    if (backBuffer)
      backBuffer->Release();
  }

  void DeviceLost()
  {
    std::lock_guard _(uiMutex);
    if (uiDevice)
      ImGui_ImplDX9_InvalidateDeviceObjects();
    Capture::OnDeviceLost();
    Graphics::DeviceLost();
  }

  void DeviceRestored()
  {
    std::lock_guard _(uiMutex);
    if (uiDevice)
      ImGui_ImplDX9_CreateDeviceObjects();
  }

  bool SaveProject(const std::string& path, std::string* error)
  {
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    std::filesystem::path file = path.empty() ? s.projectFile : std::filesystem::path(path);
    if (file.empty())
      file = s.ProjectsDirectory() / (iris::SanitizeFileName(s.project.name) + std::string(iris::kProjectExtension));
    if (file.extension() != iris::kProjectExtension)
      file += iris::kProjectExtension;
    if (s.loadedDemoPath)
    {
      if (!s.project.demo)
        s.project.demo = iris::DemoRef{};
      s.project.demo->path = s.loadedDemoPath->string();
      s.project.demo->name = s.loadedDemoPath->stem().string();
      s.project.demo->mapName = s.status.mapName;
      std::error_code ec;
      s.project.demo->fileSize = std::filesystem::file_size(*s.loadedDemoPath, ec);
    }
    if (!iris::SaveProjectFile(file, s.project, error))
      return false;
    s.projectFile = file;
    s.dirty = false;
    s.settings.recentProjects = iris::PushRecent(s.settings.recentProjects, file.string());
    std::error_code ec;
    std::filesystem::remove(iris::AutosavePathFor(file), ec);
    SaveSettings(s);
    Log::Info("Saved project {}", file.string());
    return true;
  }

  bool LoadProject(const std::string& path, std::string* error)
  {
    auto result = iris::LoadProjectFile(path);
    if (!result.project)
    {
      *error = result.error;
      return false;
    }
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    s.project = std::move(*result.project);
    s.projectFile = path;
    s.dirty = result.migrated;
    s.selectedKeys.clear();
    s.selectedCamera = s.project.activeCameraId;
    s.ResetHistory();
    for (const auto& w : result.warnings)
      Log::Warn("Project: {}", w);
    for (const auto& m : iris::FindMissingReferences(s.project, path))
      Log::Warn("Project: {}", m);
    s.settings.recentProjects = iris::PushRecent(s.settings.recentProjects, path);
    SaveSettings(s);
    Log::Info("Opened project {}", path);
    if (const auto demo = iris::ResolveDemoPath(s.project, path))
    {
      std::error_code ec;
      if (std::filesystem::exists(*demo, ec))
      {
        const auto demoPath = *demo;
        s.Post([demoPath] {
          std::string e;
          if (!DemoControl::Play(demoPath, &e))
            Log::Error("{}", e);
        });
      }
    }
    return true;
  }

  void NewProject()
  {
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    s.project = iris::MakeDefaultProject("Untitled");
    s.projectFile.clear();
    s.dirty = false;
    s.selectedKeys.clear();
    s.selectedCamera = s.project.activeCameraId;
    s.playhead = 0.0;
    s.ResetHistory();
  }

  void ResetLook()
  {
    Engine::RestoreAll();
  }

  void AddKeyFromView()
  {
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    std::optional<iris::CameraState> view = s.freeCamera.State();
    if (!mouseCaptured)
      view = LastAppliedCamera();
    if (!view && s.status.gameView)
      view = s.status.gameView;
    if (!view)
      return;
    auto& dolly = s.EditTargetDolly();
    iris::CameraKey key;
    key.time = s.project.timeline.snapToFrames ? s.project.timeline.frameRate.SnapSeconds(s.playhead) : s.playhead;
    key.position = view->position;
    key.angles = view->angles;
    key.horizontalFov = view->horizontalFov;
    key.focusDistance = dolly.lens.focusDistance;
    key.fNumber = dolly.lens.fNumber;
    // New keys take the interpolation of the key before them.
    for (const auto& k : dolly.path.Keys())
      if (k.time < key.time)
        key.interpolation = k.interpolation;
    const auto index = dolly.path.AddKey(key);
    s.selectedCamera = dolly.id;
    s.selectedKeys = {index};
    s.Commit("Add camera key");
  }
}

namespace IW4Iris::Hooks
{
  using Runtime::MakeRigContextFor;
  void OnViewReady(int localClientNum)
  {
    if (localClientNum != 0 || !OfflineGuard::SessionAllowed())
      return;
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    const auto view = Engine::ReadView(localClientNum);
    if (!view)
      return;
    const auto gameView = Engine::ToCameraState(*view);
    s.status.gameView = gameView;

    // While a capture runs, the frame's time comes from the scene time it
    // shows; otherwise from the playhead. A hidden editor keeps previewing
    // the project's cameras, so the shot can be watched clean.
    const auto captureTime = Capture::FrameTime();
    // During a capture the frame's time is the authority for everything
    // that reads the playhead (Director poses, effects, graphics, the UI).
    if (captureTime)
      s.playhead = *captureTime;
    const double t = captureTime.value_or(s.playhead);
    const iris::CameraDef* def = s.project.CameraAt(t);
    // The navigation camera: while flying, and in free view with the editor
    // shown. Never in a capture.
    const bool flying = !captureTime && (Runtime::IsFlying() || (s.freeView && Runtime::editorVisible.load()));

    std::optional<iris::CameraState> camera;
    if (!s.freeCameraPlaced)
    {
      // A free camera chosen before it was ever flown starts at the view.
      s.freeCamera.SetState(gameView);
      s.freeCameraPlaced = true;
    }
    if (flying || (def && def->mode == iris::CameraMode::Free))
      camera = s.freeCamera.State();
    else if (def && def->mode != iris::CameraMode::FirstPerson && def->mode != iris::CameraMode::ThirdPerson)
      camera = iris::EvaluateCamera(*def, t, MakeRigContextFor(s));

    if (camera)
    {
      Engine::WriteView(localClientNum, *camera);
      Runtime::SetLastApplied(*camera);
    }
    else
    {
      Runtime::SetLastApplied(gameView);
    }
    // Motion graphics for this frame, tagged with its scene time.
    Graphics::OnView(s, t, camera.value_or(gameView), Engine::ClientServerTime());
  }

  bool BlocksGameMouse()
  {
    return Runtime::editorVisible.load();
  }
}
