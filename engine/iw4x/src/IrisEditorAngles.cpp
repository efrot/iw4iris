// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The Angles panel: other players' recordings of the main demo's match.
// Each client demo holds its recorder's exact first-person view and its
// own snapshots, so cutting to an angle gives an authentic perspective a
// single demo does not have.
//
// Synchronising: the offset between an angle's server clock and the main
// demo's. A suggestion comes from the kill feeds both recordings were seen
// to contain (the game's obituaries, sent to every client); it is shown
// with its evidence and applied only when the user says so, then checked by
// eye and confirmed, or set by hand. Nothing is synchronised silently, and
// a recording of another map or mode cannot be added.
#include "EditorInternal.hpp"
#include "Runtime.hpp"

#include <iris/sync.hpp>

#include <algorithm>
#include <cstdio>
#include <map>

namespace IW4Iris::Editor
{
  namespace
  {
    struct AnglesUi
    {
      std::map<std::string, iris::SyncSuggestion> suggestions;  // by angle id
      int addIndex = -1;
    } angles;

    const char* StateLabel(iris::SyncState st)
    {
      switch (st)
      {
        case iris::SyncState::Unsynced: return "not synchronised";
        case iris::SyncState::Suggested: return "suggested, not checked";
        case iris::SyncState::Manual: return "set by hand";
        case iris::SyncState::Confirmed: return "confirmed";
      }
      return "";
    }

    ImVec4 StateColor(iris::SyncState st)
    {
      switch (st)
      {
        case iris::SyncState::Unsynced: return ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
        case iris::SyncState::Suggested: return ImVec4(1.0f, 0.78f, 0.4f, 1.0f);
        case iris::SyncState::Manual:
        case iris::SyncState::Confirmed: return ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
      }
      return ImVec4(1, 1, 1, 1);
    }

    bool IsLoaded(const Session& s, const std::string& id)
    {
      return s.status.demoPlaying && s.loadedAngle && *s.loadedAngle == id;
    }

    // Where on the timeline a recording has pictures.
    void CoverageLine(const Session& s, const std::string& id)
    {
      const auto& p = s.project;
      if (const auto cover = iris::RecordingCoverage(p, id))
      {
        const double search = std::max(p.timeline.end, cover->end) * 4.0 + 60.0;
        const double a = p.timeRemap.CinematicTime(std::max(0.0, cover->start), search);
        const double b = p.timeRemap.CinematicTime(std::max(0.0, cover->end), search);
        const auto& tl = p.timeline;
        ImGui::TextDisabled("Covers %s to %s of the timeline", iris::FormatTime(a, tl.frameRate, tl.display).c_str(),
                            iris::FormatTime(b, tl.frameRate, tl.display).c_str());
      }
      else
        ImGui::TextDisabled("Coverage not known yet: open it once");
    }

    void FeedLine(const iris::DemoRef& ref)
    {
      if (ref.killFeed && !ref.killFeed->Empty())
      {
        int seenMs = 0;
        for (const auto& span : ref.killFeed->seen)
          seenMs += span.last - span.first;
        ImGui::TextDisabled("%zu kill(s) seen in %.0f s of it", ref.killFeed->kills.size(), seenMs / 1000.0);
      }
      else
        ImGui::TextDisabled("Kills not seen yet: open it and play it, or use \"Find all moments\"");
    }

    std::size_t CutsUsing(const Session& s, const std::string& id)
    {
      return static_cast<std::size_t>(std::count_if(s.project.cuts.Cuts().begin(), s.project.cuts.Cuts().end(),
                                                    [&](const iris::CameraCut& c) { return c.angle == id; }));
    }

    // A cut to the recording at the playhead, with its recorder's own view
    // (the game view camera) unless another camera is chosen afterwards.
    void CutTo(Session& s, const std::string& id)
    {
      auto& p = s.project;
      std::string camera;
      for (const auto& c : p.cameras)
        if (c.mode == iris::CameraMode::FirstPerson)
        {
          camera = c.id;
          break;
        }
      if (camera.empty())
      {
        iris::CameraDef c;
        c.id = iris::MakeUniqueId(p, "cam");
        c.name = "Game view";
        c.mode = iris::CameraMode::FirstPerson;
        c.color = "#9AA4B2";
        p.cameras.push_back(c);
        camera = c.id;
      }
      p.cuts.AddCut({s.playhead, camera, id});
      p.useCameraCuts = true;
      s.Commit("Cut to an angle");
    }

    void DrawAngle(Session& s, iris::DemoAngle& a, std::size_t index)
    {
      auto& p = s.project;
      ImGui::PushID(a.id.c_str());
      const bool loaded = IsLoaded(s, a.id);
      ImGui::SeparatorText((std::string(Icons::VIDEO) + "  " + a.name + (loaded ? "  (playing)" : "")).c_str());

      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputTextWithHint("##name", "Name", &a.name))
        s.Commit("Rename angle", true);
      ImGui::TextDisabled("%s%s", a.demo.name.c_str(),
                          a.recorder.empty() ? "" : ("  -  recorded by " + a.recorder).c_str());
      if (p.demo)
        if (const auto why = iris::AngleIncompatibility(*p.demo, a.demo))
          ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s Cannot be used: %s", Icons::WARNING, why->c_str());

      ImGui::TextColored(StateColor(a.sync), "%s Sync: %s", a.sync == iris::SyncState::Unsynced ? Icons::WARNING : Icons::CHECK,
                         StateLabel(a.sync));
      ImGui::SetItemTooltip("Suggested: applied from the kill feeds, not checked by eye yet (recordings warn).\n"
                            "Set by hand / confirmed: checked by you.\nNot synchronised: recordings refuse to use it.");
      if (a.sync == iris::SyncState::Suggested)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0f%% of shared kills agree)", a.confidence * 100.0);
      }

      // The offset, by hand.
      ImGui::SetNextItemWidth(120);
      int offset = a.offsetMs;
      if (ImGui::DragInt("Offset (ms)", &offset, 2.0f, -3600000, 3600000))
      {
        a.offsetMs = offset;
        a.sync = iris::SyncState::Manual;
        s.Commit("Angle offset", true);
      }
      ImGui::SetItemTooltip("This recording's server time minus the main demo's at the same moment. 0 is usual for "
                            "recordings of one server session.");
      const struct
      {
        const char* label;
        int ms;
      } nudges[] = {{"-1 s", -1000}, {"-50 ms", -50}, {"+50 ms", 50}, {"+1 s", 1000}};
      for (const auto& n : nudges)
      {
        ImGui::SameLine();
        if (ImGui::SmallButton(n.label))
        {
          a.offsetMs += n.ms;
          a.sync = iris::SyncState::Manual;
          s.Commit("Angle offset", true);
        }
        ImGui::SetItemTooltip("While this angle plays, the picture stays and the playhead moves: line a kill up with "
                              "its marker on the timeline (50 ms is about one snapshot).");
      }

      // The suggestion from the kill feeds.
      const bool feeds = p.demo && p.demo->killFeed && a.demo.killFeed;
      ImGui::BeginDisabled(!feeds);
      if (ImGui::Button("Suggest from kills"))
      {
        Runtime::StoreKillFeed(s);
        angles.suggestions[a.id] = iris::SuggestSync(*p.demo->killFeed, *a.demo.killFeed);
      }
      ImGui::EndDisabled();
      if (!feeds)
        ImGui::SetItemTooltip("Both recordings need kills seen first: open each and play it (or Find all moments in "
                              "the Moments panel).");
      else
        ImGui::SetItemTooltip("Compares the kills (killer and victim) both recordings show and proposes the offset "
                              "at which they agree.");
      ImGui::SameLine();
      ImGui::BeginDisabled(a.sync == iris::SyncState::Unsynced || a.sync == iris::SyncState::Confirmed);
      if (ImGui::Button("Confirm"))
      {
        a.sync = iris::SyncState::Confirmed;
        s.Commit("Confirm angle sync");
      }
      ImGui::EndDisabled();
      ImGui::SetItemTooltip("After checking by eye that a kill happens at the same moment in both recordings.");
      if (const auto it = angles.suggestions.find(a.id); it != angles.suggestions.end())
      {
        const auto& sg = it->second;
        ImGui::TextWrapped("%s %s", sg.found ? Icons::INFO : Icons::WARNING, sg.reason.c_str());
        if (sg.found)
        {
          char label[64];
          std::snprintf(label, sizeof(label), "Use %+d ms", sg.offsetMs);
          if (ImGui::Button(label))
          {
            a.offsetMs = sg.offsetMs;
            a.sync = iris::SyncState::Suggested;
            a.confidence = sg.confidence;
            angles.suggestions.erase(it);
            s.Commit("Use the suggested angle sync");
          }
          ImGui::SetItemTooltip("Applies it as a suggestion: check it at a kill, then press Confirm.");
        }
      }

      // Open, cut, remove.
      ImGui::BeginDisabled(loaded);
      if (ImGui::Button((std::string(Icons::PLAY) + "  Open here").c_str()))
        Runtime::OpenAngle(s, a.id);
      ImGui::EndDisabled();
      ImGui::SetItemTooltip("Plays this recording at the playhead's moment of the match (the demo is opened and "
                            "played up to there, which takes a moment).");
      ImGui::SameLine();
      if (ImGui::Button((std::string(Icons::SCISSORS) + "  Cut to it here").c_str()))
        CutTo(s, a.id);
      ImGui::SetItemTooltip("A camera cut at the playhead to this recording, with its recorder's own view (the Game "
                            "view camera; change the camera on the cut afterwards).");
      ImGui::SameLine();
      const auto uses = CutsUsing(s, a.id);
      const std::string remove = uses ? "Remove with its " + std::to_string(uses) + " cut(s)" : std::string("Remove");
      if (ImGui::Button(remove.c_str()))
      {
        auto cuts = p.cuts.Cuts();
        cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [&](const iris::CameraCut& c) { return c.angle == a.id; }),
                   cuts.end());
        p.cuts.SetCuts(cuts);
        angles.suggestions.erase(a.id);
        p.angles.erase(p.angles.begin() + static_cast<std::ptrdiff_t>(index));
        s.Commit("Remove angle");
        ImGui::PopID();
        return;
      }
      CoverageLine(s, a.id);
      FeedLine(a.demo);
      ImGui::PopID();
    }
  }  // namespace

  std::string CutAngle(const Session& s)
  {
    return s.loadedAngle.value_or(std::string());
  }

  std::string AddDemoAsAngle(Session& s, const iris::DemoEntry& d)
  {
    auto& p = s.project;
    if (!p.demo)
    {
      Notify("Open the main demo first: angles are other recordings of its match");
      return {};
    }
    iris::DemoAngle a;
    a.id = iris::MakeUniqueId(p, "angle");
    a.demo.path = d.path.string();
    a.demo.name = d.name;
    a.demo.fileSize = d.size;
    if (d.meta)
    {
      a.demo.mapName = d.meta->mapName;
      a.demo.gametype = d.meta->gametype;
      a.demo.author = d.meta->author;
    }
    a.name = !a.demo.author.empty() ? a.demo.author + "'s view" : d.name;
    if (const auto main = Runtime::SourceRef(s, ""); main)
    {
      if (const auto mainPath = iris::ResolveDemoRefPath(*main, s.projectFile); mainPath && *mainPath == d.path)
      {
        Notify("That is the main demo itself");
        return {};
      }
      if (const auto why = iris::AngleIncompatibility(*main, a.demo))
      {
        Notify("Not added: " + *why);
        return {};
      }
    }
    for (const auto& other : p.angles)
      if (const auto path = iris::ResolveDemoRefPath(other.demo, s.projectFile); path && *path == d.path)
      {
        Notify(d.name + " is already the angle '" + other.name + "'");
        return {};
      }
    p.angles.push_back(a);
    s.Commit("Add angle");
    Shown(Panel::Angles) = true;
    Notify("Added " + a.name + ". Synchronise it in the Angles panel before cutting to it.");
    return a.id;
  }

  void DrawAngles(Session& s)
  {
    if (!BeginPanel(Panel::Angles))
    {
      ImGui::End();
      return;
    }
    auto& p = s.project;
    // Keep the kills seen in the recording playing with it (merged), so
    // "Suggest from kills" can use them without a save or a switch.
    static double lastStore = 0.0;
    if (ImGui::GetTime() - lastStore > 1.0)
    {
      lastStore = ImGui::GetTime();
      Runtime::StoreKillFeed(s);
    }
    if (!p.demo)
    {
      ImGui::TextWrapped("Open a demo first: it becomes the main demo. Then add other players' recordings of the same "
                         "match here: each gives its recorder's exact first-person view to cut to.");
      ImGui::End();
      return;
    }
    ImGui::TextWrapped("Other players' recordings of this match. Each holds its recorder's exact first-person view; "
                       "cuts can switch between them. Synchronise each one before cutting to it.");
    if (s.status.demoPlaying && !s.loadedAngle)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.4f, 1.0f), "%s The demo playing is not part of this project.",
                         Icons::WARNING);
      if (s.loadedDemoPath)
      {
        ImGui::SameLine();
        if (ImGui::SmallButton("Add it as an angle"))
        {
          const auto it = std::find_if(s.demoList.begin(), s.demoList.end(),
                                       [&](const iris::DemoEntry& d) { return d.path == *s.loadedDemoPath; });
          iris::DemoEntry entry;
          if (it != s.demoList.end())
            entry = *it;
          else
          {
            entry.path = *s.loadedDemoPath;
            entry.name = s.loadedDemoPath->stem().string();
          }
          AddDemoAsAngle(s, entry);
        }
        ImGui::SetItemTooltip("If it is another player's recording of the main demo's match. (To edit it on its own, "
                              "start a new project.)");
      }
    }

    // The main demo.
    ImGui::SeparatorText((std::string(Icons::FILM) + "  Main demo" + (IsLoaded(s, "") ? "  (playing)" : "")).c_str());
    ImGui::TextUnformatted(p.demo->name.c_str());
    if (!p.demo->firstServerTime)
      ImGui::TextDisabled("Its start is not known yet: play it for a moment (angles follow its clock).");
    CoverageLine(s, "");
    FeedLine(*p.demo);
    ImGui::BeginDisabled(IsLoaded(s, ""));
    if (ImGui::Button((std::string(Icons::PLAY) + "  Open the main demo here").c_str()))
      Runtime::OpenAngle(s, "");
    ImGui::EndDisabled();
    if (!p.angles.empty())
    {
      ImGui::SameLine();
      if (ImGui::Button((std::string(Icons::SCISSORS) + "  Cut to it here").c_str()))
        CutTo(s, "");
    }

    for (std::size_t i = 0; i < p.angles.size(); ++i)
    {
      const auto before = p.angles.size();
      DrawAngle(s, p.angles[i], i);
      if (p.angles.size() != before)
        break;  // removed
    }

    // Adding one from the demo list.
    ImGui::SeparatorText((std::string(Icons::PLUS) + "  Add a recording of this match").c_str());
    std::vector<const iris::DemoEntry*> candidates;
    std::vector<std::filesystem::path> used;
    if (const auto path = iris::ResolveDemoRefPath(*p.demo, s.projectFile))
      used.push_back(*path);
    for (const auto& a : p.angles)
      if (const auto path = iris::ResolveDemoRefPath(a.demo, s.projectFile))
        used.push_back(*path);
    for (const auto& d : s.demoList)
      if (std::find(used.begin(), used.end(), d.path) == used.end() &&
          !(s.loadedDemoPath && *s.loadedDemoPath == d.path && IsLoaded(s, "")))
        candidates.push_back(&d);
    if (candidates.empty())
      ImGui::TextDisabled("No other demos in the demo folders (Demos panel).");
    else
    {
      if (angles.addIndex >= static_cast<int>(candidates.size()))
        angles.addIndex = -1;
      const char* preview = angles.addIndex >= 0 ? candidates[static_cast<std::size_t>(angles.addIndex)]->name.c_str()
                                                  : "Choose a demo";
      ImGui::SetNextItemWidth(-70);
      if (ImGui::BeginCombo("##add", preview))
      {
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
          const auto& d = *candidates[i];
          iris::DemoRef ref;
          if (d.meta)
          {
            ref.mapName = d.meta->mapName;
            ref.gametype = d.meta->gametype;
          }
          const auto why = iris::AngleIncompatibility(*p.demo, ref);
          ImGui::BeginDisabled(why.has_value());
          const std::string label = d.name + (d.meta ? "  (" + d.meta->mapName + ", " + d.meta->author + ")" : "");
          if (ImGui::Selectable(label.c_str(), angles.addIndex == static_cast<int>(i)))
            angles.addIndex = static_cast<int>(i);
          ImGui::EndDisabled();
          if (why)
            ImGui::SetItemTooltip("%s", why->c_str());
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(angles.addIndex < 0);
      if (ImGui::Button("Add", ImVec2(-1, 0)) && angles.addIndex >= 0)
      {
        AddDemoAsAngle(s, *candidates[static_cast<std::size_t>(angles.addIndex)]);
        angles.addIndex = -1;
      }
      ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Recording a range with several angles: \"Queue per angle\" in the render queue.");
    ImGui::End();
  }
}
