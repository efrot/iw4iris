// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Clock.hpp"
#include "ImportPatch.hpp"
#include "Log.hpp"

#include <atomic>
#include <mutex>

namespace IW4Iris::Clock
{
  namespace
  {
    using timeGetTime_t = DWORD(WINAPI*)();
    using QueryPerformanceCounter_t = BOOL(WINAPI*)(LARGE_INTEGER*);

    timeGetTime_t realTimeGetTime = nullptr;
    QueryPerformanceCounter_t realQpc = nullptr;
    bool installed = false;

    std::atomic<bool> active{false};
    std::mutex mutex;
    // Virtual time = baseReal + offsetMs, in timeGetTime milliseconds; the
    // QPC view is derived from the same offset.
    DWORD baseTgt = 0;
    LONGLONG baseQpc = 0;
    LONGLONG qpcFrequency = 1;
    std::int64_t offsetMs = 0;
    std::atomic<std::uint32_t> readsThisFrame{0};
    std::atomic<std::int64_t> driftMs{0};
    constexpr std::uint32_t kWatchdogReads = 200000;

    void Watchdog()
    {
      if (++readsThisFrame >= kWatchdogReads)
      {
        readsThisFrame = 0;
        std::lock_guard _(mutex);
        ++offsetMs;
        ++driftMs;
      }
    }

    DWORD WINAPI VirtualTimeGetTime()
    {
      if (!active)
        return realTimeGetTime();
      Watchdog();
      std::lock_guard _(mutex);
      return baseTgt + static_cast<DWORD>(offsetMs);
    }

    BOOL WINAPI VirtualQpc(LARGE_INTEGER* counter)
    {
      if (!active || !counter)
        return realQpc(counter);
      Watchdog();
      std::lock_guard _(mutex);
      counter->QuadPart = baseQpc + offsetMs * qpcFrequency / 1000;
      return TRUE;
    }
  }

  bool Install()
  {
    if (installed)
      return true;
    const HMODULE exe = GetModuleHandleA(nullptr);
    void* tgt = nullptr;
    void* qpc = nullptr;
    const int a = ImportPatch::Replace(exe, "winmm.dll", "timeGetTime", reinterpret_cast<void*>(&VirtualTimeGetTime), &tgt);
    const int b = ImportPatch::Replace(exe, "kernel32.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(&VirtualQpc), &qpc);
    realTimeGetTime = tgt ? reinterpret_cast<timeGetTime_t>(tgt) : &timeGetTime;
    realQpc = qpc ? reinterpret_cast<QueryPerformanceCounter_t>(qpc) : &QueryPerformanceCounter;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    qpcFrequency = f.QuadPart > 0 ? f.QuadPart : 1;
    installed = a > 0 || b > 0;
    Log::Info("Render clock: {} timeGetTime and {} QueryPerformanceCounter import(s) of the game virtualised", a, b);
    if (!installed)
      Log::Warn("Render clock: the game imports neither timer; offline captures are unavailable");
    return installed;
  }

  bool Installed()
  {
    return installed;
  }

  void Begin()
  {
    std::lock_guard _(mutex);
    baseTgt = realTimeGetTime();
    LARGE_INTEGER c{};
    realQpc(&c);
    baseQpc = c.QuadPart;
    offsetMs = 0;
    driftMs = 0;
    readsThisFrame = 0;
    active = true;
  }

  void End()
  {
    active = false;
  }

  bool Active()
  {
    return active;
  }

  void Advance(std::int32_t ms)
  {
    std::lock_guard _(mutex);
    offsetMs += ms;
    readsThisFrame = 0;
  }

  std::int64_t WatchdogDriftMs()
  {
    return driftMs;
  }

  bool Verify()
  {
    if (!installed)
      return false;
    const bool wasActive = active;
    if (!wasActive)
      Begin();
    const int before = Game::Sys_Milliseconds();
    Advance(1234);
    const int after = Game::Sys_Milliseconds();
    Advance(-1234);
    if (!wasActive)
      End();
    const bool ok = after - before == 1234;
    if (!ok)
      Log::Error("Render clock: Sys_Milliseconds moved by {} ms instead of 1234 ms; the engine does not use the "
                 "virtualised timers", after - before);
    return ok;
  }
}
