// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace IW4Iris::Log
{
  namespace
  {
    std::mutex mutex;
    std::FILE* file = nullptr;
    std::deque<std::string> recent;
    constexpr std::size_t kRecent = 400;

    const char* Name(Level level)
    {
      switch (level)
      {
      case Level::Debug: return "debug";
      case Level::Info: return "info";
      case Level::Warning: return "warn";
      case Level::Error: return "error";
      }
      return "info";
    }
  }

  void Open(const std::filesystem::path& path)
  {
    std::lock_guard _(mutex);
    if (file)
      return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // Keep the last sessions' logs for problem reports: a session that hung
    // is usually followed by one or two restarts, which must not overwrite
    // it. iw4iris.log.previous is the last session, .previous2 the one
    // before, up to .previous5.
    constexpr int kKeep = 5;
    auto Previous = [&](int n) {
      auto p = path;
      p += n == 1 ? std::string(".previous") : ".previous" + std::to_string(n);
      return p;
    };
    std::filesystem::remove(Previous(kKeep), ec);
    for (int n = kKeep - 1; n >= 1; --n)
      std::filesystem::rename(Previous(n), Previous(n + 1), ec);
    std::filesystem::rename(path, Previous(1), ec);
    file = _wfopen(path.wstring().c_str(), L"wb");
    // Lines logged before the game's folder was known (offline guard,
    // render clock, window) are kept in memory: write them first.
    if (file)
    {
      for (const auto& line : recent)
      {
        std::fwrite(line.data(), 1, line.size(), file);
        std::fputc('\n', file);
      }
      std::fflush(file);
    }
  }

  void Write(Level level, std::string_view message)
  {
    // Local time: users compare these lines with what they did and when.
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &t);
    const auto line = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] {}", local.tm_year + 1900,
                                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, ms,
                                  Name(level), message);
    std::lock_guard _(mutex);
    recent.push_back(line);
    while (recent.size() > kRecent)
      recent.pop_front();
    if (file)
    {
      std::fwrite(line.data(), 1, line.size(), file);
      std::fputc('\n', file);
      std::fflush(file);
    }
  }

  std::vector<std::string> Tail(std::size_t count)
  {
    std::lock_guard _(mutex);
    const auto n = std::min(count, recent.size());
    return {recent.end() - static_cast<std::ptrdiff_t>(n), recent.end()};
  }
}
