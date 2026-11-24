// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Offline recording. A capture renders the recording range frame by frame:
//
//   1. seek the demo to just before the range (main thread),
//   2. switch the game to the virtual render clock (Clock) so each game frame
//      advances the scene by exactly the time of one output frame,
//   3. render a few warm-up frames that carry a small colour tag in the
//      corner; the render thread reads the tags back to learn how many
//      frames the renderer runs behind the game (calibration), then
//   4. read every frame of the range from the back buffer before the editor
//      is drawn (so recordings are clean) and hand it to the encoder through
//      a bounded queue (the game waits when the encoder is behind).
#pragma once

#include <iris/project.hpp>
#include <iris/recording.hpp>

#include <cstdint>
#include <optional>
#include <string>

struct IDirect3DDevice9;

namespace IW4Iris::Capture
{
  enum class State
  {
    Idle,
    Seeking,
    Warmup,
    Recording,
    Finishing,
    Done,
    Failed,
    Cancelled,
  };
  const char* ToString(State state);

  struct Progress
  {
    State state = State::Idle;
    std::int64_t framesCaptured = 0;
    std::int64_t framesEncoded = 0;
    std::int64_t framesTotal = 0;
    double elapsedSeconds = 0.0;
    std::string message;
    std::string outputPath;
    int latencyFrames = 0;
    bool syncVerified = false;
    std::int64_t clockDriftMs = 0;
  };

  // Validates the settings against the running game; returns the issues
  // (errors block a capture).
  std::vector<iris::Issue> Check(const iris::RecordingSettings& settings);
  std::optional<std::string> FindFfmpeg(const std::string& configured);

  // Main thread. Returns false (and the reason) when the capture cannot
  // start.
  bool Start(const iris::Project& project, const iris::RecordingSettings& settings, std::string* error);
  void Cancel();
  bool IsActive();
  Progress GetProgress();

  // Main thread, once per frame (Scheduler MAIN pipeline): advances the
  // render clock and runs the capture's state machine.
  std::optional<double> MainFrame();
  // Main thread, from Hooks::OnViewReady: the cinematic time the frame being
  // built must show while a capture drives the timeline.
  std::optional<double> FrameTime();
  // Main thread, in the frame's 2D pass (Scheduler RENDERER pipeline).
  void DrawSyncTag();
  // Render thread, before the editor is drawn.
  void BackendFrame(IDirect3DDevice9* device);
  void OnDeviceLost();
}
