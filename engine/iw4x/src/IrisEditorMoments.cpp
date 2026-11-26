// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The Moments panel: kills (from the game's obituaries), deaths, respawns,
// players joining and leaving (inferred, marked approximate when frames
// were skipped), and trickshot candidates with their evidence and a
// confidence. Moments fill in while the demo plays; "Find all moments"
// plays the whole demo fast once. One click jumps to a moment, makes a
// clip of it, or builds an editable trickshot cinematic (in / out, marker,
// slow motion, chase and eyes cuts on the shooter).
#include "EditorInternal.hpp"
#include "Runtime.hpp"

#include <iris/moments.hpp>

#include <algorithm>
#include <cstdio>

namespace IW4Iris::Editor
{
  namespace
  {
    struct MomentsUi
    {
      bool kills = true, deaths = true, respawns = false, joins = false;
      bool onlySelected = false;
    } panel;

    const char* IconOf(iris::MomentKind k)
    {
      switch (k)
      {
        case iris::MomentKind::Kill: return Icons::CROSSHAIRS;
        case iris::MomentKind::Death: return Icons::TIMES;
        case iris::MomentKind::Respawn: return Icons::SYNC;
        case iris::MomentKind::Joined:
        case iris::MomentKind::Left: return Icons::USER;
        case iris::MomentKind::Trickshot: return Icons::STAR;
      }
      return Icons::INFO;
    }

    std::string Describe(const iris::Moment& m)
    {
      switch (m.kind)
      {
        case iris::MomentKind::Kill: return m.detail;
        case iris::MomentKind::Death: return m.player + " died";
        case iris::MomentKind::Respawn: return m.player + " spawned";
        case iris::MomentKind::Joined: return m.player + " is in the game";
        case iris::MomentKind::Left: return m.player + " left";
        case iris::MomentKind::Trickshot: return m.detail;
      }
      return m.detail;
    }

    // Server time -> timeline time, through the project's speed curve.
    std::optional<double> ToTimeline(const Session& s, int serverTime)
    {
      if (!s.status.demoFirstServerTime)
        return std::nullopt;
      const double scene = (serverTime - *s.status.demoFirstServerTime) / 1000.0;
      const double end = std::max(s.project.timeline.end, scene) * 4.0 + 60.0;
      return s.project.timeRemap.CinematicTime(std::max(0.0, scene), end);
    }

    void AddMomentMarker(Session& s, double time, const std::string& name, const char* kind, const char* color)
    {
      iris::Marker m;
      m.id = iris::MakeUniqueId(s.project, "marker");
      m.time = time;
      m.name = name;
      m.kind = kind;
      m.color = color;
      s.project.markers.push_back(m);
      std::sort(s.project.markers.begin(), s.project.markers.end(),
                [](const iris::Marker& a, const iris::Marker& b) { return a.time < b.time; });
    }

    // In / out around the moment and a marker on it.
    bool MakeClip(Session& s, const iris::Moment& m, bool commit)
    {
      const auto a = ToTimeline(s, m.rangeStart), b = ToTimeline(s, m.rangeEnd), at = ToTimeline(s, m.serverTime);
      if (!a || !b || !at)
      {
        Notify("The demo's start time is not known yet: play the demo for a moment");
        return false;
      }
      auto& tl = s.project.timeline;
      if (*b > tl.end)
      {
        tl.end = *b;
        tl.Normalize();
      }
      tl.inPoint = std::clamp(*a, tl.start, tl.end);
      tl.outPoint = std::clamp(*b, tl.inPoint, tl.end);
      tl.Normalize();
      AddMomentMarker(s, *at, Describe(m), m.kind == iris::MomentKind::Trickshot ? "trickshot" : "kill",
                      m.kind == iris::MomentKind::Trickshot ? "#F2C14E" : "#E5484D");
      if (commit)
        s.Commit("Clip of a moment");
      return true;
    }

    // An editable trickshot cinematic: slow motion around the kill, the
    // clip, a chase camera on the shooter, their eyes for the shot.
    void MakeTrickshotCinematic(Session& s, const iris::Moment& m)
    {
      if (!s.status.demoFirstServerTime)
      {
        Notify("The demo's start time is not known yet: play the demo for a moment");
        return;
      }
      const double scene = (m.serverTime - *s.status.demoFirstServerTime) / 1000.0;
      const double kill = iris::AddSlowMotion(s.project.timeRemap, scene, 0.3, 2.0,
                                              std::max(s.project.timeline.end, scene) * 4.0 + 60.0);
      if (!MakeClip(s, m, false))
        return;
      const auto* shooter = s.players.Resolve(m.player, -1);
      const int slot = shooter ? shooter->clientNum : -1;
      const bool recording = shooter && shooter->recording;
      const std::string chase = MakePlayerCamera(s, m.player, slot, recording, iris::CameraMode::Chase);
      const std::string eyes = MakePlayerCamera(s, m.player, slot, recording, iris::CameraMode::PlayerEyes);
      auto& p = s.project;
      p.cuts.AddCut({p.timeline.inPoint, chase});
      p.cuts.AddCut({std::max(p.timeline.inPoint, kill - 0.4), eyes});
      p.cuts.AddCut({std::min(p.timeline.outPoint, kill + 1.0), chase});
      p.useCameraCuts = true;
      ShowShot(s);
      s.Commit("Trickshot cinematic");
      RequestSeek(s, p.timeline.inPoint);
      Notify("Trickshot cinematic: slow motion, chase and eyes cuts on " + m.player +
             ". Everything is editable on the timeline.");
    }
  }  // namespace

  void DrawMoments(Session& s)
  {
    if (!ImGui::Begin(PanelName(Panel::Moments), &Shown(Panel::Moments)))
    {
      ImGui::End();
      return;
    }
    const auto& log = s.moments;
    if (!s.status.demoPlaying && log.Moments().empty())
    {
      ImGui::TextWrapped("Load a demo (Demos panel): kills, deaths and trickshot candidates appear here as it plays.");
      ImGui::End();
      return;
    }

    // Coverage and the analysis pass.
    const auto& tl = s.project.timeline;
    if (s.analysis.active)
    {
      const float f = s.status.demoDuration > 0.0
                          ? static_cast<float>(std::clamp(s.status.sceneTime / s.status.demoDuration, 0.0, 1.0))
                          : 0.0f;
      ImGui::ProgressBar(f, ImVec2(-90, 0), "Finding moments ...");
      ImGui::SameLine();
      if (ImGui::Button("Stop", ImVec2(-1, 0)))
        s.Post([] { Runtime::StopMomentAnalysis(); });
    }
    else
    {
      if (ImGui::Button((std::string(Icons::SEARCH) + "  Find all moments in this demo").c_str(), ImVec2(-1, 0)))
        s.Post([] { Runtime::StartMomentAnalysis(); });
      ImGui::SetItemTooltip("Plays the whole demo at three times its speed once and comes back here. Moments also "
                            "fill in whenever the demo plays.");
    }
    if (const auto a = log.FirstTime(), b = log.LastTime(); a && b && s.status.demoFirstServerTime)
      ImGui::TextDisabled("Seen so far: %s to %s of the demo",
                          iris::FormatTime((*a - *s.status.demoFirstServerTime) / 1000.0, tl.frameRate, tl.display).c_str(),
                          iris::FormatTime((*b - *s.status.demoFirstServerTime) / 1000.0, tl.frameRate, tl.display).c_str());

    // Trickshot candidates.
    const auto shots = log.Trickshots();
    ImGui::SeparatorText((std::string(Icons::STAR) + "  Trickshot candidates").c_str());
    if (shots.empty())
      ImGui::TextDisabled("None yet: airborne kills with a turn or a long jump show up here.");
    int shown = 0;
    for (const auto& m : shots)
    {
      if (shown++ >= 25)
        break;
      ImGui::PushID(&m);
      char pct[16];
      std::snprintf(pct, sizeof(pct), "%.0f%%", m.confidence * 100.0);
      ImGui::ProgressBar(static_cast<float>(m.confidence), ImVec2(48, 0), pct);
      ImGui::SetItemTooltip("Confidence of this candidate (airborne at the kill, turn in the air, time in the air, "
                            "scope). A local analysis of the recorded movement, not a judgement.");
      ImGui::SameLine();
      ImGui::TextWrapped("%s", m.detail.c_str());
      if (ImGui::SmallButton("Go"))
        if (const auto at = ToTimeline(s, m.rangeStart))
          RequestSeek(s, *at);
      ImGui::SameLine();
      if (ImGui::SmallButton("Clip"))
        MakeClip(s, m, true);
      ImGui::SetItemTooltip("In / out around it and a marker on the kill");
      ImGui::SameLine();
      if (ImGui::SmallButton("Make cinematic"))
        MakeTrickshotCinematic(s, m);
      ImGui::SetItemTooltip("Slow motion around the kill, the clip, and cuts: chase camera on the shooter, their eyes "
                            "for the shot, chase again. All editable.");
      ImGui::PopID();
    }

    // All moments.
    ImGui::SeparatorText("Moments");
    ImGui::Checkbox("Kills", &panel.kills);
    ImGui::SameLine();
    ImGui::Checkbox("Deaths", &panel.deaths);
    ImGui::SameLine();
    ImGui::Checkbox("Spawns", &panel.respawns);
    ImGui::SameLine();
    ImGui::Checkbox("Join / leave", &panel.joins);
    const auto* sel = s.players.Find(s.selectedPlayer);
    ImGui::BeginDisabled(!sel);
    ImGui::Checkbox(sel ? ("Only " + sel->name).c_str() : "Only the selected player", &panel.onlySelected);
    ImGui::EndDisabled();
    if (ImGui::BeginChild("##moments", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
      for (const auto& m : log.Moments())
      {
        const bool want = (m.kind == iris::MomentKind::Kill && panel.kills) ||
                          (m.kind == iris::MomentKind::Death && panel.deaths) ||
                          (m.kind == iris::MomentKind::Respawn && panel.respawns) ||
                          ((m.kind == iris::MomentKind::Joined || m.kind == iris::MomentKind::Left) && panel.joins);
        if (!want)
          continue;
        if (panel.onlySelected && sel && m.player != sel->name && m.other != sel->name)
          continue;
        const auto at = ToTimeline(s, m.serverTime);
        ImGui::PushID(&m);
        std::string label = (at ? iris::FormatTime(*at, tl.frameRate, tl.display) : std::string("--")) + "  " +
                            IconOf(m.kind) + "  " + Describe(m) + (m.approximate ? "  (time approximate)" : "");
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && at)
        {
          RequestSeek(s, std::max(tl.start, *at - 2.0));
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m.kind == iris::MomentKind::Kill)
            MakeClip(s, m, true);
        }
        ImGui::SetItemTooltip("%s", m.confirmed ? "From the game's own event. Click: go there (2 s before). "
                                                  "Double-click a kill: clip it."
                                                : "Inferred from what the demo shows between frames. Click: go there.");
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    ImGui::End();
  }
}
