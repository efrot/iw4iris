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

#include <iris/version.hpp>

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
      }
      else
      {
        state.status = "Setup failed: " + error;
        Log(state.status);
      }
    });
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
    const bool ready = EnvironmentReady();
    if (ready)
      ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "Cinematic environment ready: %s", state.environmentFolder.c_str());
    else
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "Not set up yet. Open Setup first.");
    ImGui::Spacing();
    ImGui::BeginDisabled(!ready || state.busy);
    if (ImGui::Button("Launch IW4Iris (offline)", ImVec2(280, 44)))
    {
      std::string error;
      if (LaunchGame(Utf8Path(state.environmentFolder), L"", &error))
        Log("Started the cinematic build");
      else
        Log("Launch failed: " + error);
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("In game: F1 shows the editor. Load a demo from the Demos panel.");
    ImGui::Spacing();
    if (!state.status.empty())
      ImGui::TextWrapped("%s", state.status.c_str());
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
    const char* pages[] = {"Home", "Setup", "FFmpeg", "Projects", "Logs", "About"};
    for (int i = 0; i < 6; ++i)
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
