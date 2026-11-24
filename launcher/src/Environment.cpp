// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Environment.hpp"

#include <windows.h>
#include <winioctl.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace iris::launcher
{
  namespace fs = std::filesystem;

  namespace
  {
    constexpr const char* kManifest = "iw4iris-environment.json";

    // REPARSE_DATA_BUFFER for a mount point (junction); not in every SDK's
    // user-mode headers.
    struct MountPointReparseBuffer
    {
      DWORD ReparseTag;
      WORD ReparseDataLength;
      WORD Reserved;
      WORD SubstituteNameOffset;
      WORD SubstituteNameLength;
      WORD PrintNameOffset;
      WORD PrintNameLength;
      WCHAR PathBuffer[1];
    };

    std::string LastError(const char* what)
    {
      return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
    }

    std::optional<std::wstring> RegistryString(HKEY root, const wchar_t* key, const wchar_t* value)
    {
      wchar_t buffer[MAX_PATH * 2]{};
      DWORD size = sizeof(buffer);
      if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
        return std::wstring(buffer);
      return std::nullopt;
    }

    std::string ReadText(const fs::path& file)
    {
      std::ifstream in(file, std::ios::binary);
      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }
  }

  std::string Narrow(const std::wstring& w)
  {
    if (w.empty())
      return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
  }

  std::wstring Widen(const std::string& s)
  {
    if (s.empty())
      return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
  }

  std::vector<fs::path> DetectGameFolders()
  {
    std::vector<fs::path> libraries;
    for (const auto& [root, key, value] : {std::tuple{HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"},
                                           std::tuple{HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"},
                                           std::tuple{HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"}})
    {
      if (const auto steam = RegistryString(root, key, value))
      {
        const fs::path steamPath(*steam);
        libraries.push_back(steamPath);
        const auto vdf = ReadText(steamPath / "steamapps" / "libraryfolders.vdf");
        for (const auto& lib : ParseSteamLibraryFolders(vdf))
          libraries.push_back(Utf8Path(lib));
      }
    }
    std::vector<fs::path> found;
    std::error_code ec;
    for (const auto& lib : libraries)
    {
      const auto game = (lib / "steamapps" / "common" / kSteamFolderName).lexically_normal();
      if (fs::exists(game / "iw4x.exe", ec) || fs::exists(game / "iw4mp.exe", ec))
        if (std::find(found.begin(), found.end(), game) == found.end())
          found.push_back(game);
    }
    return found;
  }

  std::vector<GameEntry> ListFolder(const fs::path& folder)
  {
    std::vector<GameEntry> out;
    std::error_code ec;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
    {
      GameEntry e;
      e.name = Narrow(it->path().filename().wstring());
      e.directory = it->is_directory(ec);
      e.size = e.directory ? 0 : it->file_size(ec);
      e.reparsePoint = IsReparsePoint(it->path());
      out.push_back(std::move(e));
    }
    return out;
  }

  fs::path DefaultEnvironmentFolder()
  {
    PWSTR local = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
      base = local;
    CoTaskMemFree(local);
    return base / "IW4Iris" / "game";
  }

  bool IsReparsePoint(const fs::path& path)
  {
    const DWORD attributes = GetFileAttributesW(path.wstring().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
  }

  bool CreateJunction(const fs::path& link, const fs::path& target, std::string* error)
  {
    if (!CreateDirectoryW(link.wstring().c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
      *error = LastError("CreateDirectory");
      return false;
    }
    HANDLE h = CreateFileW(link.wstring().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
      *error = LastError("Opening the junction");
      return false;
    }
    const std::wstring substitute = L"\\??\\" + fs::absolute(target).wstring();
    const std::wstring print = fs::absolute(target).wstring();
    const std::size_t pathBytes = (substitute.size() + 1 + print.size() + 1) * sizeof(WCHAR);
    std::vector<unsigned char> buffer(offsetof(MountPointReparseBuffer, PathBuffer) + pathBytes);
    auto* rb = reinterpret_cast<MountPointReparseBuffer*>(buffer.data());
    rb->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    rb->ReparseDataLength = static_cast<WORD>(buffer.size() - 8);
    rb->SubstituteNameOffset = 0;
    rb->SubstituteNameLength = static_cast<WORD>(substitute.size() * sizeof(WCHAR));
    rb->PrintNameOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(WCHAR));
    rb->PrintNameLength = static_cast<WORD>(print.size() * sizeof(WCHAR));
    std::memcpy(rb->PathBuffer, substitute.c_str(), (substitute.size() + 1) * sizeof(WCHAR));
    std::memcpy(rb->PathBuffer + substitute.size() + 1, print.c_str(), (print.size() + 1) * sizeof(WCHAR));
    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &bytes, nullptr);
    CloseHandle(h);
    if (!ok)
    {
      *error = LastError("FSCTL_SET_REPARSE_POINT");
      RemoveDirectoryW(link.wstring().c_str());
      return false;
    }
    return true;
  }

  bool SafeRemove(const fs::path& path, std::string* error)
  {
    const std::wstring w = path.wstring();
    const DWORD attributes = GetFileAttributesW(w.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
      return true;  // already gone
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
      // A junction or symbolic link: remove the link itself, never its target.
      const BOOL ok = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(w.c_str()) : DeleteFileW(w.c_str());
      if (!ok && error)
        *error = LastError("Removing a link");
      return ok != FALSE;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      std::error_code ec;
      std::vector<fs::path> children;
      for (fs::directory_iterator it(path, ec), end; it != end; it.increment(ec))
        children.push_back(it->path());
      for (const auto& c : children)
        if (!SafeRemove(c, error))
          return false;
      if (!RemoveDirectoryW(w.c_str()))
      {
        if (error)
          *error = LastError("RemoveDirectory");
        return false;
      }
      return true;
    }
    SetFileAttributesW(w.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFileW(w.c_str()))
    {
      if (error)
        *error = LastError("DeleteFile");
      return false;
    }
    return true;
  }

  bool EnvironmentExists(const fs::path& env)
  {
    std::error_code ec;
    return fs::exists(env / kManifest, ec);
  }

  std::optional<fs::path> EnvironmentGameFolder(const fs::path& env)
  {
    try
    {
      const auto j = nlohmann::json::parse(ReadText(env / kManifest));
      return Utf8Path(j.at("game").get<std::string>());
    }
    catch (...)
    {
      return std::nullopt;
    }
  }

  bool SetupEnvironment(const SetupOptions& o, const LogFn& log, std::string* error)
  {
    std::error_code ec;
    const auto entries = ListFolder(o.gameFolder);
    const auto validation = ValidateGameFolder(entries);
    if (!validation.ok)
    {
      *error = "The game folder is missing: ";
      for (const auto& m : validation.missing)
        *error += "\n  " + m;
      return false;
    }
    for (const auto& w : validation.warnings)
      log("warning: " + w);
    if (!fs::exists(o.cinematicDll, ec))
    {
      *error = "The IW4Iris client (" + o.cinematicDll.string() + ") is missing from the launcher's folder.";
      return false;
    }
    // Never build the environment inside the game folder or vice versa.
    const auto game = fs::weakly_canonical(o.gameFolder, ec);
    const auto envAbs = fs::weakly_canonical(o.environmentFolder, ec);
    if (envAbs == game)
    {
      *error = "The cinematic environment cannot be the game folder itself.";
      return false;
    }
    fs::create_directories(o.environmentFolder, ec);
    if (ec)
    {
      *error = "Cannot create " + o.environmentFolder.string() + ": " + ec.message();
      return false;
    }

    nlohmann::json created = nlohmann::json::array();
    std::string envName;
    if (envAbs.parent_path() == game)
      envName = Narrow(envAbs.filename().wstring());

    for (const auto& p : PlanEnvironment(entries, envName))
    {
      const auto source = o.gameFolder / Utf8Path(p.name);
      const auto target = o.environmentFolder / Utf8Path(p.name);
      if (p.action == Action::Junction)
      {
        if (IsReparsePoint(target))
        {
          created.push_back({{"path", p.name}, {"kind", "junction"}});
          continue;
        }
        if (fs::exists(target, ec))
        {
          log("skipped " + p.name + ": something else already exists there");
          continue;
        }
        std::string err;
        if (!CreateJunction(target, source, &err))
        {
          *error = "Could not link " + p.name + ": " + err;
          return false;
        }
        created.push_back({{"path", p.name}, {"kind", "junction"}});
        log("linked " + p.name);
      }
      else if (p.action == Action::Copy)
      {
        // Hard link on the same volume, else a copy.
        if (fs::exists(target, ec))
          SafeRemove(target, nullptr);
        const bool linked = CreateHardLinkW(target.wstring().c_str(), source.wstring().c_str(), nullptr) != FALSE;
        if (!linked && !CopyFileW(source.wstring().c_str(), target.wstring().c_str(), FALSE))
        {
          *error = "Could not copy " + p.name + ": " + LastError("CopyFile");
          return false;
        }
        created.push_back({{"path", p.name}, {"kind", linked ? "hardlink" : "copy"}});
      }
    }

    // The cinematic client and IW4Iris' own folders.
    const auto dll = o.environmentFolder / "iw4x.dll";
    if (fs::exists(dll, ec))
      SafeRemove(dll, nullptr);
    if (!CopyFileW(o.cinematicDll.wstring().c_str(), dll.wstring().c_str(), FALSE))
    {
      *error = "Could not install the IW4Iris client: " + LastError("CopyFile");
      return false;
    }
    created.push_back({{"path", "iw4x.dll"}, {"kind", "copy"}});
    for (const char* own : {"players", "userraw", "iw4iris", "iw4iris/projects", "iw4iris/recordings", "iw4iris/exports"})
      fs::create_directories(o.environmentFolder / own, ec);
    if (fs::is_directory(o.resourcesFolder / "fonts", ec))
      fs::copy(o.resourcesFolder / "fonts", o.environmentFolder / "iw4iris" / "fonts",
               fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (fs::is_directory(o.resourcesFolder / "tools", ec))
      fs::copy(o.resourcesFolder / "tools", o.environmentFolder / "iw4iris" / "tools",
               fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (o.copyGameSettings && fs::is_directory(o.gameFolder / "players", ec))
      for (fs::directory_iterator it(o.gameFolder / "players", ec), end; it != end; it.increment(ec))
        if (it->path().extension() == ".cfg" && !fs::exists(o.environmentFolder / "players" / it->path().filename(), ec))
          fs::copy_file(it->path(), o.environmentFolder / "players" / it->path().filename(), ec);

    const nlohmann::json manifest{{"format", "iw4iris-environment"},
                                  {"version", 1},
                                  {"game", Narrow(o.gameFolder.wstring())},
                                  {"created", created}};
    std::ofstream(o.environmentFolder / kManifest) << manifest.dump(2);
    log("environment ready in " + Narrow(o.environmentFolder.wstring()));
    return true;
  }

  bool UninstallEnvironment(const fs::path& env, bool removeUserData, const LogFn& log, std::string* error)
  {
    nlohmann::json manifest;
    try
    {
      manifest = nlohmann::json::parse(ReadText(env / kManifest));
    }
    catch (...)
    {
      *error = "No IW4Iris environment manifest in " + env.string() + "; nothing was removed.";
      return false;
    }
    for (const auto& entry : manifest.value("created", nlohmann::json::array()))
    {
      const auto name = entry.value("path", std::string());
      const auto kind = entry.value("kind", std::string());
      const auto path = env / Utf8Path(name);
      if (name.empty() || name.find_first_of("/\\") != std::string::npos || name == "." || name == "..")
        continue;  // only direct children of the environment
      std::error_code ec;
      // Remove a junction only while it still is one, and a copied file only
      // while it still is a file: never anything the user put there since.
      if (kind == "junction" && !IsReparsePoint(path))
      {
        log("kept " + name + ": it is no longer a link");
        continue;
      }
      if (kind != "junction" && (!fs::is_regular_file(path, ec) || IsReparsePoint(path)))
      {
        log("kept " + name + ": it is no longer the copied file");
        continue;
      }
      std::string err;
      if (!SafeRemove(path, &err))
        log("could not remove " + path.string() + ": " + err);
    }
    for (const char* own : {"players", "userraw"})
      SafeRemove(env / own, nullptr);
    if (removeUserData)
      SafeRemove(env / "iw4iris", nullptr);
    else
      for (const char* tool : {"iw4iris/fonts", "iw4iris/tools", "iw4iris/logs"})
        SafeRemove(env / tool, nullptr);
    SafeRemove(env / kManifest, nullptr);
    std::error_code ec;
    if (fs::is_empty(env, ec))
      RemoveDirectoryW(env.wstring().c_str());
    log(removeUserData ? "environment removed" : "environment removed; projects and recordings kept in iw4iris\\");
    return true;
  }

  bool LaunchGame(const fs::path& env, const std::wstring& arguments, std::string* error)
  {
    const auto exe = env / "iw4x.exe";
    std::wstring commandLine = L"\"" + exe.wstring() + L"\"";
    if (!arguments.empty())
      commandLine += L" " + arguments;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.wstring().c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        env.wstring().c_str(), &si, &pi))
    {
      *error = LastError("Starting iw4x.exe");
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  }

  std::optional<fs::path> FindFfmpeg(const fs::path& env)
  {
    std::error_code ec;
    for (const auto& candidate : {env / "iw4iris" / "ffmpeg" / "ffmpeg.exe", env / "iw4iris" / "ffmpeg.exe", env / "ffmpeg.exe"})
      if (fs::is_regular_file(candidate, ec))
        return candidate;
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, found, nullptr))
      return fs::path(found);
    return std::nullopt;
  }

  bool WriteEditorSettings(const fs::path& env, const std::optional<fs::path>& ffmpeg, const std::vector<fs::path>& demoFolders,
                           std::string* error)
  {
    const auto file = env / "iw4iris" / "settings.json";
    nlohmann::json j = nlohmann::json::object();
    try
    {
      std::error_code ec;
      if (fs::exists(file, ec))
        j = nlohmann::json::parse(ReadText(file));
    }
    catch (...)
    {
      j = nlohmann::json::object();
    }
    if (ffmpeg)
      j["ffmpegPath"] = Narrow(ffmpeg->wstring());
    nlohmann::json folders = j.value("demoFolders", nlohmann::json::array());
    for (const auto& f : demoFolders)
    {
      const auto s = Narrow(f.wstring());
      if (std::find(folders.begin(), folders.end(), s) == folders.end())
        folders.push_back(s);
    }
    j["demoFolders"] = folders;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << j.dump(2);
    if (!out)
    {
      *error = "Could not write " + file.string();
      return false;
    }
    return true;
  }
}
