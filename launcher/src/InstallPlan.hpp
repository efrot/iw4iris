// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Portable planning of the isolated cinematic environment: from a listing of
// the game folder, decide what is linked, what is copied and what the
// environment keeps for itself. No file-system access here, so the rules are
// unit-tested on any platform.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iris::launcher
{
  struct GameEntry
  {
    std::string name;  // file or folder name in the game root
    bool directory = false;
    std::uint64_t size = 0;
    bool reparsePoint = false;  // already a link / junction
  };

  enum class Action
  {
    Junction,   // directory junction to the game's folder (read by the game, never written)
    Copy,       // copied (or hard-linked) file
    Skip,       // left out
  };

  struct PlannedEntry
  {
    std::string name;
    Action action = Action::Skip;
    std::string reason;
  };

  struct Validation
  {
    bool ok = false;
    std::vector<std::string> missing;   // required entries that are absent
    std::vector<std::string> warnings;
  };

  // The game folder must contain the IW4x executable and the game data.
  Validation ValidateGameFolder(const std::vector<GameEntry>& entries);

  // Folders the environment owns (never linked from the game).
  bool IsEnvironmentOwned(const std::string& name);

  // What to do with each entry of the game root. `environmentName` is the
  // folder name of the environment if it lies inside the game folder, so it
  // is never linked into itself.
  std::vector<PlannedEntry> PlanEnvironment(const std::vector<GameEntry>& entries, const std::string& environmentName = {});

  // Case-insensitive comparison of file names (Windows semantics).
  bool SameName(const std::string& a, const std::string& b);

  // The IW4Iris Director Mod (mods/iw4iris_director/mod.json): which script
  // files the launcher installs into the environment. Only relative .gsc
  // paths inside the target folder are accepted, so a manifest can never
  // make the launcher write anywhere else.
  struct ModManifest
  {
    std::string id;
    std::string name;
    std::string version;
    int protocol = 0;
    std::string target;               // "userraw": the environment's raw folder
    std::vector<std::string> files;   // relative paths with forward slashes
  };
  std::optional<ModManifest> ParseModManifest(const std::string& json, std::string* error = nullptr);
  // Accepts "scripts/mp/x.gsc"; refuses absolute paths, "..", drive letters,
  // empty parts and anything that is not a .gsc file.
  bool IsSafeModFile(const std::string& relativePath);

  // Steam's libraryfolders.vdf lists the library folders ("path" values).
  std::vector<std::string> ParseSteamLibraryFolders(const std::string& vdf);
  // The folder name Steam uses for Modern Warfare 2 (2009), app 10180.
  inline constexpr const char* kSteamFolderName = "Call of Duty Modern Warfare 2";
}
