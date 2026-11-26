// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// What works right now: every capability with its real state (from the
// subsystems' own status, nothing assumed), a guided self-test that walks
// through the checks that need the user (load something, fly, play), and a
// support bundle: the report plus the recent log, with the home folder and
// account name removed and no game assets.
#include "Capture.hpp"
#include "Clock.hpp"
#include "DemoControl.hpp"
#include "Director.hpp"
#include "EditorInternal.hpp"
#include "Graphics.hpp"
#include "Log.hpp"
#include "OfflineGuard.hpp"
#include "Runtime.hpp"

#include <iris/diagnostics.hpp>
#include <iris/version.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

namespace IW4Iris::Editor
{
  namespace
  {
    using State = iris::CapabilityState;

    std::string NowIso()
    {
      const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::tm tm{};
      gmtime_s(&tm, &now);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
      return buf;
    }

    // An environment variable as UTF-8 (Win32 API: works with every C runtime).
    std::string Env(const wchar_t* name)
    {
      wchar_t buffer[1024];
      const DWORD n = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
      if (n == 0 || n >= std::size(buffer))
        return {};
      const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
      std::string out(static_cast<std::size_t>(std::max(0, bytes)), '\0');
      if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(n), out.data(), bytes, nullptr, nullptr);
      return out;
    }

    iris::Redaction RedactionForThisPc()
    {
      iris::Redaction r;
      for (const wchar_t* var : {L"USERPROFILE", L"HOMEPATH", L"APPDATA", L"LOCALAPPDATA"})
        if (auto v = Env(var); !v.empty())
          r.homes.push_back(std::move(v));
      if (auto user = Env(L"USERNAME"); !user.empty())
        r.userNames.push_back(std::move(user));
      return r;
    }

    iris::DiagnosticsReport BuildReport(Session& s)
    {
      iris::DiagnosticsReport r;
      r.product = std::string(iris::kVersionString);
      r.build = std::string("IW4x ") + IW4IRIS_IW4X_REVISION + ", Dear ImGui " + IMGUI_VERSION;
      r.createdAt = NowIso();
      auto add = [&](const char* id, const char* name, const char* area, State state, std::string detail, std::string fix = {}) {
        r.capabilities.push_back({id, name, area, state, std::move(detail), std::move(fix)});
      };
      const auto& st = s.status;

      // Safety
      const auto guard = OfflineGuard::GetStatus();
      if (guard.sessionViolation)
        add("offline", "Offline session", "Safety", State::Failed, "a network session was detected: tools are disabled",
            "Disconnect from the server. IW4Iris only runs in demos and local film sets.");
      else if (!guard.installed || !guard.engineSendBlocked)
        add("offline", "Offline session", "Safety", State::Failed, "the network guard is not active",
            "Repair the installation with the launcher.");
      else if (!guard.loopbackForwarding)
        add("offline", "Offline session", "Safety", State::Limited,
            "offline, but local packets are blocked too: film sets stay on the loading screen",
            "Report the IW4x revision with a support bundle; demos still work.");
      else
        add("offline", "Offline session", "Safety", State::Ready,
            std::to_string(guard.winsockImportsGuarded) + " socket and " + std::to_string(guard.httpImportsGuarded) +
                " HTTP imports guarded");

      // Engine
      add("render-clock", "Render clock (frame-exact recording)", "Engine",
          Clock::Installed() ? State::Ready : State::Failed,
          Clock::Installed() ? "installed" : "the clock hook is not installed in this game build",
          Clock::Installed() ? "" : "Repair the installation with the launcher; report the IW4x revision if it persists.");
      if (!st.cgameReady)
        add("view", "Game view (camera read / write)", "Engine", State::Checking, "nothing loaded yet",
            "Load a demo or a film set: the camera can only be checked in a running scene.");
      else if (!st.gameView)
        add("view", "Game view (camera read / write)", "Engine", State::Failed, "the view could not be read",
            "Report this with a support bundle.");
      else
        add("view", "Game view (camera read / write)", "Engine", State::Ready,
            "reading the view each frame, " + std::to_string(st.renderWidth) + " x " + std::to_string(st.renderHeight));
      const std::size_t missing = st.checkedDvars.size() - std::min(st.checkedDvars.size(), st.availableDvars.size());
      if (st.checkedDvars.empty())
        add("dvars", "Engine settings (dvars)", "Engine", State::Checking, "not checked yet");
      else if (missing == 0)
        add("dvars", "Engine settings (dvars)", "Engine", State::Ready, std::to_string(st.checkedDvars.size()) + " found");
      else
        add("dvars", "Engine settings (dvars)", "Engine", State::Limited,
            std::to_string(missing) + " of " + std::to_string(st.checkedDvars.size()) +
                " missing: the look settings that use them have no effect");

      // Demo
      if (!st.demoPlaying)
        add("demo", "Demo playback", "Demo", State::NotAvailable, "no demo loaded");
      else if (!s.demoIndex)
        add("demo", "Demo playback", "Demo", State::Limited, "playing; the demo index (length, seeking map) is still loading");
      else
        add("demo", "Demo playback", "Demo", State::Ready,
            st.demoName + ", " + iris::FormatTime(s.demoIndex->DurationSeconds(), s.project.timeline.frameRate,
                                                  iris::TimeDisplay::Seconds));

      // Demo players
      if (st.demoPlaying)
      {
        int connected = 0, visible = 0;
        bool recorder = false;
        for (const auto& p : s.players.Players())
        {
          if (p.availability == iris::PlayerAvailability::Left)
            continue;
          ++connected;
          visible += p.availability == iris::PlayerAvailability::Visible || p.availability == iris::PlayerAvailability::Dead;
          recorder = recorder || p.recording;
        }
        if (connected == 0)
          add("players", "Demo players", "Demo", State::Checking, "no snapshot read yet");
        else
          add("players", "Demo players", "Demo", recorder ? State::Ready : State::Limited,
              std::to_string(connected) + " connected, " + std::to_string(visible) +
                  " in the current snapshot (others: no position at this moment)" +
                  (recorder ? "" : "; the recording player was not identified"));
      }

      // Director
      const auto ds = Director::GetStatus();
      switch (ds.state)
      {
        case Director::SetState::None:
          add("director", "Director Mode (actors, props, effects)", "Director", State::NotAvailable, "no film set loaded",
              "Director panel: create a scene on a map.");
          break;
        case Director::SetState::Loading:
          add("director", "Director Mode (actors, props, effects)", "Director", State::Checking,
              "loading " + ds.map + ": waiting for the Director Mod");
          break;
        case Director::SetState::Failed:
          add("director", "Director Mode (actors, props, effects)", "Director", State::Failed, ds.lastError,
              "Repair the Director Mod with the launcher (Setup page), then reload the set.");
          break;
        case Director::SetState::Ready:
          if (!ds.failures.empty())
            add("director", "Director Mode (actors, props, effects)", "Director", State::Limited,
                std::to_string(ds.failures.size()) + " object(s) failed: " + ds.failures.front(),
                "Select the object in the Scene panel to see why; delete or fix it.");
          else
            add("director", "Director Mode (actors, props, effects)", "Director", State::Ready,
                ds.map + ", protocol " + std::to_string(ds.protocol) + ", " + std::to_string(ds.objects) + " object(s)");
          if (!ds.needReload.empty())
            add("director-assets", "Scene assets", "Director", State::Limited,
                std::to_string(ds.needReload.size()) + " asset(s) need a reload of the set", "Director panel: Reload set.");
          break;
      }

      // Graphics
      const auto gs = Graphics::GetStatus();
      if (gs.depth.empty() || gs.depth == "checking")
        add("depth", "Occlusion (graphics behind walls)", "Graphics", State::Checking,
            "checked when a graphic is drawn", "Add a graphic (Motion Graphics panel) in a running scene.");
      else
        add("depth", "Occlusion (graphics behind walls)", "Graphics", gs.depthAvailable ? State::Ready : State::NotAvailable,
            gs.depth, gs.depthAvailable ? "" : "Graphics can still be rendered \"Always on top\".");
      if (gs.matrices.empty())
        add("matrices", "Graphics placement (view matrices)", "Graphics", State::Checking, "checked when a graphic is drawn");
      else
        add("matrices", "Graphics placement (view matrices)", "Graphics", gs.engineMatrices ? State::Ready : State::Limited,
            gs.matrices, gs.engineMatrices ? "" : "Placement is computed from the camera; it can be slightly off.");
      if (!gs.degraded.empty())
        add("graphics-degraded", "Graphics set to hide behind walls", "Graphics", State::Limited,
            std::to_string(gs.degraded.size()) + " not drawn: occlusion unavailable",
            "Set them to \"Always on top\" (Object panel), or record where occlusion works.");

      // Capture
      const auto issues = Capture::Check(s.project.recording, &s.project);
      int errors = 0;
      std::string first;
      for (const auto& i : issues)
        if (i.severity == iris::Issue::Severity::Error)
        {
          if (errors++ == 0)
            first = i.message;
        }
      if (errors > 0)
        add("capture", "Recording with the current settings", "Capture", State::Failed, first,
            "Open the Recording panel: it lists every problem.");
      else if (!issues.empty())
        add("capture", "Recording with the current settings", "Capture", State::Limited, issues.front().message);
      else
        add("capture", "Recording with the current settings", "Capture", State::Ready, "no problems found");
      return r;
    }

    ImVec4 Color(State st)
    {
      switch (st)
      {
        case State::Ready: return {0.45f, 0.88f, 0.55f, 1.0f};
        case State::Checking: return {0.55f, 0.75f, 1.0f, 1.0f};
        case State::Limited: return {1.0f, 0.8f, 0.4f, 1.0f};
        case State::NotAvailable: return {0.6f, 0.6f, 0.65f, 1.0f};
        case State::Failed: return {1.0f, 0.45f, 0.45f, 1.0f};
      }
      return {1, 1, 1, 1};
    }

    const char* Icon(State st)
    {
      switch (st)
      {
        case State::Ready: return Icons::CHECK;
        case State::Checking: return Icons::SYNC;
        case State::Limited: return Icons::WARNING;
        case State::NotAvailable: return Icons::INFO;
        case State::Failed: return Icons::TIMES;
      }
      return Icons::INFO;
    }

    // ---------------------------------------------------------- self-test

    // Guided steps: each one is passed by the real thing happening (a
    // frame read, a free-camera move, playback advancing ...), never by
    // clicking "OK".
    struct SelfTest
    {
      bool running = false;
      int step = 0;
      double stepStart = 0.0;
      std::optional<iris::CameraState> viewAtStart;
      double playheadAtStart = 0.0;
      std::vector<std::pair<std::string, bool>> results;  // step, passed
    } selfTest;

    struct Step
    {
      const char* title;
      const char* instruction;
    };
    const Step kSteps[] = {
      {"Scene loaded", "Load a demo (Demos panel) or a film set (Director panel)."},
      {"Camera read", "Nothing to do: the game view is read each frame."},
      {"Free camera", "Hold the right mouse button over the game and move the mouse a little."},
      {"Playback", "Press Space and let it play for a moment."},
      {"Recording check", "Nothing to do: the recording settings and the scene are checked."},
    };
    constexpr int kStepCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));

    void Pass(bool passed)
    {
      selfTest.results.emplace_back(kSteps[selfTest.step].title, passed);
      ++selfTest.step;
      selfTest.stepStart = ImGui::GetTime();
      selfTest.viewAtStart.reset();
      if (selfTest.step >= kStepCount)
        selfTest.running = false;
    }

    void RunSelfTest(Session& s)
    {
      if (!selfTest.running)
        return;
      const double elapsed = ImGui::GetTime() - selfTest.stepStart;
      switch (selfTest.step)
      {
        case 0:
          if (s.status.cgameReady)
            Pass(true);
          break;
        case 1:
          if (s.status.gameView)
            Pass(true);
          else if (elapsed > 5.0)
            Pass(false);
          break;
        case 2:
        {
          const auto view = Runtime::LastAppliedCamera();
          if (!selfTest.viewAtStart)
            selfTest.viewAtStart = view;
          else if (view && (iris::Length(view->position - selfTest.viewAtStart->position) > 1.0 ||
                            std::abs(view->angles.yaw - selfTest.viewAtStart->angles.yaw) > 1.0))
            Pass(true);
          break;
        }
        case 3:
          if (elapsed < 0.1)
            selfTest.playheadAtStart = s.playhead;
          else if (s.playing && s.playhead > selfTest.playheadAtStart + 0.25)
            Pass(true);
          break;
        case 4:
        {
          bool ok = true;
          for (const auto& i : Capture::Check(s.project.recording, &s.project))
            ok = ok && i.severity != iris::Issue::Severity::Error;
          Pass(ok);
          break;
        }
        default: break;
      }
    }

    bool SaveBundle(Session& s, const iris::DiagnosticsReport& report, std::string* where)
    {
      const auto redaction = RedactionForThisPc();
      auto copy = report;
      for (auto& c : copy.capabilities)
      {
        c.detail = iris::Redact(c.detail, redaction);
        c.fix = iris::Redact(c.fix, redaction);
      }
      for (const auto& line : Log::Tail(400))
        copy.log.push_back(iris::Redact(line, redaction));
      auto j = iris::ReportJson(copy);
      // What the project contains, without file names or paths.
      const auto& p = s.project;
      j["project"] = {{"cameras", p.cameras.size()},
                      {"markers", p.markers.size()},
                      {"actors", p.scene.actors.size()},
                      {"props", p.scene.props.size()},
                      {"effects", p.scene.effects.size()},
                      {"graphics", p.scene.graphics.size()},
                      {"frameRate", p.timeline.frameRate.ToString()},
                      {"length", p.timeline.end - p.timeline.start}};
      j["selfTest"] = nlohmann::json::array();
      for (const auto& [name, passed] : selfTest.results)
        j["selfTest"].push_back({{"step", name}, {"passed", passed}});
      std::error_code ec;
      const auto dir = s.baseDirectory / "support";
      std::filesystem::create_directories(dir, ec);
      std::string stamp = copy.createdAt;
      for (auto& c : stamp)
        if (c == ':')
          c = '-';
      const auto file = dir / ("support-" + stamp + ".json");
      std::ofstream out(file, std::ios::binary);
      out << j.dump(2);
      if (!out)
        return false;
      *where = iris::Redact(file.string(), redaction);
      return true;
    }
  }  // namespace

  void DrawDiagnosticsOverview(Session& s)
  {
    RunSelfTest(s);
    // The checks read several subsystems (and the recording check touches
    // the output folder): twice a second is plenty.
    static iris::DiagnosticsReport report;
    static double builtAt = -10.0;
    if (ImGui::GetTime() - builtAt > 0.5)
    {
      report = BuildReport(s);
      builtAt = ImGui::GetTime();
    }
    const auto overall = report.Overall();
    ImGui::TextColored(Color(overall), "%s  %s", Icon(overall),
                       overall == State::Ready      ? "Everything checked works."
                       : overall == State::Checking ? "Some checks are waiting for a running scene."
                       : overall == State::Limited  ? "Works, with limitations (see below)."
                                                    : "Something is broken (see below).");
    ImGui::Spacing();
    if (ImGui::BeginTable("##caps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.2f);
      ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.0f);
      ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 2.0f);
      std::string area;
      for (const auto& c : report.capabilities)
      {
        if (c.area != area)
        {
          area = c.area;
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextDisabled("%s", area.c_str());
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(Color(c.state), "%s %s", Icon(c.state), std::string(iris::ToString(c.state)).c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", c.detail.empty() ? "-" : c.detail.c_str());
        if (!c.fix.empty() && c.state != State::Ready)
          ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.8f, 1.0f), "%s %s", Icons::ARROWS, c.fix.c_str());
      }
      ImGui::EndTable();
    }

    ImGui::SeparatorText("Guided self-test");
    if (!selfTest.running)
    {
      if (ImGui::Button(selfTest.results.empty() ? "Start the self-test" : "Run the self-test again"))
      {
        selfTest = {};
        selfTest.running = true;
        selfTest.stepStart = ImGui::GetTime();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("about a minute; it checks the real thing at each step");
    }
    else
    {
      const auto& step = kSteps[selfTest.step];
      ImGui::TextColored(CurrentPalette().accent, "Step %d of %d: %s", selfTest.step + 1, kStepCount, step.title);
      ImGui::TextWrapped("%s", step.instruction);
      if (ImGui::SmallButton("Skip this step"))
        Pass(false);
      ImGui::SameLine();
      if (ImGui::SmallButton("Stop"))
        selfTest.running = false;
    }
    for (const auto& [name, passed] : selfTest.results)
      ImGui::TextColored(passed ? Color(State::Ready) : Color(State::Failed), "%s %s: %s",
                         passed ? Icons::CHECK : Icons::TIMES, name.c_str(), passed ? "passed" : "failed or skipped");

    ImGui::SeparatorText("Support");
    if (ImGui::Button("Copy the report"))
      ImGui::SetClipboardText(iris::Redact(iris::ReportText(report), RedactionForThisPc()).c_str());
    ImGui::SetItemTooltip("Copies this table as text (without your home folder or account name)");
    ImGui::SameLine();
    if (ImGui::Button("Save a support bundle"))
    {
      std::string where;
      Notify(SaveBundle(s, report, &where) ? "Support bundle saved: " + where : "Could not write the support bundle");
    }
    ImGui::SetItemTooltip("A JSON file with this report, the self-test results, project counts and the recent log.\n"
                          "Your home folder and account name are removed; no game files or project files are included.");
  }
}
