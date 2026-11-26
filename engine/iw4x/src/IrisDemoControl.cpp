// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "DemoControl.hpp"
#include "Engine.hpp"
#include "Log.hpp"
#include "Session.hpp"

#include <iris/demo.hpp>
#include <iris/sync.hpp>

#include <cmath>
#include <thread>

namespace IW4Iris::DemoControl
{
  namespace
  {
    constexpr const char* kTempFolder = "iw4iris";
    constexpr int kSeekChunkMs = 500;    // realtime advanced per frame while seeking forward
    constexpr int kSeekToleranceMs = 25;

    double speed = 1.0;
    bool paused = false;
    int pausedRealtime = 0;
    std::optional<int> firstServerTime;
    struct Clock
    {
      int mainFirstServerTime = 0;
      int offsetMs = 0;
    };
    std::optional<Clock> clock;  // an angle on the main demo's clock
    std::string currentDemo;
    std::filesystem::path currentFile;

    enum class Seek
    {
      Idle,
      Restart,
      Forward,
    } seek = Seek::Idle;
    double seekTarget = 0.0;
    int seekFrames = 0;

    float MinTimescale()
    {
      if (const auto d = Engine::FloatDomain("timescale"))
        return std::max(d->first, 0.0001f);
      return 0.001f;
    }

    // Demo control changes the engine's timescale only while a demo plays
    // (pause holds it near 0, speed ramps change it). It must give it back:
    // a film set or a local map started afterwards would otherwise run at
    // the demo's paused timescale, about 1000 times too slow, and seem to
    // hang on the loading screen.
    bool ownsTimescale = false;

    void ApplyTimescale(double value)
    {
      ownsTimescale = std::abs(value - 1.0) > 1e-6;
      if (const auto d = Engine::FloatDomain("timescale"))
        value = std::clamp(value, static_cast<double>(d->first), static_cast<double>(d->second));
      // com_timescale is the engine's "timescale" dvar (Game::com_timescale in IW4x).
      if (Game::com_timescale && *Game::com_timescale)
        Game::Dvar_SetFloat(*Game::com_timescale, static_cast<float>(value));
    }

    // Scans the demo file in the background. Each demo started gets a new
    // generation; a scan that finishes after another demo was started is
    // dropped (it used to overwrite the newer demo's index: wrong length,
    // wrong seek points).
    void ScanIndexAsync(const std::filesystem::path& file, std::uint64_t generation)
    {
      std::thread([file, generation] {
        std::string error;
        auto index = iris::ScanDemoFile(file, Utils::Huffman::Decompress, &error);
        auto& s = GetSession();
        std::lock_guard _(s.mutex);
        if (generation != s.demoGeneration)
        {
          Log::Info("Demo index of {} dropped: another demo was opened meanwhile", file.filename().string());
          return;
        }
        if (index)
        {
          for (const auto& w : index->warnings)
            Log::Warn("Demo index: {}", w);
          Log::Info("Demo index: {} snapshots, {:.2f} s", index->snapshots.size(), index->DurationSeconds());
          s.demoIndex = std::move(index);
        }
        else
        {
          Log::Warn("Demo index: {}", error);
        }
      }).detach();
    }
  }

  bool Play(const std::filesystem::path& demo, std::string* error)
  {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(demo, ec))
    {
      if (error)
        *error = "The demo file does not exist: " + demo.string();
      return false;
    }
    const auto base = Engine::BasePath();
    if (base.empty())
    {
      if (error)
        *error = "The game's base path is not known yet.";
      return false;
    }
    // Copy into userraw/demos/iw4iris/ so the engine finds it on its search
    // path without touching the user's demo folders.
    const fs::path folder = fs::path(base) / "userraw" / "demos" / kTempFolder;
    fs::create_directories(folder, ec);
    const fs::path target = folder / demo.filename();
    if (fs::absolute(demo, ec) != fs::absolute(target, ec))
    {
      // copy_options::overwrite_existing is not reliable here: the MinGW
      // libstdc++ build reports "File exists" for an existing target (seen
      // in a user's log when a demo was opened a second time). Reuse an
      // identical copy, otherwise remove the old one and copy again.
      std::error_code sizeError, timeError;
      const bool same = fs::is_regular_file(target, ec) &&
                        fs::file_size(target, sizeError) == fs::file_size(demo, sizeError) && !sizeError &&
                        fs::last_write_time(target, timeError) == fs::last_write_time(demo, timeError) && !timeError;
      if (!same)
      {
        fs::remove(target, ec);
        std::error_code copyError;
        fs::copy_file(demo, target, copyError);
        if (!copyError)
          fs::last_write_time(target, fs::last_write_time(demo, ec), ec);
        std::error_code checkError;
        if (!fs::is_regular_file(target, checkError) ||
            fs::file_size(target, checkError) != fs::file_size(demo, checkError))
        {
          if (error)
            *error = "Could not copy the demo to " + target.string() +
                     (copyError ? ": " + copyError.message() : std::string(" (is the file open in another program?)"));
          return false;
        }
      }
    }

    auto stem = demo.stem().string();
    firstServerTime.reset();
    clock.reset();
    currentDemo = std::string(kTempFolder) + "/" + stem;
    currentFile = demo;
    ReleaseTime();
    std::uint64_t generation = 0;
    {
      auto& s = GetSession();
      std::lock_guard _(s.mutex);
      s.loadedDemoPath = demo;
      s.demoIndex.reset();
      generation = ++s.demoGeneration;
    }
    ScanIndexAsync(demo, generation);
    Log::Info("Playing demo {}", demo.string());
    Engine::Execute("demo " + currentDemo);
    return true;
  }

  void Stop()
  {
    ReleaseTime();
    Engine::Execute("disconnect");
  }

  void ReleaseTime()
  {
    seek = Seek::Idle;
    paused = false;
    if (ownsTimescale)
    {
      ApplyTimescale(1.0);
      Log::Info("Demo control: timescale back to 1");
    }
  }

  std::optional<int> FirstServerTime()
  {
    return firstServerTime;
  }

  void SetClock(int mainFirstServerTime, int offsetMs)
  {
    clock = Clock{mainFirstServerTime, offsetMs};
  }

  void ClearClock()
  {
    clock.reset();
  }

  double SceneShift()
  {
    if (!clock || !firstServerTime)
      return 0.0;
    return iris::AngleSceneShift(clock->mainFirstServerTime, *firstServerTime, clock->offsetMs);
  }

  double SceneTime()
  {
    if (!firstServerTime)
      return 0.0;
    return (Engine::ClientServerTime() - *firstServerTime) / 1000.0 + SceneShift();
  }

  bool HasScene()
  {
    return Engine::IsDemoPlaying() && Engine::IsCgameReady() && firstServerTime.has_value();
  }

  bool Seek(double sceneTime)
  {
    if (!Engine::IsDemoPlaying() || currentDemo.empty())
      return false;
    // seekTarget is in the demo's own time (since its first snapshot).
    seekTarget = std::max(0.0, sceneTime - SceneShift());
    seekFrames = 0;
    if (HasScene() && seekTarget * 1000.0 >= (Engine::ClientServerTime() - *firstServerTime) - kSeekToleranceMs)
    {
      seek = Seek::Forward;
    }
    else
    {
      // Backward: replay from the demo's first snapshot.
      seek = Seek::Restart;
      firstServerTime.reset();
      Log::Info("Seeking backwards to {:.3f} s: restarting the demo", seekTarget);
      Engine::Execute("demo " + currentDemo);
    }
    return true;
  }

  bool IsSeeking()
  {
    return seek != Seek::Idle;
  }

  void CancelSeek()
  {
    seek = Seek::Idle;
  }

  void SetPaused(bool p)
  {
    if (p == paused)
      return;
    paused = p;
    if (paused)
    {
      pausedRealtime = Engine::Realtime();
      ApplyTimescale(MinTimescale());
    }
    else
    {
      ApplyTimescale(speed);
    }
  }

  bool IsPaused()
  {
    return paused;
  }

  void Step(double seconds)
  {
    if (!paused)
      return;
    pausedRealtime += static_cast<int>(std::llround(seconds * 1000.0));
  }

  void SetSpeed(double s)
  {
    speed = std::max(0.0, s);
    if (!paused && seek == Seek::Idle)
      ApplyTimescale(speed);
  }

  double Speed()
  {
    return speed;
  }

  void Update()
  {
    if (!Engine::IsDemoPlaying())
    {
      if (seek == Seek::Restart)
        return;  // still loading the restarted demo
      firstServerTime.reset();
      // The demo ended or was left: pause and speed belonged to it.
      if (ownsTimescale || paused)
        ReleaseTime();
      seek = Seek::Idle;
      return;
    }

    // The first snapshot of the demo is its time origin. Prefer the demo
    // index (read from the file) and fall back to the first time seen.
    if (!firstServerTime && Engine::IsCgameReady() && Engine::SnapshotServerTime() > 0)
    {
      auto& s = GetSession();
      std::lock_guard _(s.mutex);
      if (s.demoIndex && s.demoIndex->FirstServerTime())
        firstServerTime = *s.demoIndex->FirstServerTime();
      else
        firstServerTime = Engine::SnapshotServerTime();
    }

    switch (seek)
    {
    case Seek::Idle:
      if (paused)
        Engine::SetRealtime(pausedRealtime);
      break;
    case Seek::Restart:
      if (HasScene())
      {
        seek = Seek::Forward;
        seekFrames = 0;
      }
      break;
    case Seek::Forward:
    {
      ++seekFrames;
      const int now = Engine::ClientServerTime() - *firstServerTime;
      const int target = static_cast<int>(std::llround(seekTarget * 1000.0));
      const int remaining = target - now;
      if (remaining <= kSeekToleranceMs || seekFrames > 20000)
      {
        seek = Seek::Idle;
        if (paused)
          pausedRealtime = Engine::Realtime();
        else
          ApplyTimescale(speed);
        if (seekFrames > 20000)
          Log::Warn("Seek gave up before reaching {:.3f} s", seekTarget);
        break;
      }
      ApplyTimescale(MinTimescale());
      Engine::SetRealtime(Engine::Realtime() + std::min(remaining, kSeekChunkMs));
      if (paused)
        pausedRealtime = Engine::Realtime();
      break;
    }
    }
  }
}
