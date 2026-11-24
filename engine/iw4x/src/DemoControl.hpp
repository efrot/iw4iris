// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Demo playback control on the game's main thread.
//
//   * Play: the demo is copied to <game>/userraw/demos/iw4iris/ (userraw is a
//     search path IW4x registers) and started with the engine's "demo"
//     command; the user's demo folders are never written to.
//   * Speed: the engine's timescale (Game::com_timescale).
//   * Pause: holds clientStatic_t::realtime, which drives the client's demo
//     clock, at the paused value (and the timescale at its minimum).
//   * Seek forward: advances clientStatic_t::realtime in steps so the client
//     reads the demo up to the target, as fast as the game can parse it.
//   * Seek backward: there is no engine state to rewind to, so the demo is
//     restarted (its first snapshot is the checkpoint) and played forward to
//     the target. Exact, but takes as long as parsing up to the target.
//
// Scene time is the demo's time in seconds since its first snapshot.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace IW4Iris::DemoControl
{
  bool Play(const std::filesystem::path& demo, std::string* error);
  void Stop();

  void Update();  // once per frame

  double SceneTime();
  bool HasScene();          // a demo is playing and its first snapshot is known
  bool Seek(double sceneTime);
  bool IsSeeking();
  void CancelSeek();

  void SetPaused(bool paused);
  bool IsPaused();
  // Advances a paused demo by `seconds` of scene time.
  void Step(double seconds);
  // Playback speed (timescale) while not paused; clamped to the dvar's range.
  void SetSpeed(double speed);
  double Speed();
}
