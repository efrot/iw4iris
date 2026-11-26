// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Updater.hpp"

#include "Environment.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "miniz.h"

namespace fs = std::filesystem;

namespace iris::launcher::update
{
  namespace
  {
    constexpr std::size_t kMaxApiBytes = 2u << 20;          // the release JSON
    constexpr std::size_t kMaxSumsBytes = 64u << 10;        // SHA256SUMS.txt
    constexpr std::size_t kMaxPackageBytes = 512u << 20;    // the zip
    constexpr std::uint64_t kMaxEntryBytes = 512ull << 20;  // one extracted file
    constexpr std::uint64_t kMaxTotalBytes = 1536ull << 20; // everything extracted
    constexpr unsigned kMaxEntries = 10000;
    constexpr int kMaxRedirects = 6;
    constexpr const wchar_t* kWorkFolder = L".iw4iris-update";

    bool Fail(std::string* error, std::string message)
    {
      if (error)
        *error = std::move(message);
      return false;
    }

    std::string WinError(const std::string& what)
    {
      return what + " failed (error " + std::to_string(GetLastError()) + ")";
    }

    std::string Lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    bool Exists(const fs::path& p)
    {
      return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // ---------------------------------------------------------------- HTTP

    struct Internet
    {
      HINTERNET handle = nullptr;
      explicit Internet(HINTERNET h) : handle(h) {}
      ~Internet()
      {
        if (handle)
          WinHttpCloseHandle(handle);
      }
      Internet(const Internet&) = delete;
      Internet& operator=(const Internet&) = delete;
      explicit operator bool() const { return handle != nullptr; }
    };

    std::string Header(HINTERNET request, DWORD info)
    {
      DWORD size = 0;
      WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size,
                          WINHTTP_NO_HEADER_INDEX);
      if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
        return {};
      std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
      if (!WinHttpQueryHeaders(request, info, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size, WINHTTP_NO_HEADER_INDEX))
        return {};
      buffer.resize(size / sizeof(wchar_t));
      return Narrow(buffer);
    }

    // One GET, redirects not followed (the caller checks every hop).
    bool GetOnce(HINTERNET session, const std::string& url, bool api, std::size_t maxBytes, std::uint64_t expectedBytes,
                 const ProgressFn& progress, std::string* body, std::string* redirect, std::string* error)
    {
      const std::wstring wide = Widen(url);
      URL_COMPONENTS parts{};
      parts.dwStructSize = sizeof(parts);
      parts.dwSchemeLength = static_cast<DWORD>(-1);
      parts.dwHostNameLength = static_cast<DWORD>(-1);
      parts.dwUrlPathLength = static_cast<DWORD>(-1);
      parts.dwExtraInfoLength = static_cast<DWORD>(-1);
      if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts))
        return Fail(error, WinError("Reading the address " + url));
      if (parts.nScheme != INTERNET_SCHEME_HTTPS)
        return Fail(error, "Updates are only downloaded over HTTPS.");
      const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
      std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
      if (parts.lpszExtraInfo)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

      Internet connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
      if (!connection)
        return Fail(error, WinError("Connecting to " + Narrow(host)));
      Internet request(WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
      if (!request)
        return Fail(error, WinError("Preparing the request"));
      DWORD disable = WINHTTP_DISABLE_REDIRECTS;
      WinHttpSetOption(request.handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
      const wchar_t* headers = api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
                                   : WINHTTP_NO_ADDITIONAL_HEADERS;
      if (!WinHttpSendRequest(request.handle, headers, api ? static_cast<DWORD>(-1L) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
          !WinHttpReceiveResponse(request.handle, nullptr))
        return Fail(error, WinError("Contacting GitHub") + "; check the internet connection.");

      DWORD status = 0;
      DWORD size = sizeof(status);
      WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &size, WINHTTP_NO_HEADER_INDEX);
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
      {
        *redirect = Header(request.handle, WINHTTP_QUERY_LOCATION);
        if (redirect->empty())
          return Fail(error, "GitHub sent a redirect without an address.");
        return true;
      }
      if (status == 403 || status == 429)
        return Fail(error, "GitHub refused the request for now (too many requests); try again in an hour.");
      if (status == 404)
        return Fail(error, "Not found on GitHub (no release published yet?).");
      if (status != 200)
        return Fail(error, "GitHub answered HTTP " + std::to_string(status) + ".");

      body->clear();
      for (;;)
      {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available))
          return Fail(error, WinError("Downloading"));
        if (available == 0)
          break;
        if (body->size() + available > maxBytes)
          return Fail(error, "The download is larger than an IW4Iris update can be; it was stopped.");
        const std::size_t at = body->size();
        body->resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, body->data() + at, available, &read))
          return Fail(error, WinError("Downloading"));
        body->resize(at + read);
        if (progress && expectedBytes > 0)
          progress(std::min(1.0, static_cast<double>(body->size()) / static_cast<double>(expectedBytes)));
      }
      return true;
    }

    bool HttpGet(const std::string& url, bool api, std::size_t maxBytes, std::uint64_t expectedBytes,
                 const ProgressFn& progress, std::string* body, std::string* error)
    {
      HINTERNET raw = WinHttpOpen(L"IW4Iris-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
      if (!raw)
        raw = WinHttpOpen(L"IW4Iris-Launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
      Internet session(raw);
      if (!session)
        return Fail(error, WinError("Starting WinHTTP"));
      // TLS 1.2 or newer only.
      DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
      DWORD modern = protocols | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
      if (!WinHttpSetOption(session.handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &modern, sizeof(modern)))
#endif
        WinHttpSetOption(session.handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
      WinHttpSetTimeouts(session.handle, 15000, 15000, 30000, 60000);

      std::string current = url;
      for (int hop = 0; hop < kMaxRedirects; ++hop)
      {
        if (!IsTrustedUpdateUrl(current))
          return Fail(error, "Refused to download from an address that is not this project's GitHub releases: " + current);
        std::string redirect;
        if (!GetOnce(session.handle, current, api, maxBytes, expectedBytes, progress, body, &redirect, error))
          return false;
        if (redirect.empty())
          return true;
        current = redirect;
      }
      return Fail(error, "Too many redirects.");
    }

    // ---------------------------------------------------------------- SHA-256

    std::string Sha256Hex(const std::string& data)
    {
      BCRYPT_ALG_HANDLE algorithm = nullptr;
      BCRYPT_HASH_HANDLE hash = nullptr;
      unsigned char digest[32]{};
      const bool ok =
          BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
          BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)) &&
          BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                        static_cast<ULONG>(data.size()), 0)) &&
          BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
      if (hash)
        BCryptDestroyHash(hash);
      if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
      if (!ok)
        return {};
      static const char* hex = "0123456789abcdef";
      std::string out;
      for (const unsigned char b : digest)
      {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 15]);
      }
      return out;
    }

    // ---------------------------------------------------------------- package

    bool Extract(const std::string& zipData, const fs::path& staging, std::vector<std::string>* files, std::string* error)
    {
      mz_zip_archive zip{};
      if (!mz_zip_reader_init_mem(&zip, zipData.data(), zipData.size(), 0))
        return Fail(error, "The update package is not a valid zip file.");
      struct Closer
      {
        mz_zip_archive* zip;
        ~Closer() { mz_zip_reader_end(zip); }
      } closer{&zip};
      const mz_uint count = mz_zip_reader_get_num_files(&zip);
      if (count == 0 || count > kMaxEntries)
        return Fail(error, "The update package has " + std::to_string(count) + " entries; that is not an IW4Iris package.");
      std::set<std::string> seen;
      std::uint64_t total = 0;
      for (mz_uint i = 0; i < count; ++i)
      {
        mz_zip_archive_file_stat entry{};
        if (!mz_zip_reader_file_stat(&zip, i, &entry))
          return Fail(error, "The update package is damaged.");
        const auto safe = SafeArchivePath(entry.m_filename);
        if (!safe)
          return Fail(error, std::string("The update package contains a path that must not be written: ") + entry.m_filename);
        if (entry.m_is_directory || safe->back() == '/')
          continue;
        if (entry.m_is_encrypted || !entry.m_is_supported)
          return Fail(error, "The update package contains an encrypted or unsupported entry: " + *safe);
        total += entry.m_uncomp_size;
        if (entry.m_uncomp_size > kMaxEntryBytes || total > kMaxTotalBytes)
          return Fail(error, "The update package is larger than an IW4Iris package can be.");
        if (!seen.insert(Lower(*safe)).second)
          return Fail(error, "The update package contains " + *safe + " twice.");
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);  // checks the entry's CRC-32
        if (!data)
          return Fail(error, "The update package is damaged (" + *safe + ").");
        const std::unique_ptr<void, decltype(&mz_free)> holder(data, &mz_free);
        const fs::path target = staging / Utf8Path(*safe);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        out.close();
        if (!out)
          return Fail(error, "Could not write " + *safe + " (disk full?).");
        files->push_back(*safe);
      }
      const auto missing = MissingPackageFiles(*files);
      if (!missing.empty())
        return Fail(error, "The download is not an IW4Iris package (no " + missing.front() + ").");
      return true;
    }

    // Every folder between the package and the target must be a real folder
    // (no junction or link leading outside the package), the target a file.
    bool SafeTarget(const fs::path& package, const fs::path& relative, std::string* error)
    {
      fs::path at = package;
      auto it = relative.begin();
      while (it != relative.end())
      {
        at /= *it;
        const bool last = std::next(it) == relative.end();
        const DWORD attributes = GetFileAttributesW(at.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
          if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            return Fail(error, Narrow(at.wstring()) + " is a link; IW4Iris does not update through links.");
          if (!last && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return Fail(error, Narrow(at.wstring()) + " is a file where the update needs a folder.");
          if (last && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return Fail(error, Narrow(at.wstring()) + " is a folder where the update has a file.");
        }
        ++it;
      }
      return true;
    }

    bool ReplaceFiles(const fs::path& package, const fs::path& staging, const fs::path& backup,
                      const std::vector<std::string>& files, std::string* error)
    {
      struct Done
      {
        fs::path target;
        fs::path saved;
        bool hadOld = false;
      };
      std::vector<Done> done;
      auto rollback = [&] {
        for (auto it = done.rbegin(); it != done.rend(); ++it)
        {
          if (it->hadOld)
            MoveFileExW(it->saved.c_str(), it->target.c_str(), MOVEFILE_REPLACE_EXISTING);
          else
            DeleteFileW(it->target.c_str());
        }
      };
      for (const auto& file : files)
      {
        const fs::path relative = Utf8Path(file);
        const fs::path target = package / relative;
        const fs::path source = staging / relative;
        const fs::path saved = backup / relative;
        if (!SafeTarget(package, relative, error))
        {
          rollback();
          return false;
        }
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        const bool hadOld = Exists(target);
        if (hadOld)
        {
          fs::create_directories(saved.parent_path(), ec);
          SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
          if (!MoveFileExW(target.c_str(), saved.c_str(), MOVEFILE_REPLACE_EXISTING))
          {
            const std::string message = WinError("Moving " + file + " aside") + "; is it open in another program?";
            rollback();
            return Fail(error, message);
          }
        }
        if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        {
          const std::string message = WinError("Installing " + file);
          if (hadOld)
            MoveFileExW(saved.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
          rollback();
          return Fail(error, message);
        }
        done.push_back({target, saved, hadOld});
      }
      return true;
    }

    // Only one launcher updates at a time (two open launchers must not both
    // install or undo an update).
    struct UpdateLock
    {
      HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\IW4Iris-Launcher-Update");
      bool owned = mutex && WaitForSingleObject(mutex, 0) == WAIT_OBJECT_0;
      ~UpdateLock()
      {
        if (owned)
          ReleaseMutex(mutex);
        if (mutex)
          CloseHandle(mutex);
      }
    };

    // Puts back every file of an update that did not finish (the launcher
    // or the PC stopped half-way): the backup holds the old files.
    void RestoreBackup(const fs::path& package, const fs::path& backup, const LogFn& log)
    {
      std::error_code ec;
      int restored = 0;
      for (fs::recursive_directory_iterator it(backup, fs::directory_options::none, ec), end; it != end; it.increment(ec))
      {
        if (it->is_symlink(ec) || !it->is_regular_file(ec))
          continue;
        const fs::path relative = fs::relative(it->path(), backup, ec);
        if (ec || relative.empty() || *relative.begin() == "..")
          continue;
        std::string ignored;
        if (!SafeTarget(package, relative, &ignored))
          continue;
        if (MoveFileExW(it->path().c_str(), (package / relative).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
          ++restored;
      }
      if (restored > 0 && log)
        log("An interrupted update was undone: " + std::to_string(restored) + " file(s) restored.");
    }
  }

  namespace
  {
    // Extracts a verified package to staging and replaces the package files.
    bool InstallPackage(const fs::path& packageFolder, const fs::path& work, std::string& zip, const std::string& version,
                        const LogFn& log, std::string* error)
    {
      const fs::path staging = work / L"staging";
      const fs::path backup = work / L"backup";
      std::vector<std::string> files;
      if (!Extract(zip, staging, &files, error))
      {
        SafeRemove(staging, nullptr);
        return false;
      }
      zip.clear();
      zip.shrink_to_fit();
      log("Installing " + std::to_string(files.size()) + " files ...");
      if (!ReplaceFiles(packageFolder, staging, backup, files, error))
      {
        SafeRemove(staging, nullptr);
        return false;
      }
      std::ofstream(work / L"finished.json") << nlohmann::json{{"version", version}, {"files", files.size()}}.dump();
      SafeRemove(staging, nullptr);
      log("IW4Iris " + version + " is installed.");
      return true;
    }
  }

  std::optional<ReleaseInfo> FetchLatest(std::string* error)
  {
    std::string body;
    const std::string url = "https://api.github.com/repos/" + std::string(kUpdateRepository) + "/releases/latest";
    if (!HttpGet(url, true, kMaxApiBytes, 0, {}, &body, error))
      return std::nullopt;
    return ParseRelease(body, error);
  }

  bool IsDevelopmentCheckout(const fs::path& packageFolder)
  {
    return Exists(packageFolder / ".git");
  }

  bool Install(const fs::path& packageFolder, const ReleaseInfo& release, const LogFn& log, const ProgressFn& progress,
               std::string* error)
  {
    if (IsDevelopmentCheckout(packageFolder))
      return Fail(error, "This folder is a git checkout; update it with git instead.");
    const UpdateLock lock;
    if (!lock.owned)
      return Fail(error, "Another IW4Iris launcher is updating; close it and try again.");
    const fs::path work = packageFolder / kWorkFolder;
    if (Exists(work) && IsReparsePoint(work))
      return Fail(error, Narrow(work.wstring()) + " is a link; remove it and try again.");
    const fs::path staging = work / L"staging";
    const fs::path backup = work / L"backup";
    std::string removeError;
    if (!SafeRemove(staging, &removeError) || !SafeRemove(backup, &removeError) ||
        !SafeRemove(work / L"finished.json", &removeError))
      return Fail(error, "Could not clear the update folder: " + removeError);

    const std::string version = ToString(release.version);
    log("Downloading IW4Iris " + version + " from GitHub ...");
    std::string sums;
    if (!HttpGet(release.checksums.url, false, kMaxSumsBytes, 0, {}, &sums, error))
      return false;
    const auto expected = ChecksumFor(sums, release.package.name);
    if (!expected)
      return Fail(error, "The release's " + std::string(kChecksumsAsset) + " has no line for " + release.package.name + ".");
    std::string zip;
    if (!HttpGet(release.package.url, false, kMaxPackageBytes, release.package.size, progress, &zip, error))
      return false;
    const std::string actual = Sha256Hex(zip);
    if (actual.empty() || actual != *expected)
      return Fail(error, "The download does not match its published checksum, so nothing was changed (expected " +
                             expected->substr(0, 16) + "..., got " + actual.substr(0, 16) + "...).");
    log("Checksum verified (SHA-256 " + actual.substr(0, 16) + "...).");

    return InstallPackage(packageFolder, work, zip, version, log, error);
  }

  bool InstallFromFile(const fs::path& packageFolder, const fs::path& zipFile, const LogFn& log, std::string* version,
                       std::string* error)
  {
    if (IsDevelopmentCheckout(packageFolder))
      return Fail(error, "This folder is a git checkout; update it with git instead.");
    // "IW4Iris-v0.6.0-Windows-x86.zip" -> 0.6.0
    const std::string name = Narrow(zipFile.filename().wstring());
    constexpr std::string_view prefix = "IW4Iris-v";
    const auto suffix = name.find("-Windows");
    std::optional<Version> parsed;
    if (name.rfind(prefix, 0) == 0 && suffix != std::string::npos && suffix > prefix.size())
      parsed = ParseVersion(name.substr(prefix.size(), suffix - prefix.size()));
    if (!parsed || name != PackageAssetName(*parsed))
      return Fail(error, "Choose an IW4Iris package (IW4Iris-vX.Y.Z-Windows-x86.zip).");
    std::error_code ec;
    const auto size = fs::file_size(zipFile, ec);
    if (ec || size > kMaxPackageBytes)
      return Fail(error, "The package cannot be read or is too large.");
    std::ifstream in(zipFile, std::ios::binary);
    std::string zip(static_cast<std::size_t>(size), '\0');
    if (!in.read(zip.data(), static_cast<std::streamsize>(size)))
      return Fail(error, "The package cannot be read.");
    const UpdateLock lock;
    if (!lock.owned)
      return Fail(error, "Another IW4Iris launcher is updating; close it and try again.");
    const fs::path work = packageFolder / kWorkFolder;
    if (Exists(work) && IsReparsePoint(work))
      return Fail(error, Narrow(work.wstring()) + " is a link; remove it and try again.");
    std::string removeError;
    if (!SafeRemove(work / L"staging", &removeError) || !SafeRemove(work / L"backup", &removeError) ||
        !SafeRemove(work / L"finished.json", &removeError))
      return Fail(error, "Could not clear the update folder: " + removeError);
    *version = ToString(*parsed);
    log("Installing " + Narrow(zipFile.filename().wstring()) + " (SHA-256 " + Sha256Hex(zip).substr(0, 16) + "...)");
    return InstallPackage(packageFolder, work, zip, *version, log, error);
  }

  std::optional<std::string> FinishPreviousUpdate(const fs::path& packageFolder, const LogFn& log)
  {
    const fs::path work = packageFolder / kWorkFolder;
    if (!Exists(work) || IsReparsePoint(work))
      return std::nullopt;
    const UpdateLock lock;
    if (!lock.owned)
      return std::nullopt;  // another launcher is updating right now
    const fs::path marker = work / L"finished.json";
    std::optional<std::string> installed;
    if (Exists(marker))
    {
      try
      {
        std::ifstream in(marker);
        installed = nlohmann::json::parse(in).value("version", std::string());
      }
      catch (...)
      {
        installed = std::string();
      }
    }
    else if (Exists(work / L"backup"))
    {
      RestoreBackup(packageFolder, work / L"backup", log);
    }
    // The backup goes first, the marker last: a backup without a marker
    // means "undo at the next start", which must never happen to an update
    // that finished. The old launcher may still be closing (its file is in
    // the backup), so removing it is retried.
    bool backupGone = false;
    for (int attempt = 0; attempt < 20 && !backupGone; ++attempt)
    {
      backupGone = SafeRemove(work / L"backup", nullptr);
      if (!backupGone)
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    SafeRemove(work / L"staging", nullptr);
    if (backupGone && SafeRemove(marker, nullptr))
      RemoveDirectoryW(work.c_str());  // only when empty
    return installed;
  }

  bool RestartLauncher(const fs::path& launcherExe, std::string* error)
  {
    std::wstring commandLine = L"\"" + launcherExe.wstring() + L"\" --updated";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launcherExe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        launcherExe.parent_path().c_str(), &startup, &process))
      return Fail(error, WinError("Starting the updated launcher"));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
  }
}
