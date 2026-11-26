// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Self-update from this repository's GitHub Releases (Windows part; the
// rules are in UpdatePlan.hpp):
//
//   1. GET api.github.com/repos/<repo>/releases/latest over HTTPS (WinHTTP,
//      TLS 1.2+). Redirects are followed by hand and every hop must be a
//      trusted URL (IsTrustedUpdateUrl).
//   2. Download the package zip and SHA256SUMS.txt; the zip's SHA-256
//      (BCrypt) must match. Size limits on everything.
//   3. Read the zip (miniz): every entry must be a safe relative path, the
//      package must contain the launcher, the client and the Director Mod;
//      everything is extracted to <package>\.iw4iris-update\staging first.
//   4. Replace the package's files one by one, moving each old file to
//      .iw4iris-update\backup (the running launcher can be renamed); any
//      failure moves every file back. Nothing outside the package folder is
//      written: no parent of a target may be a link or junction.
//   5. The caller starts the new launcher, which removes the backup.
//
// Projects, recordings and settings live in the cinematic environment and
// are never touched; files the new package no longer has are left in place.
#pragma once

#include "UpdatePlan.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace iris::launcher::update
{
  using LogFn = std::function<void(const std::string&)>;
  using ProgressFn = std::function<void(double)>;  // 0 .. 1 of the package download

  // The latest release of kUpdateRepository; nullopt (and *error) when it
  // cannot be read.
  std::optional<ReleaseInfo> FetchLatest(std::string* error);

  // A folder with .git is a developer's checkout: updated with git, never in place.
  bool IsDevelopmentCheckout(const std::filesystem::path& packageFolder);

  // Downloads, verifies and installs the release into the package folder
  // (the folder of IW4Iris Launcher.exe). On success the folder holds the new
  // version; restart the launcher to use it.
  bool Install(const std::filesystem::path& packageFolder, const ReleaseInfo& release, const LogFn& log,
               const ProgressFn& progress, std::string* error);

  // Installs a package that is already on disk (downloaded by hand, or for
  // testing): the same checks, extraction, replacement and rollback as
  // Install, without the download. The version is read from the file name
  // ("IW4Iris-v0.6.0-Windows-x86.zip"); *version receives it.
  bool InstallFromFile(const std::filesystem::path& packageFolder, const std::filesystem::path& zipFile, const LogFn& log,
                       std::string* version, std::string* error);

  // Called at start: removes what a finished update left behind (backup,
  // staging); returns the version it installed, if an update just finished.
  std::optional<std::string> FinishPreviousUpdate(const std::filesystem::path& packageFolder, const LogFn& log);

  // Starts the (new) launcher; the caller then closes its window.
  bool RestartLauncher(const std::filesystem::path& launcherExe, std::string* error);
}
