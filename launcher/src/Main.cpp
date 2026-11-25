// IW4Iris Launcher
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// A small native window (Win32 + Direct3D 9 + Dear ImGui) that finds the
// IW4x installation, sets up the isolated cinematic environment and starts
// the cinematic build offline. It never writes to the game folder.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d9.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <misc/cpp/imgui_stdlib.h>

#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "Environment.hpp"

#include <iris/diagnostics.hpp>
#include <iris/version.hpp>

#include <ctime>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fs = std::filesystem;
using namespace iris::launcher;

namespace
{
  // ------------------------------------------------------------ D3D

  IDirect3D9* d3d = nullptr;
  IDirect3DDevice9* device = nullptr;
  D3DPRESENT_PARAMETERS params{};
  UINT resizeWidth = 0, resizeHeight = 0;

  bool CreateDevice(HWND hwnd)
  {
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
      return false;
    params = {};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.BackBufferFormat = D3DFMT_UNKNOWN;
    params.EnableAutoDepthStencil = TRUE;
    params.AutoDepthStencilFormat = D3DFMT_D16;
    params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    return SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, &device)) ||
           SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &device));
  }

  void ResetDevice()
  {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if (device->Reset(&params) == D3DERR_INVALIDCALL)
      return;
    ImGui_ImplDX9_CreateDeviceObjects();
  }

  LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
  {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
      return TRUE;
    switch (msg)
    {
    case WM_SIZE:
      if (wParam != SIZE_MINIMIZED)
      {
        resizeWidth = LOWORD(lParam);
        resizeHeight = HIWORD(lParam);
      }
      return 0;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU)
        return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  // ------------------------------------------------------------ state

  struct State
  {
    std::vector<fs::path> detected;
    std::string gameFolder;
    std::string environmentFolder;
    bool copySettings = true;
    std::string ffmpeg;
    bool ffmpegConsent = false;
    int page = 0;
    std::mutex logMutex;
    std::deque<std::string> log;
    bool busy = false;
    std::string status;
    bool removeUserData = false;
    bool launchAfterSetup = false;  // "Install & Launch": start the game when the setup succeeds
  } state;

  fs::path ExeFolder()
  {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
  }

  fs::path SettingsFile()
  {
    return DefaultEnvironmentFolder().parent_path() / "launcher.json";
  }

  void Log(const std::string& line)
  {
    std::lock_guard _(state.logMutex);
    state.log.push_back(line);
    while (state.log.size() > 500)
      state.log.pop_front();
    std::error_code ec;
    fs::create_directories(SettingsFile().parent_path(), ec);
    std::ofstream(SettingsFile().parent_path() / "launcher.log", std::ios::app) << line << "\n";
  }

  void LoadLauncherSettings()
  {
    try
    {
      std::ifstream in(SettingsFile());
      if (!in)
        return;
      const auto j = nlohmann::json::parse(in);
      state.gameFolder = j.value("gameFolder", state.gameFolder);
      state.environmentFolder = j.value("environmentFolder", state.environmentFolder);
      state.ffmpeg = j.value("ffmpeg", state.ffmpeg);
    }
    catch (...)
    {
    }
  }

  void SaveLauncherSettings()
  {
    std::error_code ec;
    fs::create_directories(SettingsFile().parent_path(), ec);
    std::ofstream(SettingsFile()) << nlohmann::json{{"gameFolder", state.gameFolder},
                                                    {"environmentFolder", state.environmentFolder},
                                                    {"ffmpeg", state.ffmpeg}}
                                         .dump(2);
  }

  void Run(std::function<void()> work)
  {
    if (state.busy)
      return;
    state.busy = true;
    std::thread([work] {
      work();
      state.busy = false;
    }).detach();
  }

  void OpenFolder(const fs::path& folder)
  {
    std::error_code ec;
    fs::create_directories(folder, ec);
    ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }

  void ApplyTheme()
  {
    auto& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.ChildRounding = 6.0f;
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    auto* c = style.Colors;
    const ImVec4 accent(0.545f, 0.486f, 0.965f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.071f, 0.075f, 0.086f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.098f, 0.102f, 0.118f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.133f, 0.137f, 0.157f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.169f, 0.173f, 0.200f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.27f, 0.50f, 1.0f);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_Border] = ImVec4(0.2f, 0.21f, 0.24f, 1.0f);
  }

  bool EnvironmentReady()
  {
    return !state.environmentFolder.empty() && EnvironmentExists(Utf8Path(state.environmentFolder));
  }

  void Setup()
  {
    SetupOptions o;
    o.gameFolder = Utf8Path(state.gameFolder);
    o.environmentFolder = Utf8Path(state.environmentFolder);
    o.cinematicDll = ExeFolder() / "client" / "iw4x.dll";
    o.resourcesFolder = ExeFolder() / "resources";
    o.directorMod = ExeFolder() / "mods" / "iw4iris_director";
    o.copyGameSettings = state.copySettings;
    Run([o] {
      std::string error;
      Log("Setting up the cinematic environment...");
      if (SetupEnvironment(o, Log, &error))
      {
        std::vector<fs::path> demos;
        for (const auto& d : {o.gameFolder / "userraw" / "demos", o.gameFolder / "demos", o.gameFolder / "players" / "demos"})
          if (fs::is_directory(d))
            demos.push_back(d);
        std::optional<fs::path> ffmpeg;
        if (!state.ffmpeg.empty())
          ffmpeg = Utf8Path(state.ffmpeg);
        else
          ffmpeg = FindFfmpeg(o.environmentFolder);
        WriteEditorSettings(o.environmentFolder, ffmpeg, demos, &error);
        state.status = "Ready. Your normal IW4x installation was not changed.";
        if (state.launchAfterSetup)
        {
          std::string launchError;
          if (LaunchGame(o.environmentFolder, L"", &launchError))
          {
            Log("Started the cinematic build");
            state.status += " Starting IW4Iris ...";
          }
          else
          {
            state.status = "Set up, but the game did not start: " + launchError;
            Log(state.status);
          }
        }
      }
      else
      {
        state.status = "Setup failed: " + error;
        Log(state.status);
      }
      state.launchAfterSetup = false;
    });
  }

  // What the package and the environment look like (cached: the checks
  // read files).
  struct Health
  {
    bool gameValid = false;
    std::vector<std::string> gameMissing;
    bool envReady = false;
    ClientStatus client;
    ModStatus mod;
    std::optional<fs::path> ffmpeg;
    unsigned long long freeBytes = 0;
    double checkedAt = -10.0;
  };

  const Health& CurrentHealth()
  {
    static Health h;
    if (ImGui::GetTime() - h.checkedAt < 1.0 || state.busy)
      return h;
    h = {};
    h.checkedAt = ImGui::GetTime();
    if (!state.gameFolder.empty())
    {
      const auto v = ValidateGameFolder(ListFolder(Utf8Path(state.gameFolder)));
      h.gameValid = v.ok;
      h.gameMissing = v.missing;
    }
    h.envReady = EnvironmentReady();
    const auto env = Utf8Path(state.environmentFolder);
    h.client = CheckClient(ExeFolder() / "client", env);
    if (h.envReady)
      h.mod = CheckDirectorMod(ExeFolder() / "mods" / "iw4iris_director", env);
    h.ffmpeg = state.ffmpeg.empty() ? (h.envReady ? FindFfmpeg(env) : std::nullopt) : std::optional(Utf8Path(state.ffmpeg));
    ULARGE_INTEGER free{};
    std::error_code ec;
    auto probe = env;
    while (!probe.empty() && !fs::exists(probe, ec) && probe.has_parent_path() && probe.parent_path() != probe)
      probe = probe.parent_path();
    if (GetDiskFreeSpaceExW(probe.wstring().c_str(), &free, nullptr, nullptr))
      h.freeBytes = free.QuadPart;
    return h;
  }

  bool NeedsSetup(const Health& h)
  {
    return !h.envReady || !h.client.upToDate || (h.mod.shipped && !h.mod.upToDate);
  }

  void InstallAndLaunch()
  {
    SaveLauncherSettings();
    state.launchAfterSetup = true;
    Setup();
  }

  void Header(const char* text)
  {
    ImGui::TextColored(ImVec4(0.545f, 0.486f, 0.965f, 1.0f), "%s", text);
    ImGui::Separator();
  }

  void PageHome()
  {
    Header("Cinematic Studio for IW4x");
    ImGui::TextWrapped("IW4Iris runs a separate, offline copy of IW4x with the cinematic editor built in. Your normal "
                       "game installation is only read, never modified.");
    ImGui::Spacing();
    const auto& h = CurrentHealth();
    const ImVec4 good(0.5f, 0.9f, 0.6f, 1.0f), warn(1.0f, 0.75f, 0.4f, 1.0f), bad(1.0f, 0.45f, 0.45f, 1.0f);
    const ImVec2 big(300, 48);
    if (state.busy)
    {
      ImGui::TextColored(warn, "Working ... (see Logs)");
    }
    else if (!h.client.shipped)
    {
      ImGui::TextColored(bad, "client\\iw4x.dll (the IW4Iris build) is missing next to the launcher.");
      ImGui::TextWrapped("Download the complete package again: the launcher needs its client folder.");
    }
    else if (state.gameFolder.empty() || !h.gameValid)
    {
      ImGui::TextColored(warn, state.gameFolder.empty() ? "IW4x was not found automatically."
                                                        : "The chosen folder is not a complete IW4x installation.");
      for (const auto& m : h.gameMissing)
        ImGui::BulletText("missing: %s", m.c_str());
      if (ImGui::Button("Choose the game folder", ImVec2(240, 36)))
        state.page = 1;
    }
    else if (NeedsSetup(h))
    {
      ImGui::TextColored(warn, "%s", !h.envReady              ? "One step left: install the cinematic environment."
                                     : !h.client.upToDate ? "This package has a newer IW4Iris client than the environment."
                                                          : "The Director Mod in the environment is out of date.");
      if (ImGui::Button(h.envReady ? "Update & Launch" : "Install & Launch", big))
        InstallAndLaunch();
      ImGui::SetItemTooltip("Creates (or updates) the offline copy in\n%s\nthen starts it. The game folder is only read.",
                            state.environmentFolder.c_str());
    }
    else
    {
      ImGui::TextColored(good, "Ready.");
      if (ImGui::Button("Launch IW4Iris (offline)", big))
      {
        std::string error;
        if (LaunchGame(Utf8Path(state.environmentFolder), L"", &error))
        {
          Log("Started the cinematic build");
          state.status = "Starting IW4Iris ... In the game, F1 shows the editor.";
        }
        else
        {
          state.status = "Launch failed: " + error;
          Log(state.status);
        }
      }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("In the game: F1 shows the editor. Its start screen shows the ways in.");
    ImGui::Spacing();
    // Quick actions
    ImGui::BeginDisabled(state.busy || !h.envReady);
    const auto env = Utf8Path(state.environmentFolder);
    if (ImGui::Button("Open recordings"))
      OpenFolder(env / "iw4iris" / "recordings");
    ImGui::SameLine();
    if (ImGui::Button("Open projects"))
      state.page = 3;
    ImGui::SameLine();
    if (ImGui::Button("Repair"))
    {
      SaveLauncherSettings();
      Setup();
    }
    ImGui::SetItemTooltip("Re-creates the links and copies the launcher made, and updates the client and the Director Mod.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Diagnostics"))
      state.page = 5;
    ImGui::Spacing();
    if (!state.status.empty())
      ImGui::TextWrapped("%s", state.status.c_str());
  }

  // ------------------------------------------------------------ diagnostics

  std::string EnvVar(const wchar_t* name)
  {
    wchar_t buffer[1024];
    const DWORD n = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return n == 0 || n >= std::size(buffer) ? std::string() : Narrow(std::wstring(buffer, n));
  }

  iris::Redaction ThisPc()
  {
    iris::Redaction r;
    for (const wchar_t* v : {L"USERPROFILE", L"APPDATA", L"LOCALAPPDATA"})
      if (auto value = EnvVar(v); !value.empty())
        r.homes.push_back(value);
    if (auto user = EnvVar(L"USERNAME"); !user.empty())
      r.userNames.push_back(user);
    return r;
  }

  iris::DiagnosticsReport LauncherReport()
  {
    using S = iris::CapabilityState;
    const auto& h = CurrentHealth();
    iris::DiagnosticsReport r;
    r.product = std::string(iris::kVersionString) + " launcher";
    r.build = "Windows";
    {
      const auto now = std::time(nullptr);
      std::tm tm{};
      gmtime_s(&tm, &now);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
      r.createdAt = buf;
    }
    auto add = [&](const char* id, const char* name, const char* area, S st, std::string detail, std::string fix = {}) {
      r.capabilities.push_back({id, name, area, st, std::move(detail), std::move(fix)});
    };
    if (h.gameValid)
      add("game", "IW4x installation", "Installation", S::Ready, state.gameFolder);
    else
    {
      std::string missing;
      for (const auto& m : h.gameMissing)
        missing += (missing.empty() ? "" : ", ") + m;
      add("game", "IW4x installation", "Installation", S::Failed,
          state.gameFolder.empty() ? "not found" : "missing: " + missing, "Setup page: choose the folder with iw4x.exe.");
    }
    add("package", "IW4Iris client in this package", "Installation", h.client.shipped ? S::Ready : S::Failed,
        h.client.shipped ? "client\\iw4x.dll" : "client\\iw4x.dll is missing",
        h.client.shipped ? "" : "Download the complete package again.");
    if (!h.envReady)
      add("environment", "Cinematic environment", "Installation", S::NotAvailable, "not installed yet",
          "Home: Install & Launch.");
    else
      add("environment", "Cinematic environment", "Installation", S::Ready, state.environmentFolder);
    if (h.envReady)
    {
      if (h.client.upToDate)
        add("client", "Client in the environment", "Installation", S::Ready, "same as this package");
      else
      {
        std::string files;
        for (const auto& f : h.client.outdated)
          files += (files.empty() ? "" : ", ") + f;
        add("client", "Client in the environment", "Installation", S::Failed,
            h.client.installed ? "different from this package: " + files : "not installed", "Home: Update & Launch (or Repair).");
      }
      if (!h.mod.shipped)
        add("mod", "Director Mod", "Director", S::Failed,
            h.mod.problems.empty() ? "missing from the package" : h.mod.problems.front(), "Download the complete package again.");
      else if (h.mod.upToDate)
        add("mod", "Director Mod", "Director", S::Ready, "version " + h.mod.version);
      else
        add("mod", "Director Mod", "Director", S::Failed, h.mod.installed ? "out of date" : "not installed",
            "Home: Update & Launch (or Repair).");
    }
    if (h.ffmpeg)
      add("ffmpeg", "FFmpeg (MP4 / MOV / WebM export)", "Export", S::Ready, Narrow(h.ffmpeg->wstring()));
    else
      add("ffmpeg", "FFmpeg (MP4 / MOV / WebM export)", "Export", S::NotAvailable,
          "not found: image sequences and AVI work without it", "FFmpeg page.");
    const double gb = static_cast<double>(h.freeBytes) / (1024.0 * 1024.0 * 1024.0);
    char space[64];
    std::snprintf(space, sizeof(space), "%.1f GB free", gb);
    add("disk", "Disk space for recordings", "Export", gb < 1.0 ? S::Failed : gb < 10.0 ? S::Limited : S::Ready, space,
        gb < 10.0 ? "A minute of 1080p PNG frames takes several GB; free space or record to another drive." : "");
    std::lock_guard _(state.logMutex);
    const std::size_t from = state.log.size() > 60 ? state.log.size() - 60 : 0;
    for (std::size_t i = from; i < state.log.size(); ++i)
      r.log.push_back(state.log[i]);
    return r;
  }

  void PageDiagnostics()
  {
    Header("Diagnostics");
    const auto report = LauncherReport();
    const auto overall = report.Overall();
    auto color = [](iris::CapabilityState st) {
      switch (st)
      {
      case iris::CapabilityState::Ready: return ImVec4(0.5f, 0.9f, 0.6f, 1.0f);
      case iris::CapabilityState::Limited: return ImVec4(1.0f, 0.75f, 0.4f, 1.0f);
      case iris::CapabilityState::Failed: return ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
      default: return ImVec4(0.65f, 0.65f, 0.7f, 1.0f);
      }
    };
    ImGui::TextColored(color(overall), "%s", overall == iris::CapabilityState::Ready ? "Everything checked works."
                                             : overall == iris::CapabilityState::Failed ? "Something needs fixing (see below)."
                                                                                        : "Works, with limitations.");
    ImGui::TextDisabled("In the game, the editor's Diagnostics panel checks the engine side (camera, render clock, "
                        "occlusion, Director Mod) and runs a guided self-test.");
    ImGui::Spacing();
    if (ImGui::BeginTable("##checks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 100.0f);
      ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 2.0f);
      for (const auto& c : report.capabilities)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(color(c.state), "%s", std::string(iris::ToString(c.state)).c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", c.detail.c_str());
        if (!c.fix.empty() && c.state != iris::CapabilityState::Ready)
          ImGui::TextDisabled("-> %s", c.fix.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Copy the report"))
      ImGui::SetClipboardText(iris::Redact(iris::ReportText(report), ThisPc()).c_str());
    ImGui::SameLine();
    if (ImGui::Button("Save a support report"))
    {
      const auto redaction = ThisPc();
      auto copy = report;
      for (auto& c : copy.capabilities)
      {
        c.detail = iris::Redact(c.detail, redaction);
        c.fix = iris::Redact(c.fix, redaction);
      }
      for (auto& l : copy.log)
        l = iris::Redact(l, redaction);
      std::error_code ec;
      const auto dir = SettingsFile().parent_path() / "support";
      fs::create_directories(dir, ec);
      std::string stamp = copy.createdAt;
      for (auto& ch : stamp)
        if (ch == ':')
          ch = '-';
      const auto file = dir / Utf8Path("launcher-support-" + stamp + ".json");
      std::ofstream out(file, std::ios::binary);
      out << iris::ReportJson(copy).dump(2);
      if (out)
      {
        Log("Support report saved: " + iris::Redact(Narrow(file.wstring()), redaction));
        OpenFolder(dir);
      }
      else
        Log("Could not write the support report");
    }
    ImGui::SetItemTooltip("A JSON file with this table and the recent launcher log. Your home folder and account name "
                          "are removed; no game files are included.");
  }

  void PageSetup()
  {
    Header("Setup");
    if (ImGui::Button("Detect installations"))
      state.detected = DetectGameFolders();
    ImGui::SameLine();
    ImGui::TextDisabled("Steam libraries are searched for Modern Warfare 2 with IW4x.");
    for (const auto& d : state.detected)
      if (ImGui::Selectable(Narrow(d.wstring()).c_str(), state.gameFolder == Narrow(d.wstring())))
        state.gameFolder = Narrow(d.wstring());
    ImGui::InputText("Game folder", &state.gameFolder);
    ImGui::SetItemTooltip("The folder with iw4x.exe, main\\ and zone\\");
    if (!state.gameFolder.empty())
    {
      const auto v = ValidateGameFolder(ListFolder(Utf8Path(state.gameFolder)));
      if (v.ok)
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "Valid IW4x installation");
      for (const auto& m : v.missing)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Missing: %s", m.c_str());
      for (const auto& w : v.warnings)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "%s", w.c_str());
    }
    if (state.environmentFolder.empty())
      state.environmentFolder = Narrow(DefaultEnvironmentFolder().wstring());
    ImGui::InputText("Cinematic environment", &state.environmentFolder);
    ImGui::SetItemTooltip("Where the isolated cinematic copy lives (links to the game data plus IW4Iris' own files)");
    ImGui::Checkbox("Copy my game settings (players\\*.cfg) once", &state.copySettings);
    std::error_code ec;
    const bool clientPresent = fs::exists(ExeFolder() / "client" / "iw4x.dll", ec);
    if (!clientPresent)
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "client\\iw4x.dll (the IW4Iris build) is missing next to the launcher.");
    ImGui::Spacing();
    ImGui::BeginDisabled(state.busy || state.gameFolder.empty() || !clientPresent);
    if (ImGui::Button(EnvironmentReady() ? "Repair / update" : "Set up", ImVec2(180, 32)))
    {
      SaveLauncherSettings();
      Setup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.busy || !EnvironmentReady());
    if (ImGui::Button("Uninstall", ImVec2(120, 32)))
      ImGui::OpenPopup("uninstall");
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("uninstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::TextWrapped("Removes the cinematic environment's links and copies. The game folder is not touched.");
      ImGui::Checkbox("Also delete projects, recordings and exports", &state.removeUserData);
      if (ImGui::Button("Uninstall", ImVec2(120, 0)))
      {
        const auto env = Utf8Path(state.environmentFolder);
        const bool removeUser = state.removeUserData;
        Run([env, removeUser] {
          std::string error;
          if (!UninstallEnvironment(env, removeUser, Log, &error))
            Log("Uninstall failed: " + error);
        });
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    if (state.busy)
      ImGui::TextDisabled("Working...");

    // Director Mode needs the Director Mod in the environment.
    if (EnvironmentReady())
    {
      ImGui::Spacing();
      Header("Director Mod");
      static ModStatus mod;
      static double checkedAt = -10.0;
      if (ImGui::GetTime() - checkedAt > 1.0)
      {
        mod = CheckDirectorMod(ExeFolder() / "mods" / "iw4iris_director", Utf8Path(state.environmentFolder));
        checkedAt = ImGui::GetTime();
      }
      if (mod.installed && mod.upToDate)
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "Installed and up to date (version %s)", mod.version.c_str());
      else if (mod.installed)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "Installed, but older than this package. Click \"Repair / update\".");
      else if (mod.shipped)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "Not installed. Click \"Repair / update\".");
      for (const auto& p : mod.problems)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", p.c_str());
      ImGui::TextDisabled("Its scripts run only when the editor starts a film set; other maps and demos are unaffected.");
    }
  }

  void PageFfmpeg()
  {
    Header("FFmpeg");
    ImGui::TextWrapped("PNG and JPEG sequences need nothing else. H.264, H.265, ProRes and FFV1 recordings use FFmpeg, "
                       "which IW4Iris does not ship. Point to an ffmpeg.exe you have, or download one yourself.");
    const auto found = EnvironmentReady() ? FindFfmpeg(Utf8Path(state.environmentFolder)) : std::nullopt;
    ImGui::Text("Detected: %s", found ? Narrow(found->wstring()).c_str() : "none");
    ImGui::InputText("ffmpeg.exe", &state.ffmpeg);
    if (ImGui::Button("Save FFmpeg path") && EnvironmentReady())
    {
      std::string error;
      SaveLauncherSettings();
      if (WriteEditorSettings(Utf8Path(state.environmentFolder), state.ffmpeg.empty() ? found : std::optional<fs::path>(Utf8Path(state.ffmpeg)), {}, &error))
        Log("FFmpeg path saved for the editor");
      else
        Log(error);
    }
    ImGui::Spacing();
    ImGui::Checkbox("I want to download FFmpeg from its official builds page in my browser", &state.ffmpegConsent);
    ImGui::BeginDisabled(!state.ffmpegConsent);
    if (ImGui::Button("Open the FFmpeg download page"))
      ShellExecuteW(nullptr, L"open", L"https://ffmpeg.org/download.html#build-windows", nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Put ffmpeg.exe into <environment>\\iw4iris\\ffmpeg\\ and it is found automatically.");
  }

  void PageProjects()
  {
    Header("Projects and recordings");
    if (!EnvironmentReady())
    {
      ImGui::TextDisabled("Set up the environment first.");
      return;
    }
    const auto env = Utf8Path(state.environmentFolder);
    if (ImGui::Button("Open projects folder"))
      OpenFolder(env / "iw4iris" / "projects");
    ImGui::SameLine();
    if (ImGui::Button("Open recordings folder"))
      OpenFolder(env / "iw4iris" / "recordings");
    ImGui::SameLine();
    if (ImGui::Button("Open exports folder"))
      OpenFolder(env / "iw4iris" / "exports");
    ImGui::Spacing();
    std::error_code ec;
    for (fs::directory_iterator it(env / "iw4iris" / "projects", ec), end; it != end; it.increment(ec))
    {
      if (it->path().extension() != ".iw4iris")
        continue;
      ImGui::PushID(it->path().string().c_str());
      ImGui::TextUnformatted(Narrow(it->path().filename().wstring()).c_str());
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
      if (ImGui::SmallButton("Launch with it"))
      {
        std::string error;
        const std::wstring args = L"+iw4iris_open \"" + it->path().wstring() + L"\"";
        if (!LaunchGame(env, args, &error))
          Log("Launch failed: " + error);
      }
      ImGui::PopID();
    }
  }

  void PageLogs()
  {
    Header("Logs");
    if (EnvironmentReady())
    {
      if (ImGui::Button("Open the editor's log folder"))
        OpenFolder(Utf8Path(state.environmentFolder) / "iw4iris" / "logs");
      ImGui::SameLine();
    }
    if (ImGui::Button("Clear"))
    {
      std::lock_guard _(state.logMutex);
      state.log.clear();
    }
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    {
      std::lock_guard _(state.logMutex);
      for (const auto& l : state.log)
        ImGui::TextUnformatted(l.c_str());
    }
    ImGui::EndChild();
  }

  void PageAbout()
  {
    Header("About");
    ImGui::Text("%s", iris::kVersionString.data());
    ImGui::TextWrapped("Free software under the GNU GPL v3 (or later). Built on IW4x (GPL-3.0) and Dear ImGui (MIT); "
                       "fonts: Rubik and Font Awesome Free (SIL OFL 1.1). See THIRD-PARTY-NOTICES.md.");
    ImGui::TextWrapped("For offline demos and local scenes only. The cinematic build blocks all network traffic and "
                       "refuses to connect to servers.");
    ImGui::TextWrapped("Call of Duty and Modern Warfare are trademarks of Activision. IW4Iris ships no game files.");
  }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"IW4IrisLauncher", nullptr};
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"IW4Iris Launcher", WS_OVERLAPPEDWINDOW, 100, 100, 980, 640, nullptr, nullptr, instance, nullptr);
  if (!CreateDevice(hwnd))
  {
    MessageBoxW(nullptr, L"Direct3D 9 is not available.", L"IW4Iris Launcher", MB_ICONERROR);
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  static std::string ini = (SettingsFile().parent_path() / "launcher-imgui.ini").string();
  io.IniFilename = ini.c_str();
  std::error_code ec;
  const auto font = ExeFolder() / "resources" / "fonts" / "Rubik-Regular.ttf";
  if (fs::exists(font, ec))
    io.Fonts->AddFontFromFileTTF(font.string().c_str(), 17.0f);
  ApplyTheme();
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX9_Init(device);

  LoadLauncherSettings();
  state.detected = DetectGameFolders();
  if (state.gameFolder.empty() && !state.detected.empty())
    state.gameFolder = Narrow(state.detected.front().wstring());
  if (state.environmentFolder.empty())
    state.environmentFolder = Narrow(DefaultEnvironmentFolder().wstring());
  Log(std::string(iris::kVersionString) + " launcher started");

  bool done = false;
  while (!done)
  {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT)
        done = true;
    }
    if (done)
      break;
    if (resizeWidth != 0 && resizeHeight != 0)
    {
      params.BackBufferWidth = resizeWidth;
      params.BackBufferHeight = resizeHeight;
      resizeWidth = resizeHeight = 0;
      ResetDevice();
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const auto* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginChild("##nav", ImVec2(200, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4(0.545f, 0.486f, 0.965f, 1.0f), "IW4Iris");
    ImGui::TextDisabled("Launcher");
    ImGui::Spacing();
    const char* pages[] = {"Home", "Setup", "FFmpeg", "Projects", "Logs", "Diagnostics", "About"};
    for (int i = 0; i < 7; ++i)
      if (ImGui::Selectable(pages[i], state.page == i, 0, ImVec2(0, 28)))
        state.page = i;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_None);
    switch (state.page)
    {
    case 0: PageHome(); break;
    case 1: PageSetup(); break;
    case 2: PageFfmpeg(); break;
    case 3: PageProjects(); break;
    case 4: PageLogs(); break;
    case 5: PageDiagnostics(); break;
    default: PageAbout(); break;
    }
    ImGui::EndChild();
    ImGui::End();

    ImGui::EndFrame();
    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(18, 19, 22, 255), 1.0f, 0);
    if (SUCCEEDED(device->BeginScene()))
    {
      ImGui::Render();
      ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
      device->EndScene();
    }
    if (device->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST && device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
      ResetDevice();
  }

  ImGui_ImplDX9_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  if (device)
    device->Release();
  if (d3d)
    d3d->Release();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  return 0;
}
