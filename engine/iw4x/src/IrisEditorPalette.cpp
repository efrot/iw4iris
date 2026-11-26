// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The command palette (Ctrl+K): type what you want to do and press Enter.
// Every entry calls the same function as its button or shortcut; nothing
// here is a separate implementation. A typed time ("1:05", "90f") offers
// to move the playhead or the in / out point there.
#include "Capture.hpp"
#include "EditorInternal.hpp"
#include "Runtime.hpp"

#include <iris/search.hpp>
#include <iris/timeline_view.hpp>

#include <algorithm>
#include <functional>

namespace IW4Iris::Editor
{
  namespace
  {
    struct Command
    {
      std::string label;
      std::string shortcut;
      std::string keywords;  // extra words that find it
      std::function<void(Session&)> run;
      bool enabled = true;
      std::string disabledWhy;
      bool pinned = false;   // typed-time entries stay on top
    };

    struct PaletteState
    {
      bool openRequested = false;
      bool open = false;
      std::string query;
      int selected = 0;
      std::vector<std::string> recent;  // labels, most recent first
    } palette;

    std::string PanelLabel(Panel p)
    {
      std::string name = PanelName(p);
      if (const auto pos = name.find("###"); pos != std::string::npos)
        name.resize(pos);
      return name;
    }

    std::vector<Command> BuildCommands(Session& s)
    {
      auto& p = s.project;
      auto& tl = p.timeline;
      const bool capturing = Capture::IsActive();
      std::vector<Command> c;
      auto add = [&](std::string label, std::string shortcut, std::string keywords, std::function<void(Session&)> run) {
        Command cmd;
        cmd.label = std::move(label);
        cmd.shortcut = std::move(shortcut);
        cmd.keywords = std::move(keywords);
        cmd.run = std::move(run);
        c.push_back(std::move(cmd));
        return &c.back();
      };

      // Playback and time
      add(s.playing ? "Pause" : "Play", "Space", "playback preview start stop", [](Session& x) { TogglePlay(x); });
      add("Next frame", "Right", "step forward", [](Session& x) { StepFrames(x, 1); });
      add("Previous frame", "Left", "step back", [](Session& x) { StepFrames(x, -1); });
      add("Go to the in point", "Home", "start jump", [](Session& x) { RequestSeek(x, x.project.timeline.inPoint); });
      add("Go to the out point", "End", "jump", [](Session& x) { RequestSeek(x, x.project.timeline.outPoint); });
      add("Go to the next edit point", "Down", "key marker cut jump", [](Session& x) {
        if (const auto t = iris::NextPoint(x.playhead, EditPoints(x)))
          RequestSeek(x, *t);
      });
      add("Go to the previous edit point", "Up", "key marker cut jump", [](Session& x) {
        if (const auto t = iris::PreviousPoint(x.playhead, EditPoints(x)))
          RequestSeek(x, *t);
      });
      add("Set the in point at the playhead", "I", "range start render", [](Session& x) { SetInPoint(x, x.playhead); });
      add("Set the out point at the playhead", "O", "range end render", [](Session& x) { SetOutPoint(x, x.playhead); });
      add("In / out: the whole timeline", "", "range reset render all", [](Session& x) {
        x.project.timeline.inPoint = x.project.timeline.start;
        x.project.timeline.outPoint = x.project.timeline.end;
        x.Commit("In/out: whole timeline");
      });
      add("Show the whole timeline", "\\", "fit zoom view", [](Session& x) { FitTimeline(x); });
      add("Zoom the timeline in", "+", "", [](Session& x) { ZoomTimeline(x, 0.7); });
      add("Zoom the timeline out", "-", "", [](Session& x) { ZoomTimeline(x, 1.0 / 0.7); });
      add(tl.loop.enabled ? "Stop looping" : "Loop the in / out range", "", "repeat cycle", [](Session& x) {
        auto& t = x.project.timeline;
        t.loop.enabled = !t.loop.enabled;
        if (t.loop.enabled)
        {
          t.loop.start = t.inPoint;
          t.loop.end = t.outPoint;
        }
        x.Commit("Loop region");
      });
      add("Add a marker at the playhead", "M", "flag bookmark moment", [](Session& x) { AddMarker(x); });
      add("Graph editor", "G", "curves keys bezier", [](Session&) { Ui().timeline.graph = !Ui().timeline.graph; });

      // Cameras
      add("Add a camera key from the view", "K", "keyframe dolly path",
          [](Session& x) { x.Post([] { Runtime::AddKeyFromView(); }); });
      add("Create a camera from the view", "", "new static tripod shot", [](Session& x) { CameraFromView(x); });
      add("Move the selected camera to the view", "", "update place", [](Session& x) { MoveCameraToView(x); });
      add(s.freeView ? "Back to the shot camera" : "Free view (look around without changing the shot)", "V",
          "navigation camera fly", [](Session& x) {
            if (x.freeView)
              ShowShot(x);
            else
              x.freeView = true;
          })->enabled = !capturing;
      add(s.flying ? "Stop flying" : "Fly the free camera", "F", "navigate wasd move", [](Session& x) { x.flying = !x.flying; });
      add("Add a camera cut at the playhead", "", "edit switch scissors", [](Session& x) {
        x.project.cuts.AddCut({x.playhead, x.project.activeCameraId});
        x.project.useCameraCuts = true;
        x.Commit("Add camera cut");
      });
      for (const auto& cam : p.cameras)
      {
        const std::string id = cam.id;
        add("Use camera: " + cam.name, "", "active shot " + std::string(iris::ToString(cam.mode)), [id](Session& x) {
          x.project.activeCameraId = id;
          x.selectedCamera = id;
          ShowShot(x);
          x.Commit("Change active camera");
        });
      }

      // Demo players
      if (!s.players.Players().empty())
      {
        add("Next player", "]", "demo select", [](Session& x) { StepPlayer(x, 1); });
        add("Previous player", "[", "demo select", [](Session& x) { StepPlayer(x, -1); });
        const bool sel = s.players.Find(s.selectedPlayer) != nullptr;
        add("Watch the selected player: their eyes", "", "pov first person demo", [](Session& x) {
          WatchSelectedPlayer(x, iris::CameraMode::PlayerEyes, false);
        })->enabled = sel;
        add("Watch the selected player: behind them (chase)", "", "third person demo", [](Session& x) {
          WatchSelectedPlayer(x, iris::CameraMode::Chase, false);
        })->enabled = sel;
        add("Cut to the selected player's eyes at the playhead", "", "pov switch perspective", [](Session& x) {
          WatchSelectedPlayer(x, iris::CameraMode::PlayerEyes, true);
        })->enabled = sel;
        for (const auto& pl : s.players.Players())
        {
          if (pl.availability == iris::PlayerAvailability::Left)
            continue;
          const std::string id = pl.id;
          add("Select player: " + pl.name, "", "demo", [id](Session& x) { x.selectedPlayer = id; });
        }
      }

      // Markers
      for (const auto& m : p.markers)
      {
        const double t = m.time;
        add("Go to marker: " + (m.name.empty() ? iris::FormatTime(t, tl.frameRate, tl.display) : m.name), "", "jump",
            [t](Session& x) { RequestSeek(x, t); });
      }

      // Creating things
      add("Add 3D text", "", "title typography graphic words", [](Session& x) { QuickAddText(x); });
      add("Import a logo (SVG / PNG)", "", "graphic artwork brand", [](Session& x) { QuickImportLogo(x); });
      add("Add an actor", "", "character player soldier model", [](Session&) { Shown(Panel::Actors) = true; });
      add("Add a prop or an effect", "", "object model explosion smoke fx", [](Session&) { Shown(Panel::Assets) = true; });
      add("Film set: load a map for actors", "", "director map level scene", [](Session&) { Shown(Panel::Director) = true; });
      add("Shot presets", "", "orbit reveal push template", [](Session&) { Shown(Panel::Presets) = true; });

      // Project
      add("Save", "Ctrl+S", "project write", [](Session& x) { SaveProject(x); });
      add("Save as ...", "", "project copy", [](Session&) { Ui().saveAsPopup = true; });
      add("Open a project ...", "Ctrl+O", "load", [](Session&) { Ui().openPopup = true; });
      add("New project", "Ctrl+N", "empty start", [](Session& x) { x.Post([] { Runtime::NewProject(); }); });
      add("Start screen", "", "home welcome begin", [](Session& x) {
        x.settings.showHome = true;
        Ui().homeOpen = true;
      });
      add("Undo", "Ctrl+Z", "back revert", [](Session& x) { x.Undo(); })->enabled = s.history.CanUndo();
      add("Redo", "Ctrl+Y", "again", [](Session& x) { x.Redo(); })->enabled = s.history.CanRedo();

      // Output
      add("Record the in / out range", "", "render capture video export frames", [](Session&) {
        Shown(Panel::Recording) = true;
        ImGui::SetWindowFocus(PanelName(Panel::Recording));
      });
      add("Cancel the recording", "", "stop abort", [](Session& x) { x.Post([] { Capture::Cancel(); }); })->enabled = capturing;

      // Panels and workspaces
      for (int i = 0; i < static_cast<int>(Panel::Count); ++i)
      {
        const auto panel = static_cast<Panel>(i);
        if (panel == Panel::GettingStarted)
          continue;
        add((Shown(panel) ? "Hide panel: " : "Show panel: ") + PanelLabel(panel), panel == Panel::Shortcuts ? "?" : "",
            "window", [panel](Session&) { Shown(panel) = !Shown(panel); });
      }
      for (const auto& w : WorkspaceNames())
        add("Workspace: " + w, "", "layout panels", [w](Session&) { UseWorkspace(w); });
      add(s.settings.beginnerMode ? "Show all options (leave beginner mode)" : "Beginner mode (fewer options)", "",
          "simple advanced pro", [](Session& x) { x.settings.beginnerMode = !x.settings.beginnerMode; });

      for (auto& cmd : c)
        if (!cmd.enabled && cmd.disabledWhy.empty())
          cmd.disabledWhy = "not available right now";
      return c;
    }

    // A typed time turns into "go to / set in / set out" entries at the top.
    void AddTimeCommands(Session& s, std::vector<Command>& out)
    {
      const auto& tl = s.project.timeline;
      const auto t = iris::ParseTime(palette.query, tl.frameRate, tl.display);
      if (!t)
        return;
      const double at = std::clamp(*t, tl.start, tl.end);
      const std::string shown = iris::FormatTime(at, tl.frameRate, tl.display);
      std::vector<Command> timeCommands(3);
      timeCommands[0].label = "Go to " + shown;
      timeCommands[0].run = [at](Session& x) { RequestSeek(x, at); };
      timeCommands[1].label = "Set the in point at " + shown;
      timeCommands[1].run = [at](Session& x) { SetInPoint(x, at); };
      timeCommands[2].label = "Set the out point at " + shown;
      timeCommands[2].run = [at](Session& x) { SetOutPoint(x, at); };
      for (auto& t2 : timeCommands)
        t2.pinned = true;
      out.insert(out.begin(), timeCommands.begin(), timeCommands.end());
    }
  }  // namespace

  void OpenCommandPalette()
  {
    palette.openRequested = true;
  }

  void DrawCommandPalette(Session& s)
  {
    if (palette.openRequested)
    {
      palette.openRequested = false;
      palette.open = true;
      palette.query.clear();
      palette.selected = 0;
      ImGui::OpenPopup("##command-palette");
    }
    if (!palette.open)
      return;

    const auto* vp = ImGui::GetMainViewport();
    const float width = std::min(560.0f, vp->WorkSize.x - 40.0f);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.18f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##command-palette", ImGuiWindowFlags_NoMove))
    {
      palette.open = false;
      return;
    }

    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::InputTextWithHint("##query", "Type what you want to do, or a time like 1:05 ...", &palette.query);
    if (changed)
      palette.selected = 0;

    // Rank: fuzzy score on the label and keywords; recently used first when
    // the query is empty.
    auto commands = BuildCommands(s);
    AddTimeCommands(s, commands);
    struct Ranked
    {
      const Command* cmd;
      int score;
    };
    std::vector<Ranked> ranked;
    for (const auto& cmd : commands)
    {
      if (cmd.pinned)
      {
        ranked.push_back({&cmd, 0});
        continue;
      }
      std::optional<int> score = iris::FuzzyScore(palette.query, cmd.label);
      if (!score && !cmd.keywords.empty())
        if (const auto k = iris::FuzzyScore(palette.query, cmd.keywords))
          score = *k - 40;  // a keyword match ranks below a label match
      if (!score)
        continue;
      if (const auto it = std::find(palette.recent.begin(), palette.recent.end(), cmd.label); it != palette.recent.end())
        *score += palette.query.empty() ? 1000 - static_cast<int>(it - palette.recent.begin()) : 15;
      ranked.push_back({&cmd, *score});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
      if (a.cmd->pinned != b.cmd->pinned)
        return a.cmd->pinned;
      return !a.cmd->pinned && a.score > b.score;
    });
    if (ranked.size() > 60)
      ranked.resize(60);
    const int count = static_cast<int>(ranked.size());
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
      palette.selected = count ? (palette.selected + 1) % count : 0;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
      palette.selected = count ? (palette.selected + count - 1) % count : 0;
    palette.selected = std::clamp(palette.selected, 0, std::max(0, count - 1));

    const Command* run = nullptr;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
      if (count > 0 && ranked[static_cast<std::size_t>(palette.selected)].cmd->enabled)
        run = ranked[static_cast<std::size_t>(palette.selected)].cmd;

    ImGui::BeginChild("##results", ImVec2(0, std::min(360.0f, ImGui::GetTextLineHeightWithSpacing() * 14)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
    for (int i = 0; i < count; ++i)
    {
      const auto& cmd = *ranked[static_cast<std::size_t>(i)].cmd;
      ImGui::PushID(i);
      ImGui::BeginDisabled(!cmd.enabled);
      if (ImGui::Selectable(cmd.label.c_str(), i == palette.selected) && cmd.enabled)
        run = &cmd;
      ImGui::EndDisabled();
      if (!cmd.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", cmd.disabledWhy.c_str());
      if (i == palette.selected && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
        ImGui::SetScrollHereY();
      if (!cmd.shortcut.empty())
      {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() -
                        ImGui::CalcTextSize(cmd.shortcut.c_str()).x - 4.0f);
        ImGui::TextDisabled("%s", cmd.shortcut.c_str());
      }
      ImGui::PopID();
    }
    if (count == 0)
      ImGui::TextDisabled("Nothing matches. Try fewer letters, or a time like 12.5 or 1:05.");
    ImGui::EndChild();
    ImGui::TextDisabled("Enter: run   Up/Down: choose   Esc: close");

    if (run)
    {
      const std::string label = run->label;
      const auto action = run->run;
      std::erase(palette.recent, label);
      palette.recent.insert(palette.recent.begin(), label);
      if (palette.recent.size() > 8)
        palette.recent.resize(8);
      palette.open = false;
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      action(s);
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
      palette.open = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
