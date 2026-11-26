// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The Players panel: every player the demo lists, followed by identity
// (iris::PlayerRoster), with what is known about them right now, and one
// click to watch them: their eyes (exact for the recording player,
// reconstructed for the others), a chase camera behind them, a follow or
// an orbit camera, or a cut to one of these at the playhead. Names over
// the players in the viewport select them.
#include "EditorInternal.hpp"
#include "Runtime.hpp"

#include <iris/players.hpp>
#include <iris/projection.hpp>

#include <algorithm>
#include <cmath>

namespace IW4Iris::Editor
{
  namespace
  {
    const char* TeamName(int team)
    {
      switch (team)
      {
        case 1: return "Axis";
        case 2: return "Allies";
        case 3: return "Spectators";
        default: return "Free-for-all / no team";
      }
    }

    ImU32 TeamColor(int team, float alpha = 1.0f)
    {
      const auto a = static_cast<int>(alpha * 255.0f);
      switch (team)
      {
        case 1: return IM_COL32(235, 110, 90, a);
        case 2: return IM_COL32(110, 170, 255, a);
        case 3: return IM_COL32(170, 170, 180, a);
        default: return IM_COL32(230, 210, 130, a);
      }
    }

    ImVec4 AvailabilityColor(iris::PlayerAvailability a)
    {
      switch (a)
      {
        case iris::PlayerAvailability::Visible: return {0.5f, 0.9f, 0.6f, 1.0f};
        case iris::PlayerAvailability::Dead: return {1.0f, 0.55f, 0.45f, 1.0f};
        case iris::PlayerAvailability::NotInView: return {0.95f, 0.8f, 0.45f, 1.0f};
        case iris::PlayerAvailability::Left: return {0.55f, 0.55f, 0.6f, 1.0f};
      }
      return {1, 1, 1, 1};
    }

    bool IsFavorite(const Session& s, const std::string& name)
    {
      return std::find(s.settings.favoritePlayers.begin(), s.settings.favoritePlayers.end(), name) !=
             s.settings.favoritePlayers.end();
    }

    std::string LowerCase(std::string text)
    {
      for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return text;
    }

    struct PlayersUi
    {
      char search[64]{};
      bool showDisconnected = false;
      bool cutMode = false;  // the watch buttons add a cut at the playhead instead of switching
    } panel;
  }  // namespace

  void StepPlayer(Session& s, int direction)
  {
    const std::string next = s.players.Step(s.selectedPlayer, direction);
    if (next.empty())
      return;
    s.selectedPlayer = next;
    if (const auto* p = s.players.Find(next))
      Notify("Player: " + p->name);
  }

  std::string WatchSelectedPlayer(Session& s, iris::CameraMode mode, bool cut)
  {
    const auto* p = s.players.Find(s.selectedPlayer);
    if (!p)
    {
      Notify("Select a player first (Players panel, or click a name in the view)");
      return {};
    }
    // The recording player's eyes are the demo's own view: the game's
    // first-person camera (with the weapon and the recorded field of view).
    if (mode == iris::CameraMode::PlayerEyes && p->recording)
      mode = iris::CameraMode::FirstPerson;

    auto& proj = s.project;
    iris::CameraDef* cam = nullptr;
    for (auto& c : proj.cameras)
    {
      if (c.mode != mode)
        continue;
      if (mode == iris::CameraMode::FirstPerson ||
          (c.target.kind == iris::TargetKind::Player && c.target.name == p->name))
      {
        cam = &c;
        break;
      }
    }
    if (!cam)
    {
      iris::CameraDef c;
      c.id = iris::MakeUniqueId(proj, "cam");
      c.mode = mode;
      c.target.kind = iris::TargetKind::Player;
      c.target.index = p->clientNum;
      c.target.name = p->name;
      c.target.offset = {0, 0, 0};
      switch (mode)
      {
        case iris::CameraMode::FirstPerson:
          c.name = "Recorded POV";
          c.target = {};
          break;
        case iris::CameraMode::PlayerEyes:
          c.name = "Eyes: " + p->name;
          c.horizontalFov = 80.0;
          break;
        case iris::CameraMode::Chase:
          c.name = "Chase: " + p->name;
          c.orbitDistance = 110.0;
          c.orbitHeight = 14.0;
          c.chaseShoulder = 18.0;
          c.horizontalFov = 80.0;
          break;
        case iris::CameraMode::Follow:
          c.name = "Follow: " + p->name;
          c.orbitDistance = 150.0;
          c.orbitHeight = 50.0;
          c.orbitStartAngle = 200.0;
          c.target.offset = {0, 0, 48};
          break;
        case iris::CameraMode::Orbit:
          c.name = "Orbit: " + p->name;
          c.orbitDistance = 170.0;
          c.orbitHeight = 40.0;
          c.orbitSpeed = 20.0;
          c.target.offset = {0, 0, 48};
          break;
        default:
          c.name = std::string(iris::ToString(mode)) + ": " + p->name;
          break;
      }
      proj.cameras.push_back(std::move(c));
      cam = &proj.cameras.back();
    }
    else if (cam->target.kind == iris::TargetKind::Player)
      cam->target.index = p->clientNum;  // the player's slot now, as the hint

    const std::string id = cam->id;
    if (cut)
    {
      proj.cuts.AddCut({s.playhead, id});
      proj.useCameraCuts = true;
    }
    else
      proj.activeCameraId = id;
    s.selectedCamera = id;
    ShowShot(s);
    s.Commit(cut ? "Cut to player" : "Watch player");
    if (mode == iris::CameraMode::PlayerEyes)
      Notify("Eyes of " + p->name + ": reconstructed from the demo (position, stance, view angles); their own "
                                     "model and weapon may be in view");
    else if (cut)
      Notify("Cut to " + cam->name + " at the playhead");
    return id;
  }

  void DrawPlayers(Session& s)
  {
    if (!ImGui::Begin(PanelName(Panel::Players), &Shown(Panel::Players)))
    {
      ImGui::End();
      return;
    }
    const auto& all = s.players.Players();
    if (all.empty())
    {
      ImGui::TextWrapped(s.status.demoPlaying ? "Waiting for the first snapshot ..."
                                              : "Load a demo (Demos panel): its players appear here.");
      ImGui::End();
      return;
    }

    // Search, filters, stepping
    ImGui::SetNextItemWidth(-140);
    ImGui::InputTextWithHint("##search", "Search players", panel.search, sizeof(panel.search));
    ImGui::SameLine();
    if (IconButton(Icons::STEP_BACKWARD, "Previous player ([)"))
      StepPlayer(s, -1);
    ImGui::SameLine();
    if (IconButton(Icons::STEP_FORWARD, "Next player (])"))
      StepPlayer(s, 1);
    ImGui::Checkbox("Players who are not connected now", &panel.showDisconnected);

    const std::string query = LowerCase(panel.search);
    auto shown = [&](const iris::TrackedPlayer& p) {
      if (!panel.showDisconnected && p.availability == iris::PlayerAvailability::Left && p.id != s.selectedPlayer)
        return false;
      return query.empty() || LowerCase(p.name).find(query) != std::string::npos;
    };

    // Favourites first, then by team.
    auto row = [&](const iris::TrackedPlayer& p) {
      ImGui::PushID(p.id.c_str());
      const bool fav = IsFavorite(s, p.name);
      if (ImGui::SmallButton(fav ? Icons::STAR : "  "))
      {
        auto& f = s.settings.favoritePlayers;
        if (fav)
          f.erase(std::remove(f.begin(), f.end(), p.name), f.end());
        else
          f.push_back(p.name);
        SaveSettings(s);
      }
      ImGui::SetItemTooltip(fav ? "Remove from favourites" : "Add to favourites");
      ImGui::SameLine();
      const bool selected = p.id == s.selectedPlayer;
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(TeamColor(p.team)));
      std::string label = p.name.empty() ? "(no name)" : p.name;
      if (p.recording)
        label += "  [recording player]";
      if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
      {
        s.selectedPlayer = p.id;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          WatchSelectedPlayer(s, iris::CameraMode::PlayerEyes, false);
      }
      ImGui::PopStyleColor();
      ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 92.0f);
      ImGui::TextColored(AvailabilityColor(p.availability), "%s", std::string(iris::ToString(p.availability)).c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", p.availability == iris::PlayerAvailability::NotInView
                                    ? "Connected, but not in this demo's snapshot right now: no position is recorded for "
                                      "this moment, so cameras on this player have no picture here (the recorded view is shown "
                                      "instead)."
                                : p.availability == iris::PlayerAvailability::Left
                                    ? "Not connected at this moment of the demo."
                                    : p.recording ? "The player who recorded the demo: their view is exact."
                                                  : "Position, stance and view angles come from the demo.");
      ImGui::PopID();
    };
    std::vector<const iris::TrackedPlayer*> favs;
    for (const auto& p : all)
      if (shown(p) && IsFavorite(s, p.name))
        favs.push_back(&p);
    if (!favs.empty())
    {
      ImGui::SeparatorText("Favourites");
      for (const auto* p : favs)
        row(*p);
    }
    for (const int team : {2, 1, 0, 3})
    {
      bool header = false;
      for (const auto& p : all)
      {
        if (p.team != team || !shown(p) || IsFavorite(s, p.name))
          continue;
        if (!header)
        {
          ImGui::SeparatorText(TeamName(team));
          header = true;
        }
        row(p);
      }
    }

    // The selected player: watch them.
    ImGui::Spacing();
    const auto* sel = s.players.Find(s.selectedPlayer);
    if (!sel)
    {
      ImGui::TextDisabled("Select a player (or click a name in the view).");
      ImGui::End();
      return;
    }
    ImGui::SeparatorText(("Watch " + sel->name).c_str());
    ImGui::Checkbox("Add as a cut at the playhead", &panel.cutMode);
    ImGui::SetItemTooltip("On: each button adds a camera cut to that view at the playhead, so the edit switches between "
                          "players over time (Timeline: Camera cuts). Off: the view switches now.");
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(sel->recording ? "Recorded view (exact)" : "Their eyes (reconstructed)", ImVec2(w, 0)))
      WatchSelectedPlayer(s, iris::CameraMode::PlayerEyes, panel.cutMode);
    ImGui::SetItemTooltip("%s", sel->recording
                                    ? "The demo's own first-person view, exactly as recorded."
                                    : "From this player's eyes: position, stance and view angles from the demo. Their "
                                      "weapon movement, recoil and view bob are not recorded for other players.");
    ImGui::SameLine();
    if (ImGui::Button("Behind them (chase)", ImVec2(w, 0)))
      WatchSelectedPlayer(s, iris::CameraMode::Chase, panel.cutMode);
    ImGui::SetItemTooltip("A third-person camera behind the player that turns with them (distance, height and shoulder "
                          "in the Inspector).");
    if (ImGui::Button("Follow", ImVec2(w, 0)))
      WatchSelectedPlayer(s, iris::CameraMode::Follow, panel.cutMode);
    ImGui::SetItemTooltip("Keeps a fixed offset to the player and looks at them.");
    ImGui::SameLine();
    if (ImGui::Button("Orbit", ImVec2(w, 0)))
      WatchSelectedPlayer(s, iris::CameraMode::Orbit, panel.cutMode);
    ImGui::SetItemTooltip("Circles the player.");
    if (sel->availability == iris::PlayerAvailability::NotInView || sel->availability == iris::PlayerAvailability::Left)
      ImGui::TextColored(AvailabilityColor(sel->availability), "%s",
                         "Not in the recording at this moment: cameras on this player have no picture until they are "
                         "(the recorded view is shown instead).");
    if (s.settings.beginnerMode)
    {
      ImGui::End();
      return;
    }
    if (sel->latest)
    {
      const auto& o = *sel->latest;
      ImGui::TextDisabled("slot %d, team %d, weapon %d, %s%s", o.clientNum, o.team, o.weapon,
                          o.stance == iris::Stance::Prone   ? "prone"
                          : o.stance == iris::Stance::Crouch ? "crouched"
                                                             : "standing",
                          o.dead ? ", dead" : "");
    }
    ImGui::End();
  }

  void DrawPlayerTags(Session& s)
  {
    auto& ui = Ui();
    ui.viewportClickConsumed = false;
    const auto& all = s.players.Players();
    if (all.empty() || !s.status.demoPlaying || (!Shown(Panel::Players) && s.selectedPlayer.empty()))
      return;
    const auto cam = Runtime::LastAppliedCamera();
    if (!cam)
      return;
    const auto& io = ImGui::GetIO();
    const ImVec2 size = io.DisplaySize;
    const iris::ViewParams view = iris::MakeViewParams(*cam, size.x / std::max(size.y, 1.0f));
    auto* dl = ImGui::GetBackgroundDrawList();
    const auto space = BeginGameSpace(dl);
    const ImVec2 mouse = GameMouse();
    const bool canClick = MouseOverGame() && !io.WantCaptureMouse && !Runtime::IsFlying();
    for (const auto& p : all)
    {
      if (!p.latest || !p.latest->visible)
        continue;
      // Nothing to label from inside the recording player's own eyes.
      if (p.recording && Runtime::LastAppliedCamera() && iris::Distance(cam->position, p.latest->origin) < 80.0)
        continue;
      const iris::Vec3 head = p.latest->origin + iris::Vec3{0, 0, iris::EyeHeight(p.latest->stance) + 14.0};
      const auto sp = iris::ProjectToScreen(view, head, size.x, size.y);
      if (!sp || sp->x < -50 || sp->y < -50 || sp->x > size.x + 50 || sp->y > size.y + 50)
        continue;
      const bool selected = p.id == s.selectedPlayer;
      const std::string text = p.name + (p.availability == iris::PlayerAvailability::Dead ? "  (dead)" : "");
      const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
      const ImVec2 a(static_cast<float>(sp->x) - ts.x * 0.5f - 5.0f, static_cast<float>(sp->y) - ts.y - 6.0f);
      const ImVec2 b(a.x + ts.x + 10.0f, a.y + ts.y + 4.0f);
      const bool hover = canClick && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y;
      dl->AddRectFilled(a, b, selected ? TeamColor(p.team, 0.85f) : IM_COL32(0, 0, 0, hover ? 190 : 130), 3.0f);
      dl->AddRect(a, b, TeamColor(p.team, selected ? 1.0f : 0.7f), 3.0f, selected ? 2.0f : 1.0f);
      dl->AddText(ImVec2(a.x + 5.0f, a.y + 2.0f), selected ? IM_COL32(10, 10, 14, 255) : TeamColor(p.team), text.c_str());
      dl->AddTriangleFilled(ImVec2(static_cast<float>(sp->x) - 4, b.y), ImVec2(static_cast<float>(sp->x) + 4, b.y),
                            ImVec2(static_cast<float>(sp->x), b.y + 5), TeamColor(p.team, 0.8f));
      if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        s.selectedPlayer = p.id;
        ui.viewportClickConsumed = true;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          WatchSelectedPlayer(s, iris::CameraMode::PlayerEyes, false);
      }
    }
    EndGameSpace(dl, space);
  }
}
