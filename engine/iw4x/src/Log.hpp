// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Structured, thread-safe logging to <game>/iw4iris/logs/iw4iris.log (and
// the IW4x console for warnings and errors). Keeps the last lines in memory
// for the editor's diagnostics panel.
#pragma once

#include <deque>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace IW4Iris::Log
{
  enum class Level
  {
    Debug,
    Info,
    Warning,
    Error,
  };

  void Open(const std::filesystem::path& file);
  void Write(Level level, std::string_view message);
  std::vector<std::string> Tail(std::size_t count);

  template <typename... Args>
  void Info(std::format_string<Args...> fmt, Args&&... args)
  {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void Warn(std::format_string<Args...> fmt, Args&&... args)
  {
    Write(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void Error(std::format_string<Args...> fmt, Args&&... args)
  {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void Debug(std::format_string<Args...> fmt, Args&&... args)
  {
    Write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
  }
}
