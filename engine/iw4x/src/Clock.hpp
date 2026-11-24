// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The offline render clock. While a capture runs, the game's time sources
// (the executable's timeGetTime and QueryPerformanceCounter imports, which
// Sys_Milliseconds is built on) report a virtual time that advances by an
// exact number of milliseconds per game frame, however long the frame took
// to render and encode. Outside captures the real time passes through.
//
// Safety: should the game wait for time to pass within one frame (a frame
// limiter, a timeout loop), the watchdog advances the virtual time by 1 ms
// after many reads in the same frame, and the capture reports the drift.
// Before a capture starts, Verify() checks that Sys_Milliseconds follows the
// virtual clock; when it does not, the capture refuses to start rather than
// produce mistimed footage.
#pragma once

#include <cstdint>

namespace IW4Iris::Clock
{
  bool Install();
  bool Installed();

  // Starts virtual time at the current real time.
  void Begin();
  void End();
  bool Active();

  // Advances the virtual time by `ms` (main thread, once per game frame).
  void Advance(std::int32_t ms);
  // Milliseconds of drift the watchdog had to add since Begin().
  std::int64_t WatchdogDriftMs();

  // Checks that the engine's Sys_Milliseconds moves with the virtual clock.
  bool Verify();
}
