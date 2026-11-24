// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Cinematic.hpp"
#include "../Window.hpp"
#include "Capture.hpp"
#include "Log.hpp"
#include "Runtime.hpp"
#include "Session.hpp"

namespace Components
{
  Cinematic::Cinematic()
  {
    // Never on a dedicated server, and never in a session that could reach
    // the network (Runtime::Initialize installs the offline guard first).
    if (Dedicated::IsEnabled() || ZoneBuilder::IsEnabled())
      return;

    ::IW4Iris::Runtime::Initialize();

    Window::OnCreate([] { ::IW4Iris::Runtime::WindowCreated(); });
    Renderer::OnBackendFrame([](IDirect3DDevice9* device) { ::IW4Iris::Runtime::BackendFrame(device); });
    Renderer::OnDeviceRecoveryBegin([] { ::IW4Iris::Runtime::DeviceLost(); });
    Renderer::OnDeviceRecoveryEnd([] { ::IW4Iris::Runtime::DeviceRestored(); });
    Scheduler::Loop([] { ::IW4Iris::Runtime::MainFrame(); }, Scheduler::Pipeline::MAIN);
    Scheduler::Loop([] { ::IW4Iris::Runtime::RendererFrame(); }, Scheduler::Pipeline::RENDERER);
    Scheduler::OnGameShutdown([] { ::IW4Iris::Runtime::Shutdown(); });

    Command::Add("iw4iris_toggle", [] { ::IW4Iris::Runtime::ToggleEditor(); });
    Command::Add("iw4iris_key", [] { ::IW4Iris::Runtime::AddKeyFromView(); });
    Command::Add("iw4iris_resetlook", [] { ::IW4Iris::Runtime::ResetLook(); });
    Command::Add("iw4iris_save", [](const Command::Params* params) {
      std::string error;
      if (!::IW4Iris::Runtime::SaveProject(params->size() > 1 ? params->get(1) : "", &error))
        Logger::Print("iw4iris_save: {}\n", error);
    });
    Command::Add("iw4iris_open", [](const Command::Params* params) {
      if (params->size() < 2)
      {
        Logger::Print("usage: iw4iris_open <project.iw4iris>\n");
        return;
      }
      std::string error;
      if (!::IW4Iris::Runtime::LoadProject(params->get(1), &error))
        Logger::Print("iw4iris_open: {}\n", error);
    });
    Command::Add("iw4iris_record", [] {
      auto& s = ::IW4Iris::GetSession();
      iris::Project project;
      {
        std::lock_guard _(s.mutex);
        project = s.project;
      }
      std::string error;
      if (!::IW4Iris::Capture::Start(project, project.recording, &error))
        Logger::Print("iw4iris_record: {}\n", error);
    });
    Command::Add("iw4iris_status", [] {
      const auto p = ::IW4Iris::Capture::GetProgress();
      Logger::Print("IW4Iris: capture {} {}/{} frames, {}\n", ::IW4Iris::Capture::ToString(p.state), p.framesCaptured,
                    p.framesTotal, p.message);
    });
  }

  Cinematic::~Cinematic()
  {
    ::IW4Iris::Runtime::Shutdown();
  }
}
