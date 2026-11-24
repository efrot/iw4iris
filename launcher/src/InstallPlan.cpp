// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "InstallPlan.hpp"

#include <algorithm>
#include <cctype>

namespace iris::launcher
{
  bool SameName(const std::string& a, const std::string& b)
  {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
             return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
  }

  namespace
  {
    bool Has(const std::vector<GameEntry>& entries, const std::string& name, bool directory)
    {
      return std::any_of(entries.begin(), entries.end(),
                         [&](const GameEntry& e) { return e.directory == directory && SameName(e.name, name); });
    }

    std::string Lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }
  }

  Validation ValidateGameFolder(const std::vector<GameEntry>& entries)
  {
    Validation v;
    if (!Has(entries, "iw4x.exe", false))
      v.missing.push_back("iw4x.exe (install IW4x first: https://iw4x.io/install)");
    if (!Has(entries, "main", true))
      v.missing.push_back("main\\ (the game's data folder)");
    if (!Has(entries, "zone", true))
      v.missing.push_back("zone\\ (the game's fast files)");
    if (!Has(entries, "iw4x.dll", false) && !Has(entries, "iw4x", true))
      v.warnings.push_back("No iw4x.dll or iw4x\\ folder found next to iw4x.exe; the IW4x installation may be incomplete.");
    if (Has(entries, "iw4mp.exe", false) == false)
      v.warnings.push_back("iw4mp.exe not found; this is unusual for a Steam installation of Modern Warfare 2.");
    v.ok = v.missing.empty();
    return v;
  }

  bool IsEnvironmentOwned(const std::string& name)
  {
    static const char* owned[] = {"iw4iris", "players", "userraw", "demos", "iw4x.dll"};
    for (const char* o : owned)
      if (SameName(name, o))
        return true;
    return false;
  }

  std::vector<PlannedEntry> PlanEnvironment(const std::vector<GameEntry>& entries, const std::string& environmentName)
  {
    std::vector<PlannedEntry> plan;
    for (const auto& e : entries)
    {
      PlannedEntry p;
      p.name = e.name;
      const std::string lower = Lower(e.name);
      if (!environmentName.empty() && SameName(e.name, environmentName))
      {
        p.reason = "the environment itself";
      }
      else if (IsEnvironmentOwned(e.name))
      {
        p.reason = "kept separate in the cinematic environment";
      }
      else if (e.directory)
      {
        p.action = Action::Junction;
        p.reason = e.reparsePoint ? "linked (it is a link in the game folder too)" : "game data, linked read-only";
      }
      else if (lower.ends_with(".exe") || lower.ends_with(".dll") || lower.ends_with(".txt") || lower.ends_with(".cfg") ||
               lower.ends_with(".json") || lower.ends_with(".ini") || lower.ends_with(".bin") || lower.ends_with(".dat") ||
               lower.ends_with(".flt"))
      {
        p.action = Action::Copy;
        p.reason = "small game file, copied";
      }
      else if (lower.ends_with(".log") || lower.ends_with(".dmp") || lower.ends_with(".mdmp") || lower.ends_with(".tmp"))
      {
        p.reason = "log or crash dump";
      }
      else
      {
        p.action = e.size <= 256ull * 1024 * 1024 ? Action::Copy : Action::Skip;
        p.reason = p.action == Action::Copy ? "other game file, copied" : "large unknown file, not needed";
      }
      plan.push_back(std::move(p));
    }
    return plan;
  }

  std::vector<std::string> ParseSteamLibraryFolders(const std::string& vdf)
  {
    // Lines look like:   "path"		"D:\\SteamLibrary"
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = vdf.find("\"path\"", pos)) != std::string::npos)
    {
      pos += 6;
      const auto open = vdf.find('"', pos);
      if (open == std::string::npos)
        break;
      std::string value;
      std::size_t i = open + 1;
      for (; i < vdf.size() && vdf[i] != '"'; ++i)
      {
        if (vdf[i] == '\\' && i + 1 < vdf.size())
        {
          value.push_back(vdf[i + 1]);
          ++i;
        }
        else
          value.push_back(vdf[i]);
      }
      if (!value.empty())
        out.push_back(value);
      pos = i;
    }
    return out;
  }
}
