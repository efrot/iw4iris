// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Portable rules of the self-update: which release is newer, which files of
// a GitHub release are the package and its checksums, which download hosts
// are trusted and which archive paths may be written. No network or file
// access here, so every rule is unit-tested on any platform; Updater.cpp
// does the Windows work.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iris::launcher
{
  // Releases come only from this repository's GitHub Releases.
  inline constexpr std::string_view kUpdateRepository = "efrot/iw4iris";
  inline constexpr std::string_view kChecksumsAsset = "SHA256SUMS.txt";

  struct Version
  {
    int major = 0;
    int minor = 0;
    int patch = 0;
  };

  // "v0.6.0", "0.6.0", "IW4Iris 0.6.0" -> 0.6.0; nullopt for anything else
  // (pre-release suffixes such as "-rc1" are refused: updates are releases).
  std::optional<Version> ParseVersion(std::string_view text);
  bool IsNewer(const Version& candidate, const Version& current);
  std::string ToString(const Version& v);

  // The package's file name in a release: "IW4Iris-v0.6.0-Windows-x86.zip".
  std::string PackageAssetName(const Version& v);

  struct ReleaseAsset
  {
    std::string name;
    std::string url;  // browser_download_url
    std::uint64_t size = 0;
  };

  struct ReleaseInfo
  {
    Version version;
    std::string tag;
    std::string pageUrl;  // the release's page on GitHub (built here, not taken from the answer)
    std::string notes;    // release text, as published
    ReleaseAsset package;
    ReleaseAsset checksums;
  };

  // Reads GitHub's "latest release" answer (REST API, JSON). Drafts and
  // pre-releases are refused; the release must carry the package for its
  // own version and SHA256SUMS.txt, both downloadable from this repository.
  std::optional<ReleaseInfo> ParseRelease(std::string_view json, std::string* error = nullptr);

  // The lower-case SHA-256 a sha256sum-style file gives for a file name
  // ("<hash>  <name>" or "<hash> *<name>").
  std::optional<std::string> ChecksumFor(std::string_view sums, std::string_view fileName);

  // https only, and only GitHub's hosts for this repository's releases:
  // github.com/<repo>/releases/download/..., api.github.com/repos/<repo>/...
  // and the *.githubusercontent.com hosts GitHub redirects downloads to.
  bool IsTrustedUpdateUrl(std::string_view url);

  // A relative path inside the package for an archive entry ("client/iw4x.dll"),
  // or nullopt when the entry must not be written: absolute or drive paths,
  // "..", ".", alternate data streams (':'), Windows device names, names
  // Windows would change (trailing dot or space), control characters, and
  // the launcher's own working folders (.iw4iris-update, .git).
  std::optional<std::string> SafeArchivePath(std::string_view entry);

  // Files every package has; an archive without them is not an IW4Iris package.
  const std::vector<std::string>& RequiredPackageFiles();
  std::vector<std::string> MissingPackageFiles(const std::vector<std::string>& safePaths);
}
