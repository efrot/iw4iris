// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The isolated cinematic environment (Windows). The environment is a folder
// of its own (by default %LOCALAPPDATA%\IW4Iris\game) holding:
//
//   * directory junctions to the game's data folders (main, zone, iw4x, ...),
//     which the game only reads;
//   * copies (or hard links) of the small files of the game root, such as
//     iw4x.exe and the game's DLLs;
//   * the cinematic build's iw4x.dll and IW4Iris' own files;
//   * its own players\ and userraw\ folders, so configs and temporary demos
//     never touch the normal installation.
//
// Everything created is recorded in iw4iris-environment.json, and repair and
// uninstall only touch what is listed there. Removal never follows a
// junction: a junction is removed as a link, the game folder it points to is
// left alone.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "InstallPlan.hpp"

namespace iris::launcher
{
  using LogFn = std::function<void(const std::string&)>;

  std::vector<std::filesystem::path> DetectGameFolders();
  std::vector<GameEntry> ListFolder(const std::filesystem::path& folder);
  std::filesystem::path DefaultEnvironmentFolder();

  struct SetupOptions
  {
    std::filesystem::path gameFolder;
    std::filesystem::path environmentFolder;
    std::filesystem::path cinematicDll;      // the IW4Iris build of iw4x.dll
    std::filesystem::path resourcesFolder;   // fonts, tools (from the release package)
    bool copyGameSettings = true;            // copy players\*.cfg once
  };

  bool SetupEnvironment(const SetupOptions& options, const LogFn& log, std::string* error);
  bool EnvironmentExists(const std::filesystem::path& environmentFolder);
  std::optional<std::filesystem::path> EnvironmentGameFolder(const std::filesystem::path& environmentFolder);
  // Removes what the launcher created. Projects and recordings stay unless
  // `removeUserData` is set.
  bool UninstallEnvironment(const std::filesystem::path& environmentFolder, bool removeUserData, const LogFn& log,
                            std::string* error);

  bool LaunchGame(const std::filesystem::path& environmentFolder, const std::wstring& arguments, std::string* error);

  std::optional<std::filesystem::path> FindFfmpeg(const std::filesystem::path& environmentFolder);
  // Writes the launcher's choices (FFmpeg path, demo folders) into the
  // editor's settings.json, keeping its other settings.
  bool WriteEditorSettings(const std::filesystem::path& environmentFolder, const std::optional<std::filesystem::path>& ffmpeg,
                           const std::vector<std::filesystem::path>& demoFolders, std::string* error);

  bool CreateJunction(const std::filesystem::path& link, const std::filesystem::path& target, std::string* error);
  bool IsReparsePoint(const std::filesystem::path& path);
  // Deletes a file, an empty directory or a link; directories are emptied
  // first without ever descending into a reparse point.
  bool SafeRemove(const std::filesystem::path& path, std::string* error);

  std::string Narrow(const std::wstring& w);
  std::wstring Widen(const std::string& s);
  // A path from UTF-8 text (std::filesystem::u8path is deprecated in C++20).
  inline std::filesystem::path Utf8Path(const std::string& s) { return std::filesystem::path(Widen(s)); }
}
