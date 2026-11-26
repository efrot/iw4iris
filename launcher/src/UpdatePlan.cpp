// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "UpdatePlan.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <tuple>

#include <nlohmann/json.hpp>

namespace iris::launcher
{
  namespace
  {
    std::string Lower(std::string_view s)
    {
      std::string out(s);
      std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return out;
    }

    bool ParseNumber(std::string_view s, int* out)
    {
      if (s.empty() || s.size() > 6)
        return false;
      int v = 0;
      for (const char c : s)
      {
        if (c < '0' || c > '9')
          return false;
        v = v * 10 + (c - '0');
      }
      *out = v;
      return true;
    }

    bool Fail(std::string* error, std::string message)
    {
      if (error)
        *error = std::move(message);
      return false;
    }
  }

  std::optional<Version> ParseVersion(std::string_view text)
  {
    // The last whitespace-separated word, without a leading 'v'.
    const auto space = text.find_last_of(" \t");
    if (space != std::string_view::npos)
      text.remove_prefix(space + 1);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
      text.remove_prefix(1);
    Version v;
    int* parts[] = {&v.major, &v.minor, &v.patch};
    for (int i = 0; i < 3; ++i)
    {
      const auto dot = text.find('.');
      const auto piece = i < 2 ? text.substr(0, dot) : text;
      if ((i < 2 && dot == std::string_view::npos) || !ParseNumber(piece, parts[i]))
        return std::nullopt;
      if (i < 2)
        text.remove_prefix(dot + 1);
    }
    return v;
  }

  bool IsNewer(const Version& candidate, const Version& current)
  {
    return std::tie(candidate.major, candidate.minor, candidate.patch) >
           std::tie(current.major, current.minor, current.patch);
  }

  std::string ToString(const Version& v)
  {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
  }

  std::string PackageAssetName(const Version& v)
  {
    return "IW4Iris-v" + ToString(v) + "-Windows-x86.zip";
  }

  bool IsTrustedUpdateUrl(std::string_view url)
  {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size() || Lower(url.substr(0, scheme.size())) != scheme)
      return false;
    url.remove_prefix(scheme.size());
    const auto slash = url.find('/');
    if (slash == std::string_view::npos)
      return false;
    const std::string host = Lower(url.substr(0, slash));
    const std::string_view path = url.substr(slash);
    // No user info, no port, nothing but a plain host name.
    if (host.empty() || host.find_first_of("@:\\") != std::string::npos)
      return false;
    const std::string repo(kUpdateRepository);
    auto startsWith = [](std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; };
    if (host == "github.com")
      return startsWith(path, "/" + repo + "/releases/download/");
    if (host == "api.github.com")
      return startsWith(path, "/repos/" + repo + "/");
    // GitHub answers release downloads with a redirect to signed, short-lived
    // URLs on these hosts.
    constexpr std::string_view cdn = ".githubusercontent.com";
    return host.size() > cdn.size() && host.compare(host.size() - cdn.size(), cdn.size(), cdn) == 0;
  }

  std::optional<ReleaseInfo> ParseRelease(std::string_view json, std::string* error)
  {
    nlohmann::json j;
    try
    {
      j = nlohmann::json::parse(json);
    }
    catch (const std::exception& e)
    {
      Fail(error, std::string("the release information is not valid JSON: ") + e.what());
      return std::nullopt;
    }
    if (!j.is_object())
    {
      Fail(error, "the release information is not a release");
      return std::nullopt;
    }
    if (j.value("draft", false) || j.value("prerelease", false))
    {
      Fail(error, "the latest release is a draft or a pre-release");
      return std::nullopt;
    }
    ReleaseInfo r;
    r.tag = j.value("tag_name", std::string());
    const auto version = ParseVersion(r.tag);
    if (!version)
    {
      Fail(error, "the release tag \"" + r.tag + "\" is not a version (vX.Y.Z)");
      return std::nullopt;
    }
    r.version = *version;
    r.notes = j.value("body", std::string());
    r.pageUrl = "https://github.com/" + std::string(kUpdateRepository) + "/releases/tag/v" + ToString(r.version);
    const std::string packageName = PackageAssetName(r.version);
    if (j.contains("assets") && j["assets"].is_array())
      for (const auto& a : j["assets"])
      {
        if (!a.is_object())
          continue;
        ReleaseAsset asset{a.value("name", std::string()), a.value("browser_download_url", std::string()),
                           a.value("size", std::uint64_t{0})};
        if (asset.name == packageName)
          r.package = asset;
        else if (asset.name == kChecksumsAsset)
          r.checksums = asset;
      }
    if (r.package.url.empty())
    {
      Fail(error, "release " + r.tag + " has no " + packageName);
      return std::nullopt;
    }
    if (r.checksums.url.empty())
    {
      Fail(error, "release " + r.tag + " has no " + std::string(kChecksumsAsset));
      return std::nullopt;
    }
    if (!IsTrustedUpdateUrl(r.package.url) || !IsTrustedUpdateUrl(r.checksums.url))
    {
      Fail(error, "release " + r.tag + " points its downloads outside this repository's releases");
      return std::nullopt;
    }
    return r;
  }

  std::optional<std::string> ChecksumFor(std::string_view sums, std::string_view fileName)
  {
    while (!sums.empty())
    {
      const auto end = sums.find('\n');
      std::string_view line = sums.substr(0, end);
      sums.remove_prefix(end == std::string_view::npos ? sums.size() : end + 1);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.remove_suffix(1);
      if (line.size() < 66)
        continue;
      const std::string hash = Lower(line.substr(0, 64));
      if (!std::all_of(hash.begin(), hash.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        continue;
      std::string_view name = line.substr(64);
      // "<hash>  <name>" (text mode) or "<hash> *<name>" (binary mode).
      if (name.empty() || name.front() != ' ')
        continue;
      name.remove_prefix(1);
      if (!name.empty() && (name.front() == ' ' || name.front() == '*'))
        name.remove_prefix(1);
      if (name == fileName)
        return hash;
    }
    return std::nullopt;
  }

  std::optional<std::string> SafeArchivePath(std::string_view entry)
  {
    std::string path(entry);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty() || path.front() == '/' || path.size() > 240)
      return std::nullopt;
    static const std::array<std::string_view, 22> devices{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                                          "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                                          "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::size_t start = 0;
    bool first = true;
    while (start <= path.size())
    {
      const auto slash = path.find('/', start);
      const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
      const bool last = slash == std::string::npos;
      if (part.empty())
      {
        // Only a trailing slash (a directory entry) may leave an empty part.
        if (!last || first)
          return std::nullopt;
        break;
      }
      if (part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
        return std::nullopt;
      for (const unsigned char c : part)
        if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
          return std::nullopt;
      const std::string lower = Lower(part);
      const std::string stem = lower.substr(0, lower.find('.'));
      if (std::find(devices.begin(), devices.end(), stem) != devices.end())
        return std::nullopt;
      if (first && (lower == ".iw4iris-update" || lower == ".git"))
        return std::nullopt;
      first = false;
      if (last)
        break;
      start = slash + 1;
    }
    return path;
  }

  const std::vector<std::string>& RequiredPackageFiles()
  {
    static const std::vector<std::string> files{"IW4Iris Launcher.exe", "client/iw4x.dll",
                                                "mods/iw4iris_director/mod.json"};
    return files;
  }

  std::vector<std::string> MissingPackageFiles(const std::vector<std::string>& safePaths)
  {
    std::vector<std::string> missing;
    for (const auto& required : RequiredPackageFiles())
      if (std::find_if(safePaths.begin(), safePaths.end(),
                       [&](const std::string& p) { return Lower(p) == Lower(required); }) == safePaths.end())
        missing.push_back(required);
    return missing;
  }
}
