// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Log.hpp"

#include <chrono>
#include <cstdio>
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
    // Keep the previous session's log for crash reports.
    auto previous = path;
    previous += ".previous";
    std::filesystem::remove(previous, ec);
    std::filesystem::rename(path, previous, ec);
    file = _wfopen(path.wstring().c_str(), L"wb");
  }

  void Write(Level level, std::string_view message)
  {
    const auto now = std::chrono::system_clock::now();
    const auto line = std::format("{:%Y-%m-%d %H:%M:%S} [{}] {}", std::chrono::floor<std::chrono::milliseconds>(now),
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
