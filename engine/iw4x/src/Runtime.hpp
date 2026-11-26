// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Runtime glue between the IW4x component, the session and the editor.
#pragma once

#include <iris/camera.hpp>
#include <iris/rig.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

struct IDirect3DDevice9;

namespace IW4Iris::Runtime
{
  // Editor visibility (read from the window procedure and the render thread).
  extern std::atomic<bool> editorVisible;
  // Guards Dear ImGui (input arrives on the main thread, frames are built on
  // the render thread).
  extern std::recursive_mutex uiMutex;

  void Initialize();      // once, from the component's constructor
  void Shutdown();

  void MainFrame();       // Scheduler MAIN pipeline
  void RendererFrame();   // Scheduler RENDERER pipeline (2D pass of the frame)
  void BackendFrame(IDirect3DDevice9* device);  // render thread, before Present
  void DeviceLost();
  void DeviceRestored();
  void WindowCreated();

  // The camera applied to the last frame (for the overlay and "key from view").
  std::optional<iris::CameraState> LastAppliedCamera();
  void SetLastApplied(const iris::CameraState& camera);
  // Whether the mouse flies the free camera right now.
  bool IsFlying();
  // Toggles the editor (main thread or window procedure).
  void ToggleEditor();

  // Project file operations (main thread).
  bool SaveProject(const std::string& path, std::string* error);
  bool LoadProject(const std::string& path, std::string* error);
  void NewProject();
  void ResetLook();      // restore every dvar IW4Iris changed
  // Adds a key to the edit-target dolly at the playhead from the current view.
  void AddKeyFromView();
  // Plays the whole demo fast to find its moments, then returns to where it
  // was (main thread; the session must not be locked by the caller).
  void StartMomentAnalysis();
  void StopMomentAnalysis();
}

namespace IW4Iris
{
  class Session;
  namespace Runtime
  {
    iris::RigContext MakeRigContextFor(const Session& session);
  }
}
