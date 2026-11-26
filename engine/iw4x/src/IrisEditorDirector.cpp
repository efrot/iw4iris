// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Director Mode editor: the home panel and scene wizard, the character /
// animation / props & effects libraries, motion graphics, the object
// inspector, scene objects in the Scene panel and the viewport overlay
// (object outlines, click selection, transform gizmo, composition guides).
// Render thread; Draw() holds the session lock; game work is posted.
#include "EditorInternal.hpp"
#include "Engine.hpp"
#include "Graphics.hpp"
#include "Runtime.hpp"

#include <iris/projection.hpp>
#include <iris/shots.hpp>
#include <iris/templates.hpp>

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

namespace IW4Iris::Editor
{
  namespace
  {
    // ------------------------------------------------------------ helpers

    const Director::Catalog& Catalog()
    {
      auto& ui = Ui();
      const int v = Director::CatalogVersion();
      if (v != ui.catalogVersion)
      {
        ui.catalog = Director::GetCatalog();
        ui.catalogVersion = v;
      }
      return ui.catalog;
    }

    bool Pro(const Session& s) { return !s.settings.beginnerMode; }

    void Recent(Session& s, const std::string& name)
    {
      s.settings.recentAssets = iris::PushRecent(s.settings.recentAssets, name, 24);
    }

    bool Favourite(Session& s, const std::string& name)
    {
      const bool fav = s.settings.favoriteAssets.contains(name);
      ImGui::PushID(name.c_str());
      ImGui::PushStyleColor(ImGuiCol_Text, fav ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
      const bool clicked = ImGui::SmallButton(Icons::STAR);
      ImGui::PopStyleColor();
      ImGui::PopID();
      if (clicked)
      {
        if (fav)
          s.settings.favoriteAssets.erase(name);
        else
          s.settings.favoriteAssets.insert(name);
        SaveSettings(s);
      }
      return fav;
    }

    const iris::CharacterEntry* CharacterFor(const std::string& body)
    {
      for (const auto& c : Catalog().characters)
        if (c.body == body)
          return &c;
      return nullptr;
    }

    std::string AnimLabel(const std::string& anim)
    {
      return anim.empty() ? std::string("(none)") : iris::ClassifyAnimation(anim).label;
    }

    double CameraYaw()
    {
      const auto cam = Runtime::LastAppliedCamera();
      return cam ? cam->angles.yaw : 0.0;
    }

    template <typename T>
    std::string NewId(Session& s, const char* prefix)
    {
      return iris::MakeUniqueId(s.project, prefix);
    }

    // A transform edit: rest transform without keys, a key at the playhead
    // once the object is animated (auto-key).
    void SetTransform(Session& s, iris::Transform& rest, iris::TransformTrack& track, const iris::Transform& t)
    {
      if (track.Empty())
        rest = t;
      else
        track.SetKey(s.playhead, t, iris::Interpolation::Smooth, t.scale != iris::Vec3{1, 1, 1});
    }

    // ------------------------------------------------------------ file dialog

    std::mutex fileMutex;
    std::string pickedFile, pickedFor;
    bool pickerOpen = false;

    void PickFile(const char* purpose, const wchar_t* filter)
    {
      {
        std::lock_guard _(fileMutex);
        if (pickerOpen)
          return;
        pickerOpen = true;
      }
      std::thread([p = std::string(purpose), filter] {
        std::string result;
        if (HMODULE dll = LoadLibraryW(L"comdlg32.dll"))
        {
          using Fn = BOOL(WINAPI*)(LPOPENFILENAMEW);
          if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dll, "GetOpenFileNameW"))))
          {
            wchar_t buffer[1024]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof ofn;
            ofn.lpstrFilter = filter;
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = 1024;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (fn(&ofn))
            {
              const auto u8 = std::filesystem::path(buffer).u8string();
              result.assign(u8.begin(), u8.end());
            }
          }
          FreeLibrary(dll);
        }
        std::lock_guard _(fileMutex);
        pickerOpen = false;
        if (!result.empty())
        {
          pickedFile = result;
          pickedFor = p;
        }
      }).detach();
    }

    // The file chosen for `purpose`, once.
    std::optional<std::string> TakePicked(const char* purpose)
    {
      std::lock_guard _(fileMutex);
      if (pickedFile.empty() || pickedFor != purpose)
        return std::nullopt;
      std::string f = std::move(pickedFile);
      pickedFile.clear();
      pickedFor.clear();
      return f;
    }

    constexpr const wchar_t* kLogoFilter = L"Logos (SVG, PNG, JPEG, TGA, BMP)\0*.svg;*.png;*.jpg;*.jpeg;*.tga;*.bmp\0\0";
    constexpr const wchar_t* kImageFilter = L"Images (PNG, JPEG, TGA, BMP)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0\0";
    constexpr const wchar_t* kModelFilter = L"Wavefront OBJ\0*.obj\0\0";
    constexpr const wchar_t* kFontFilter = L"Fonts (TTF, OTF)\0*.ttf;*.otf\0\0";

    // ------------------------------------------------------------ creation

    iris::GraphicDef NewGraphic(Session& s, iris::GraphicKind kind, const std::string& name)
    {
      iris::GraphicDef g;
      g.id = iris::MakeUniqueId(s.project, "gfx");
      g.name = name;
      g.kind = kind;
      g.transform.position = PlacementPoint(s) + iris::Vec3{0, 0, 40};
      g.transform.angles = {0, CameraYaw() + 180.0, 0};  // the front faces the camera
      return g;
    }

    void AddGraphic(Session& s, iris::GraphicDef g)
    {
      const std::string id = g.id;
      s.project.scene.graphics.push_back(std::move(g));
      SelectObject(s, id);
      s.Commit("Add graphic");
    }

    void AddActorFrom(Session& s, const iris::CharacterEntry& c)
    {
      iris::TemplateAssets assets;
      assets.animations = Catalog().animationNames;
      const iris::Vec3 at = PlacementPoint(s);
      const std::string id = iris::AddActor(s.project, c, at, CameraYaw() + 180.0, assets);
      Recent(s, c.body);
      SelectObject(s, id);
      s.Commit("Add actor");
      Notify("Added " + c.label);
    }
  }  // namespace

  // ---------------------------------------------------------------- shared

  void QuickAddText(Session& s)
  {
    auto g = NewGraphic(s, iris::GraphicKind::Text, "Title");
    g.text = "IW4IRIS";
    g.materials.front = iris::MakeMaterial("matte_white");
    AddGraphic(s, std::move(g));
    Shown(Panel::ObjectInspector) = true;
    Notify("3D text added in front of the camera: edit it in the Object panel");
  }

  void QuickImportLogo(Session& s)
  {
    (void)s;
    Shown(Panel::Graphics) = true;  // the panel adds the logo once a file is picked
    PickFile("new-logo", kLogoFilter);
  }

  bool ObjectExists(const Session& s, const std::string& id)
  {
    return !id.empty() && s.project.scene.Contains(id);
  }

  void SelectObject(Session& s, const std::string& id)
  {
    s.selectedObject = id;
    if (!id.empty())
      Shown(Panel::ObjectInspector) = true;
  }

  void DeleteObject(Session& s, const std::string& id)
  {
    auto& sc = s.project.scene;
    auto erase = [&](auto& v) {
      const auto before = v.size();
      v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& o) { return o.id == id; }), v.end());
      return v.size() != before;
    };
    if (erase(sc.actors) || erase(sc.props) || erase(sc.effects) || erase(sc.graphics))
    {
      if (s.selectedObject == id)
        s.selectedObject.clear();
      s.Commit("Delete object");
    }
  }

  void DuplicateObject(Session& s, const std::string& id)
  {
    auto& sc = s.project.scene;
    const iris::Vec3 offset{24, 24, 0};
    auto dup = [&](auto& v, const char* prefix) {
      for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id)
        {
          auto copy = v[i];
          copy.id = iris::MakeUniqueId(s.project, prefix);
          copy.name += " copy";
          copy.transform.position += offset;
          if constexpr (requires { copy.track; })
          {
            // The copy starts from its own spot; movement keys stay with the original.
            copy.track.x.Clear();
            copy.track.y.Clear();
            copy.track.z.Clear();
          }
          v.push_back(std::move(copy));
          SelectObject(s, v.back().id);
          s.Commit("Duplicate object");
          return true;
        }
      return false;
    };
    if (!dup(sc.actors, "actor") && !dup(sc.props, "prop") && !dup(sc.effects, "fx"))
      dup(sc.graphics, "gfx");
  }

  iris::Vec3 PlacementPoint(Session& s, double distance)
  {
    if (const auto hit = Director::ViewTarget(); hit && hit->fraction < 0.5)
      return hit->position;
    const auto cam = Runtime::LastAppliedCamera();
    if (!cam)
      return s.status.gameView ? s.status.gameView->position : iris::Vec3{};
    iris::Vec3 p = cam->position + iris::AnglesToForward(cam->angles) * distance;
    return p;
  }

  // ---------------------------------------------------------------- Director home

  void BuildPendingTemplate(Session& s)
  {
    // The template the wizard chose, once the set is up. Called every frame:
    // it used to run inside the Director panel, which ImGui skips while the
    // panel is a tab behind another one, so the template was never built.
    if (!s.pendingTemplate.empty() && Director::Ready() && Catalog().version > 0 && s.status.cgameReady &&
        !s.status.demoPlaying)
    {
      iris::TemplateAssets assets;
      const auto& c = Catalog();
      assets.characters = c.characters;
      assets.animations = c.animationNames;
      assets.weapons = c.weaponNames;
      assets.models = c.modelNames;
      assets.effects = c.effectNames;
      const auto r = iris::ApplySceneTemplate(s.project, s.pendingTemplate, PlacementPoint(s), CameraYaw() + 180.0, assets,
                                              s.playhead);
      for (const auto& w : r.warnings)
        Notify(w);
      if (!r.created.empty())
      {
        SelectObject(s, r.created.front());
        Shown(Panel::ObjectInspector) = true;
        ImGui::SetWindowFocus(PanelName(Panel::ObjectInspector));
      }
      s.pendingTemplate.clear();
      s.Commit("Scene template");
    }
  }

  void DrawDirector(Session& s)
  {
    auto& ui = Ui();
    if (!BeginPanel(Panel::Director))
    {
      ImGui::End();
      return;
    }
    const auto st = Director::GetStatus();

    SectionHeader(Icons::FILM, "Director Mode");
    ImGui::TextWrapped("Build a cinematic on any installed map: characters, props, effects, 3D titles and cameras, "
                       "without recording gameplay.");
    ImGui::Spacing();
    const float w = ImGui::GetContentRegionAvail().x;
    auto big = [&](const char* icon, const char* label) {
      return ImGui::Button((std::string(icon) + "  " + label).c_str(), ImVec2(w, 34));
    };
    if (big(Icons::PLUS, "Create new scene"))
    {
      ui.wizardOpen = true;
      ui.wizardStep = 0;
      ui.mapsRequested = false;
    }
    if (big(Icons::VIDEO, "Edit a recorded demo"))
      Shown(Panel::Demos) = true;
    if (big(Icons::FOLDER_OPEN, "Open an existing project"))
      ui.openPopup = true;
    if (big(Icons::MAGIC, "Browse templates"))
    {
      ui.wizardOpen = true;
      ui.wizardStep = st.state == Director::SetState::Ready ? 1 : 0;
      ui.mapsRequested = false;
    }
    if (big(Icons::LIST, "Render queue"))
      Shown(Panel::RenderQueue) = true;

    ImGui::Spacing();
    SectionHeader(Icons::MAP, "Film set");
    switch (st.state)
    {
      case Director::SetState::None:
        ImGui::TextDisabled("No film set. Create a new scene to load one.");
        break;
      case Director::SetState::Loading:
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s  Loading %s ...", Icons::SYNC, st.map.c_str());
        break;
      case Director::SetState::Ready:
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "%s  %s is ready", Icons::CHECK, st.map.c_str());
        ImGui::TextDisabled("%zu object(s) in the game, %zu request(s) pending", st.objects, st.pendingRequests);
        break;
      case Director::SetState::Failed:
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s  The film set did not start", Icons::WARNING);
        break;
    }
    if (!st.lastError.empty())
      ImGui::TextWrapped("%s", st.lastError.c_str());
    if (st.state != Director::SetState::None)
    {
      if (ImGui::Button((std::string(Icons::SYNC) + "  Reload set").c_str()))
        s.Post([] { Director::ReloadFilmSet(); });
      ImGui::SetItemTooltip("Restarts the map so newly used characters, animations and effects are loaded");
      ImGui::SameLine();
      if (ImGui::Button((std::string(Icons::TIMES) + "  Leave set").c_str()))
        s.Post([] { Director::LeaveFilmSet(); });
    }
    if (!st.needReload.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s  %zu new asset(s) appear after \"Reload set\"", Icons::INFO,
                         st.needReload.size());
      if (Pro(s))
        for (const auto& n : st.needReload)
          ImGui::BulletText("%s", n.c_str());
    }
    if (!st.unavailable.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s  %zu asset(s) of this project are not on this map", Icons::WARNING,
                         st.unavailable.size());
      for (const auto& n : st.unavailable)
        ImGui::BulletText("%s", n.c_str());
      ImGui::TextDisabled("They are skipped. Choose replacements in the object inspector.");
    }
    if (st.state == Director::SetState::Ready)
    {
      ImGui::TextDisabled("%d characters' models, %d animations, %d weapons, %d effects precached",
                          st.precachedModels, st.precachedAnimations, st.precachedWeapons, st.precachedEffects);
      ImGui::Spacing();
      SectionHeader(Icons::MAGIC, "Templates");
      ImGui::TextDisabled("Built where the camera looks, as ordinary editable objects.");
      for (const auto& t : iris::SceneTemplates())
      {
        ImGui::PushID(t.id.c_str());
        if (ImGui::Button("Build"))
          s.pendingTemplate = t.id;
        ImGui::SameLine();
        ImGui::TextUnformatted(t.label.c_str());
        ImGui::SetItemTooltip("%s", t.description.c_str());
        ImGui::PopID();
      }
    }
    if (Pro(s) && ImGui::CollapsingHeader("Director Mod log"))
    {
      ImGui::BeginChild("##dirlog", ImVec2(0, 160), ImGuiChildFlags_Borders);
      for (const auto& l : st.log)
        ImGui::TextUnformatted(l.c_str());
      ImGui::EndChild();
      ImGui::TextDisabled("Protocol %d, mod: %s", Director::kProtocol, st.capabilities.c_str());
    }
    ImGui::End();
  }

  void DrawSceneWizard(Session& s)
  {
    auto& ui = Ui();
    if (ui.wizardOpen)
    {
      ImGui::OpenPopup("Create new scene");
      ui.wizardOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Create new scene", nullptr, ImGuiWindowFlags_NoSavedSettings))
      return;
    if (!ui.mapsRequested)
    {
      ui.mapsRequested = true;
      s.Post([] { Director::RefreshMaps(); });
    }
    const char* steps[] = {"1  Map", "2  Template", "3  Create"};
    for (int i = 0; i < 3; ++i)
    {
      if (i)
        ImGui::SameLine();
      ImGui::TextColored(i == ui.wizardStep ? CurrentPalette().accent : ImVec4(0.55f, 0.55f, 0.6f, 1.0f), "%s", steps[i]);
    }
    ImGui::Separator();
    if (ui.wizardStep == 0)
    {
      ImGui::SetNextItemWidth(-90);
      ImGui::InputTextWithHint("##mapsearch", (std::string(Icons::SEARCH) + "  Search maps").c_str(), &ui.mapSearch);
      ImGui::SameLine();
      ImGui::Checkbox(Icons::STAR, &ui.mapFavoritesOnly);
      ImGui::BeginChild("##maps", ImVec2(0, -40), ImGuiChildFlags_Borders);
      auto maps = Director::Maps();
      // Recent first.
      std::stable_sort(maps.begin(), maps.end(), [&](const auto& a, const auto& b) {
        auto rank = [&](const std::string& n) {
          const auto& r = s.settings.recentMaps;
          const auto it = std::find(r.begin(), r.end(), n);
          return it == r.end() ? 1000 : static_cast<int>(it - r.begin());
        };
        return rank(a.name) < rank(b.name);
      });
      if (maps.empty())
        ImGui::TextDisabled("Reading the map list ...");
      for (const auto& m : maps)
      {
        const bool fav = s.settings.favoriteMaps.contains(m.name);
        if (ui.mapFavoritesOnly && !fav)
          continue;
        iris::AssetEntry e;
        e.name = m.name;
        e.label = m.label;
        e.category = m.description;
        if (!ui.mapSearch.empty() && !iris::MatchesSearch(e, ui.mapSearch))
          continue;
        ImGui::PushID(m.name.c_str());
        if (ImGui::SmallButton(fav ? Icons::STAR : "-"))
        {
          if (fav)
            s.settings.favoriteMaps.erase(m.name);
          else
            s.settings.favoriteMaps.insert(m.name);
          SaveSettings(s);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m.installed);
        if (ImGui::Selectable(m.label.c_str(), ui.wizardMap == m.name, ImGuiSelectableFlags_AllowDoubleClick))
        {
          ui.wizardMap = m.name;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            ui.wizardStep = 1;
        }
        ImGui::EndDisabled();
        if (!m.installed)
          ImGui::SetItemTooltip("Not installed");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::TextDisabled("%s", Pro(s) ? m.name.c_str() : m.description.c_str());
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::BeginDisabled(ui.wizardMap.empty());
      if (ImGui::Button("Next", ImVec2(120, 0)))
        ui.wizardStep = 1;
      ImGui::EndDisabled();
    }
    else if (ui.wizardStep == 1)
    {
      ImGui::BeginChild("##templates", ImVec2(0, -40), ImGuiChildFlags_Borders);
      for (const auto& t : iris::SceneTemplates())
      {
        if (ImGui::Selectable(t.label.c_str(), ui.wizardTemplate == t.id, ImGuiSelectableFlags_AllowDoubleClick))
        {
          ui.wizardTemplate = t.id;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            ui.wizardStep = 2;
        }
        ImGui::Indent();
        ImGui::TextDisabled("%s", t.description.c_str());
        ImGui::Unindent();
      }
      ImGui::EndChild();
      if (ImGui::Button("Back", ImVec2(120, 0)))
        ui.wizardStep = 0;
      ImGui::SameLine();
      if (ImGui::Button("Next", ImVec2(120, 0)))
        ui.wizardStep = 2;
    }
    else
    {
      const bool sameSet = Director::Ready() && Director::GetStatus().map == ui.wizardMap;
      ImGui::TextWrapped("Map: %s", ui.wizardMap.empty() ? "(current film set)" : ui.wizardMap.c_str());
      for (const auto& t : iris::SceneTemplates())
        if (t.id == ui.wizardTemplate)
          ImGui::TextWrapped("Template: %s. %s", t.label.c_str(), t.description.c_str());
      ImGui::Spacing();
      ImGui::TextWrapped("The map loads as a local offline film set (a few seconds). The template is built where the "
                         "camera looks; move anything afterwards. Characters, animations and weapons come from the "
                         "map itself.");
      ImGui::Spacing();
      if (ImGui::Button("Back", ImVec2(120, 0)))
        ui.wizardStep = 1;
      ImGui::SameLine();
      if (ImGui::Button((std::string(Icons::CHECK) + "  Create scene").c_str(), ImVec2(180, 0)))
      {
        s.pendingTemplate = ui.wizardTemplate;
        if (!sameSet && !ui.wizardMap.empty())
        {
          s.project.mapName = ui.wizardMap;
          s.settings.recentMaps = iris::PushRecent(s.settings.recentMaps, ui.wizardMap, 8);
          SaveSettings(s);
          const std::string map = ui.wizardMap, gametype = s.project.scene.filmSet.gametype;
          s.Post([map, gametype] {
            std::string error;
            if (!Director::StartFilmSet(map, gametype, &error))
              Notify(error);
          });
          s.Commit("Film set");
        }
        Shown(Panel::Director) = true;
        Shown(Panel::ObjectInspector) = true;
        // Show the film set's progress ("Loading mp_afghan ...") in front.
        ImGui::SetWindowFocus(PanelName(Panel::Director));
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
    if (ImGui::Button("Cancel", ImVec2(80, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ---------------------------------------------------------------- libraries

  namespace
  {
    bool CategoryCombo(const char* id, std::string& current, const std::vector<std::string>& categories)
    {
      bool changed = false;
      ImGui::SetNextItemWidth(160);
      if (ImGui::BeginCombo(id, current.empty() ? "All categories" : current.c_str()))
      {
        if (ImGui::Selectable("All categories", current.empty()))
        {
          current.clear();
          changed = true;
        }
        for (const auto& c : categories)
          if (ImGui::Selectable(c.c_str(), current == c))
          {
            current = c;
            changed = true;
          }
        ImGui::EndCombo();
      }
      return changed;
    }

    bool NeedFilmSet()
    {
      if (Director::Ready())
        return true;
      ImGui::TextWrapped("The libraries list the assets of the loaded film set. Create a new scene (Director panel) "
                         "to load a map.");
      if (ImGui::Button("Open the Director panel"))
        Shown(Panel::Director) = true;
      return false;
    }
  }  // namespace

  void DrawCharacters(Session& s)
  {
    auto& ui = Ui();
    if (!BeginPanel(Panel::Actors))
    {
      ImGui::End();
      return;
    }
    if (!NeedFilmSet())
    {
      ImGui::End();
      return;
    }
    const auto& cat = Catalog();
    ImGui::SetNextItemWidth(-200);
    ImGui::InputTextWithHint("##chsearch", (std::string(Icons::SEARCH) + "  Search characters").c_str(), &ui.characterSearch);
    ImGui::SameLine();
    CategoryCombo("##chcat", ui.characterCategory, iris::CharacterCategories());
    ImGui::SameLine();
    ImGui::Checkbox(Icons::STAR, &ui.characterFavorites);
    iris::ActorDef* selected = s.project.scene.FindActor(s.selectedObject);
    ImGui::BeginChild("##chars", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (cat.characters.empty())
      ImGui::TextDisabled("This map has no multiplayer character models loaded.");
    for (const auto& category : iris::CharacterCategories())
    {
      std::vector<const iris::CharacterEntry*> list;
      for (const auto& c : cat.characters)
        if (c.category == category && (ui.characterCategory.empty() || ui.characterCategory == category) &&
            (ui.characterSearch.empty() || iris::MatchesSearch(c, ui.characterSearch)) &&
            (!ui.characterFavorites || s.settings.favoriteAssets.contains(c.body)))
          list.push_back(&c);
      if (list.empty())
        continue;
      if (!ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        continue;
      for (const auto* c : list)
      {
        ImGui::PushID(c->body.c_str());
        Favourite(s, c->body);
        ImGui::SameLine();
        if (ImGui::Selectable(c->label.c_str(), selected && selected->bodyModel == c->body, ImGuiSelectableFlags_AllowDoubleClick))
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            AddActorFrom(s, *c);
        if (Pro(s))
          ImGui::SetItemTooltip("%s\n%s", c->body.c_str(), c->head.empty() ? "(no head model)" : c->head.c_str());
        if (ImGui::BeginPopupContextItem("##chctx"))
        {
          if (ImGui::MenuItem("Add to the scene"))
            AddActorFrom(s, *c);
          if (selected && ImGui::MenuItem(("Use for " + selected->name).c_str()))
          {
            selected->bodyModel = c->body;
            selected->headModel = c->head;
            selected->character = c->label;
            Recent(s, c->body);
            s.Commit("Change character");
          }
          ImGui::EndPopup();
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
        if (ImGui::SmallButton(Icons::PLUS))
          AddActorFrom(s, *c);
        ImGui::SetItemTooltip("Add to the scene where the camera looks (double-click works too)");
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    ImGui::End();
  }

  void DrawAnimations(Session& s)
  {
    auto& ui = Ui();
    if (!BeginPanel(Panel::Animations))
    {
      ImGui::End();
      return;
    }
    if (!NeedFilmSet())
    {
      ImGui::End();
      return;
    }
    iris::ActorDef* actor = s.project.scene.FindActor(s.selectedObject);
    if (actor)
      ImGui::TextColored(CurrentPalette().accent, "%s  %s", Icons::USER, actor->name.c_str());
    else
      ImGui::TextDisabled("Select an actor to give it animations.");
    ImGui::SetNextItemWidth(-200);
    ImGui::InputTextWithHint("##ansearch", (std::string(Icons::SEARCH) + "  Search animations").c_str(), &ui.animationSearch);
    ImGui::SameLine();
    CategoryCombo("##ancat", ui.animationCategory, iris::AnimationCategories());
    ImGui::BeginChild("##anims", ImVec2(0, 0), ImGuiChildFlags_Borders);
    int shown = 0;
    for (const auto& a : Catalog().animations)
    {
      if (a.hidden && !Pro(s))
        continue;
      if (!ui.animationCategory.empty() && a.category != ui.animationCategory)
        continue;
      if (!ui.animationSearch.empty() && !iris::MatchesSearch(a, ui.animationSearch))
        continue;
      if (++shown > 600)
      {
        ImGui::TextDisabled("Refine the search to see more.");
        break;
      }
      ImGui::PushID(a.name.c_str());
      Favourite(s, a.name);
      ImGui::SameLine();
      ImGui::Selectable(a.label.c_str());
      ImGui::SetItemTooltip("%s%s%s", a.category.c_str(), Pro(s) ? "\n" : "", Pro(s) ? a.name.c_str() : "");
      if (actor)
      {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
        if (ImGui::SmallButton("Clip"))
        {
          iris::AnimClip c;
          c.id = "clip-" + std::to_string(actor->clips.size() + 1) + "-" + std::to_string(static_cast<int>(s.playhead * 1000));
          c.anim = a.name;
          c.start = s.project.timeline.snapToFrames ? s.project.timeline.frameRate.SnapSeconds(s.playhead) : s.playhead;
          c.duration = 2.0;
          actor->clips.push_back(c);
          Recent(s, a.name);
          s.Commit("Add animation clip");
        }
        ImGui::SetItemTooltip("Add a 2 s clip at the playhead (it plays right away)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Idle"))
        {
          actor->idleAnim = a.name;
          Recent(s, a.name);
          s.Commit("Idle animation");
        }
        ImGui::SetItemTooltip("Play when no clip is active");
      }
      ImGui::PopID();
    }
    if (shown == 0)
      ImGui::TextDisabled("No animation matches.");
    ImGui::EndChild();
    ImGui::End();
  }

  void DrawAssets(Session& s)
  {
    auto& ui = Ui();
    if (!BeginPanel(Panel::Assets))
    {
      ImGui::End();
      return;
    }
    if (!NeedFilmSet())
    {
      ImGui::End();
      return;
    }
    if (ImGui::BeginTabBar("##assettabs"))
    {
      if (ImGui::BeginTabItem("Props"))
      {
        ui.assetTab = 0;
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Effects"))
      {
        ui.assetTab = 1;
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    const bool props = ui.assetTab == 0;
    ImGui::SetNextItemWidth(-200);
    ImGui::InputTextWithHint("##assearch", (std::string(Icons::SEARCH) + "  Search").c_str(), &ui.assetSearch);
    ImGui::SameLine();
    CategoryCombo("##ascat", ui.assetCategory, props ? iris::PropCategories() : iris::EffectCategories());
    ImGui::BeginChild("##assets", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const auto& list = props ? Catalog().props : Catalog().effects;
    int shown = 0;
    for (const auto& e : list)
    {
      if (e.hidden && !Pro(s))
        continue;
      if (!ui.assetCategory.empty() && e.category != ui.assetCategory)
        continue;
      if (!ui.assetSearch.empty() && !iris::MatchesSearch(e, ui.assetSearch))
        continue;
      if (++shown > 600)
      {
        ImGui::TextDisabled("Refine the search to see more.");
        break;
      }
      ImGui::PushID(e.name.c_str());
      Favourite(s, e.name);
      ImGui::SameLine();
      const bool place = ImGui::Selectable(e.label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                         ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
      ImGui::SetItemTooltip("%s%s%s", e.category.c_str(), Pro(s) ? "\n" : "", Pro(s) ? e.name.c_str() : "");
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
      if (ImGui::SmallButton(Icons::PLUS) || place)
      {
        if (props)
        {
          iris::PropDef p;
          p.id = iris::MakeUniqueId(s.project, "prop");
          p.name = e.label;
          p.model = e.name;
          p.transform.position = PlacementPoint(s);
          p.transform.angles = {0, CameraYaw() + 180.0, 0};
          s.project.scene.props.push_back(p);
          SelectObject(s, p.id);
          s.Commit("Add prop");
        }
        else
        {
          iris::EffectDef fx;
          fx.id = iris::MakeUniqueId(s.project, "fx");
          fx.name = e.label;
          fx.fx = e.name;
          fx.start = s.playhead;
          fx.transform.position = PlacementPoint(s);
          fx.transform.angles = {-90, 0, 0};  // effects play along their forward axis: up
          s.project.scene.effects.push_back(fx);
          SelectObject(s, fx.id);
          s.Commit("Add effect");
        }
        Recent(s, e.name);
      }
      ImGui::SetItemTooltip("Place where the camera looks");
      ImGui::PopID();
    }
    if (shown == 0)
      ImGui::TextDisabled("Nothing matches.");
    ImGui::EndChild();
    ImGui::End();
  }

  // ---------------------------------------------------------------- motion graphics

  void DrawGraphics(Session& s)
  {
    auto& ui = Ui();
    if (!BeginPanel(Panel::Graphics))
    {
      ImGui::End();
      return;
    }
    if (const auto f = TakePicked("new-logo"))
    {
      auto g = NewGraphic(s, iris::GraphicKind::Logo, std::filesystem::path(std::u8string(f->begin(), f->end())).stem().string());
      g.source = *f;
      g.materials.front = iris::MakeMaterial("polished_metal");
      g.useLogoColors = false;
      AddGraphic(s, std::move(g));
    }
    if (const auto f = TakePicked("new-image"))
    {
      auto g = NewGraphic(s, iris::GraphicKind::Image, "Image");
      g.source = *f;
      g.materials.front = iris::MakeMaterial("matte_white");
      g.materials.front.emission = {1, 1, 1};
      g.materials.front.emissionStrength = 0.6;
      AddGraphic(s, std::move(g));
    }
    if (const auto f = TakePicked("new-model"))
    {
      auto g = NewGraphic(s, iris::GraphicKind::Model, std::filesystem::path(std::u8string(f->begin(), f->end())).stem().string());
      g.source = *f;
      AddGraphic(s, std::move(g));
    }

    SectionHeader(Icons::PLUS, "Add graphics");
    const float bw = (ImGui::GetContentRegionAvail().x - 8) / 3.0f;
    if (ImGui::Button("3D Text", ImVec2(bw, 0)))
    {
      auto g = NewGraphic(s, iris::GraphicKind::Text, "Title");
      g.text = "IW4IRIS";
      g.materials.front = iris::MakeMaterial("matte_white");
      AddGraphic(s, std::move(g));
    }
    ImGui::SameLine();
    if (ImGui::Button("Logo ...", ImVec2(bw, 0)))
      PickFile("new-logo", kLogoFilter);
    ImGui::SetItemTooltip("SVG artwork or a transparent PNG, turned into 3D geometry");
    ImGui::SameLine();
    if (ImGui::Button("Image ...", ImVec2(bw, 0)))
      PickFile("new-image", kImageFilter);
    ImGui::SetItemTooltip("A flat, textured plane that keeps the image's colours");
    if (ImGui::Button("3D Model ...", ImVec2(bw, 0)))
      PickFile("new-model", kModelFilter);
    ImGui::SetItemTooltip("A Wavefront OBJ (exported from Blender, Cinema 4D, ...)");
    ImGui::SameLine();
    if (ImGui::Button("Shape", ImVec2(bw, 0)))
      ImGui::OpenPopup("##shapes");
    if (ImGui::BeginPopup("##shapes"))
    {
      for (const auto k : {iris::ShapeKind::Box, iris::ShapeKind::Cylinder, iris::ShapeKind::Sphere, iris::ShapeKind::Plane,
                           iris::ShapeKind::Ring})
      {
        std::string n(iris::ToString(k));
        n[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
        if (ImGui::MenuItem(n.c_str()))
        {
          auto g = NewGraphic(s, iris::GraphicKind::Shape, n);
          g.shape = k;
          g.materials.front = iris::MakeMaterial("painted_steel");
          AddGraphic(s, std::move(g));
        }
      }
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Tracking anchor", ImVec2(bw, 0)))
      AddGraphic(s, NewGraphic(s, iris::GraphicKind::Anchor, "Anchor"));
    ImGui::SetItemTooltip("An invisible point other graphics can follow, and that exports to Blender / After Effects");

    ImGui::Spacing();
    SectionHeader(Icons::MAGIC, "Cinematic title");
    ImGui::InputTextMultiline("##titletext", &ui.titleText, ImVec2(-1, ImGui::GetTextLineHeight() * 3));
    const auto& titles = iris::TitleTemplates();
    ui.titleTemplate = std::clamp(ui.titleTemplate, 0, static_cast<int>(titles.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##titletpl", titles[static_cast<std::size_t>(ui.titleTemplate)].label.c_str()))
    {
      for (int i = 0; i < static_cast<int>(titles.size()); ++i)
      {
        if (ImGui::Selectable(titles[static_cast<std::size_t>(i)].label.c_str(), i == ui.titleTemplate))
          ui.titleTemplate = i;
        ImGui::SetItemTooltip("%s", titles[static_cast<std::size_t>(i)].description.c_str());
      }
      ImGui::EndCombo();
    }
    static bool orbit = true;
    ImGui::Checkbox("Generate a camera orbit", &orbit);
    if (ImGui::Button((std::string(Icons::CHECK) + "  Create title").c_str(), ImVec2(-1, 28)))
    {
      const auto& tpl = titles[static_cast<std::size_t>(ui.titleTemplate)];
      const iris::Vec3 at = PlacementPoint(s) + iris::Vec3{0, 0, 60};
      const auto r = iris::ApplyTitleTemplate(s.project, tpl.id, ui.titleText, at, CameraYaw() + 180.0, "", s.playhead);
      for (const auto& w : r.warnings)
        Notify(w);
      if (!r.created.empty())
      {
        if (iris::GraphicDef* g = s.project.scene.FindGraphic(r.created.front()))
        {
          if (tpl.id == "floatingName")
            if (iris::ActorDef* a = s.project.scene.FindActor(s.selectedObject))
            {
              g->tracking.mode = iris::TrackingMode::Billboard;
              g->transform.position = a->transform.position + iris::Vec3{0, 0, 84};
            }
          if (orbit)
          {
            // Approximate bounds until the geometry is built: size x 4 wide.
            const double size = g->textSettings.size;
            const iris::Bounds3D b{{-size * 2.0, -size * 0.5, -3}, {size * 2.0, size * 0.5, 3}};
            const auto camId = iris::AddGraphicOrbit(s.project, *g, b, s.playhead, 6.0);
            s.project.activeCameraId = camId;
          }
        }
        SelectObject(s, r.created.front());
      }
      s.Commit("Create title");
    }

    ImGui::Spacing();
    SectionHeader(Icons::LAYER, "Graphics in the scene");
    const auto listStatus = Graphics::GetStatus();
    for (const auto& g : s.project.scene.graphics)
    {
      ImGui::PushID(g.id.c_str());
      const auto& gs = listStatus;
      const bool broken = gs.errors.contains(g.id);
      if (ImGui::Selectable(((broken ? std::string(Icons::WARNING) + "  " : std::string()) + g.name).c_str(), s.selectedObject == g.id))
        SelectObject(s, g.id);
      if (broken)
        ImGui::SetItemTooltip("%s", gs.errors.at(g.id).c_str());
      ImGui::PopID();
    }
    if (s.project.scene.graphics.empty())
      ImGui::TextDisabled("No graphics yet.");

    ImGui::Spacing();
    SectionHeader(Icons::INFO, "Renderer");
    const auto gs = Graphics::GetStatus();
    ImGui::TextWrapped("Depth occlusion: %s", gs.depth.empty() ? "not checked yet (no graphic drawn)" : gs.depth.c_str());
    if (gs.building > 0)
      ImGui::TextColored(CurrentPalette().accent, "%s  Building %d graphic(s) ...", Icons::SYNC, gs.building);
    if (!gs.degraded.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s  %zu graphic(s) %s: they should hide behind walls, but "
                         "occlusion is not available.", Icons::WARNING, gs.degraded.size(),
                         gs.previewWithoutOcclusion ? "are previewed on top" : "are not drawn");
      ImGui::TextWrapped("Set them to \"Always on top\" to render them without occlusion. Recording is blocked while "
                         "they are set to hide behind walls.");
    }
    bool previewOnTop = gs.previewWithoutOcclusion;
    if (ImGui::Checkbox("Preview hidden-mode graphics on top while occlusion is unavailable", &previewOnTop))
      Graphics::SetPreviewWithoutOcclusion(previewOnTop);
    ImGui::SetItemTooltip("Only in the editor, to place them. Recordings never draw them this way.");
    if (Pro(s))
    {
      ImGui::TextWrapped("View: %s%s", gs.matrices.empty() ? "-" : gs.matrices.c_str(), gs.reversedDepth ? ", reversed depth" : "");
      ImGui::TextDisabled("Frame sync %s, %d graphic(s), %d triangles last frame", gs.timeSynced ? "exact" : "approximate",
                          gs.drawnGraphics, gs.drawnTriangles);
    }
    ImGui::TextDisabled("Graphics are lit by their own key light and drawn after the game's post effects.");
    ImGui::End();
  }

  // ---------------------------------------------------------------- inspector

  namespace
  {
    bool TransformEditor(Session& s, iris::Transform& rest, iris::TransformTrack& track, bool scale, bool fullRotation)
    {
      iris::Transform t = track.Evaluate(s.playhead, rest);
      bool changed = false;
      changed |= DragVec3("Position", &t.position, 1.0f, "%.1f");
      if (fullRotation || Pro(s))
      {
        iris::Vec3 a{t.angles.pitch, t.angles.yaw, t.angles.roll};
        if (DragVec3("Rotation", &a, 0.5f, "%.1f"))
        {
          t.angles = {a.x, a.y, a.z};
          changed = true;
        }
      }
      else
        changed |= DragDouble("Facing", &t.angles.yaw, 0.5f, -1e6, 1e6, "%.1f deg");
      if (scale)
      {
        double u = t.scale.x;
        if (Pro(s))
          changed |= DragVec3("Scale", &t.scale, 0.01f, "%.3f");
        else if (DragDouble("Size", &u, 0.01f, 0.01, 100.0, "%.2f x"))
        {
          t.scale = {u, u, u};
          changed = true;
        }
      }
      if (changed)
        SetTransform(s, rest, track, t);
      const bool keyed = !track.Empty();
      if (ImGui::Button((std::string(Icons::KEY) + (keyed ? "  Key at playhead" : "  Animate (key here)")).c_str()))
      {
        track.SetKey(s.playhead, t, iris::Interpolation::Smooth, scale);
        s.Commit("Key transform");
      }
      ImGui::SetItemTooltip("Keys position, rotation%s at the playhead; once keyed, edits key automatically", scale ? " and size" : "");
      if (keyed)
      {
        ImGui::SameLine();
        if (ImGui::Button("Delete key"))
          if (track.RemoveKeysAt(s.playhead, 1e-3))
            s.Commit("Delete transform key");
        ImGui::SameLine();
        if (ImGui::Button("Clear animation"))
        {
          rest = t;
          track.Clear();
          s.Commit("Clear transform animation");
        }
      }
      if (ImGui::Button("Move to view target"))
      {
        t.position = PlacementPoint(s);
        SetTransform(s, rest, track, t);
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Face the camera"))
      {
        if (const auto cam = Runtime::LastAppliedCamera())
        {
          const iris::Vec3 d = cam->position - t.position;
          t.angles.yaw = std::atan2(d.y, d.x) * iris::kRadToDeg;
          SetTransform(s, rest, track, t);
          changed = true;
        }
      }
      if (Director::Ready())
      {
        ImGui::SameLine();
        if (ImGui::Button("Snap to ground"))
        {
          const iris::Vec3 from = t.position + iris::Vec3{0, 0, 24};
          const std::string id = s.selectedObject;
          // The trace runs on the game's main thread.
          s.Post([id, from] {
            auto& ss = GetSession();
            const auto hit = Director::TraceSurface(from, from - iris::Vec3{0, 0, 8192});
            std::lock_guard _(ss.mutex);
            if (!hit)
            {
              Notify("No ground below the object.");
              return;
            }
            auto apply = [&](auto* o) {
              if (!o)
                return false;
              iris::Transform tt = o->track.Evaluate(ss.playhead, o->transform);
              tt.position.z = hit->position.z;
              if (o->track.Empty())
                o->transform = tt;
              else
                o->track.SetKey(ss.playhead, tt);
              ss.Commit("Snap to ground");
              return true;
            };
            if (!apply(ss.project.scene.FindActor(id)))
              apply(ss.project.scene.FindProp(id));
          });
        }
      }
      if (changed)
        s.Commit("Move object", true);
      return changed;
    }

    template <typename T>
    void CommonFlags(Session& s, T& o)
    {
      bool changed = ImGui::Checkbox("Visible", &o.visible);
      ImGui::SameLine();
      changed |= ImGui::Checkbox("Locked", &o.locked);
      if (changed)
        s.Commit("Object flags");
    }

    void WeaponCombo(Session& s, std::string& weapon, const char* label)
    {
      const std::string current = weapon.empty() ? std::string("Unarmed") : iris::ClassifyWeapon(weapon).label;
      if (ImGui::BeginCombo(label, current.c_str(), ImGuiComboFlags_HeightLarge))
      {
        if (ImGui::Selectable("Unarmed", weapon.empty()))
        {
          weapon.clear();
          s.Commit("Weapon");
        }
        std::string lastCategory;
        for (const auto& w : Catalog().weapons)
        {
          if (w.hidden && !Pro(s))
            continue;
          if (w.category != lastCategory)
          {
            ImGui::SeparatorText(w.category.c_str());
            lastCategory = w.category;
          }
          if (ImGui::Selectable((w.label + "##" + w.name).c_str(), weapon == w.name))
          {
            weapon = w.name;
            Recent(s, w.name);
            s.Commit("Weapon");
          }
          if (Pro(s))
            ImGui::SetItemTooltip("%s", w.name.c_str());
        }
        ImGui::EndCombo();
      }
    }

    void AnimCombo(Session& s, std::string& anim, const char* label)
    {
      if (ImGui::BeginCombo(label, AnimLabel(anim).c_str(), ImGuiComboFlags_HeightLarge))
      {
        if (ImGui::Selectable("(none)", anim.empty()))
        {
          anim.clear();
          s.Commit("Animation");
        }
        int n = 0;
        for (const auto& a : Catalog().animations)
        {
          if (a.hidden && !Pro(s))
            continue;
          if (++n > 400)
            break;
          if (ImGui::Selectable((a.label + "##" + a.name).c_str(), anim == a.name))
          {
            anim = a.name;
            s.Commit("Animation");
          }
        }
        ImGui::EndCombo();
      }
    }

    void ActorCombo(Session& s, std::string& actor, const char* label, const std::string& except = {})
    {
      const iris::ActorDef* a = s.project.scene.FindActor(actor);
      if (ImGui::BeginCombo(label, a ? a->name.c_str() : "(none)"))
      {
        if (ImGui::Selectable("(none)", actor.empty()))
        {
          actor.clear();
          s.Commit("Target actor");
        }
        for (const auto& o : s.project.scene.actors)
          if (o.id != except && ImGui::Selectable((o.name + "##" + o.id).c_str(), actor == o.id))
          {
            actor = o.id;
            s.Commit("Target actor");
          }
        ImGui::EndCombo();
      }
    }

    void AttachCombo(Session& s, std::string& attach, const char* label)
    {
      if (ImGui::BeginCombo(label, attach.empty() ? "origin" : attach.c_str()))
      {
        for (const auto& p : Director::AttachPoints())
          if (ImGui::Selectable(p.c_str(), attach == p))
          {
            attach = p;
            s.Commit("Attach point");
          }
        ImGui::EndCombo();
      }
    }

    void ActorInspector(Session& s, iris::ActorDef& a)
    {
      auto& ui = Ui();
      ImGui::TextColored(CurrentPalette().accent, "%s  Actor", Icons::USER);
      if (ImGui::InputText("Name", &a.name))
        s.Commit("Rename actor", true);
      // Character
      const iris::CharacterEntry* ch = CharacterFor(a.bodyModel);
      if (ImGui::BeginCombo("Character", ch ? ch->label.c_str() : (a.character.empty() ? a.bodyModel.c_str() : a.character.c_str()),
                            ImGuiComboFlags_HeightLarge))
      {
        std::string lastCategory;
        for (const auto& c : Catalog().characters)
        {
          if (c.category != lastCategory)
          {
            ImGui::SeparatorText(c.category.c_str());
            lastCategory = c.category;
          }
          if (ImGui::Selectable((c.label + "##" + c.body).c_str(), c.body == a.bodyModel))
          {
            a.bodyModel = c.body;
            a.headModel = c.head;
            a.character = c.label;
            s.Commit("Change character");
          }
        }
        ImGui::EndCombo();
      }
      if (Pro(s))
      {
        if (ImGui::InputText("Body model", &a.bodyModel))
          s.Commit("Body model", true);
        if (ImGui::InputText("Head model", &a.headModel))
          s.Commit("Head model", true);
      }
      CommonFlags(s, a);
      if (a.locked)
        ImGui::BeginDisabled();
      SectionHeader(Icons::ARROWS, "Placement");
      TransformEditor(s, a.transform, a.track, false, false);

      SectionHeader(Icons::SHIELD, "Equipment");
      WeaponCombo(s, a.weapon, "Weapon");
      if (Pro(s))
      {
        int camo = a.camo;
        if (ImGui::SliderInt("Camo index", &camo, 0, 8))
        {
          a.camo = camo;
          s.Commit("Camo", true);
        }
        if (ImGui::InputText("Weapon tag", &a.weaponTag))
          s.Commit("Weapon tag", true);
        for (std::size_t i = 0; i < a.attachments.size(); ++i)
        {
          ImGui::PushID(static_cast<int>(i));
          auto& at = a.attachments[i];
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
          if (ImGui::InputText("##atmodel", &at.model))
            s.Commit("Attachment", true);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
          if (ImGui::InputText("##attag", &at.tag))
            s.Commit("Attachment", true);
          ImGui::SameLine();
          if (ImGui::SmallButton(Icons::TRASH))
          {
            a.attachments.erase(a.attachments.begin() + static_cast<std::ptrdiff_t>(i));
            s.Commit("Remove attachment");
            ImGui::PopID();
            break;
          }
          ImGui::PopID();
        }
        if (ImGui::SmallButton("Add attachment"))
        {
          a.attachments.push_back({"", "tag_stowed_back", 0});
          s.Commit("Add attachment");
        }
        ImGui::SetItemTooltip("A model or a weapon name (\"..._mp\") on a bone tag, e.g. a rifle on tag_stowed_back");
      }

      SectionHeader(Icons::FILM, "Animation");
      AnimCombo(s, a.idleAnim, "Idle");
      ImGui::SetItemTooltip("Plays whenever no clip is active");
      for (std::size_t i = 0; i < a.clips.size(); ++i)
      {
        auto& c = a.clips[i];
        ImGui::PushID(c.id.c_str());
        ImGui::TextUnformatted(AnimLabel(c.anim).c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::SetNextItemWidth(70);
        bool ch2 = DragDouble("##start", &c.start, 0.01f, -1e6, 1e6, "%.2f s");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ch2 |= DragDouble("##dur", &c.duration, 0.01f, 0.05, 600.0, "%.2f s");
        if (ch2)
          s.Commit("Clip timing", true);
        ImGui::SameLine();
        if (ImGui::SmallButton(Icons::TRASH))
        {
          a.clips.erase(a.clips.begin() + static_cast<std::ptrdiff_t>(i));
          s.Commit("Delete clip");
          ImGui::PopID();
          break;
        }
        ImGui::PopID();
      }
      if (ImGui::SmallButton("Add a clip from the animation library"))
        Shown(Panel::Animations) = true;
      ImGui::TextDisabled("Clips play from their start at the game's speed; they cannot be scrubbed.");

      SectionHeader(Icons::ROUTE, "Directions");
      ImGui::TextDisabled("Chain simple directions; \"Apply\" turns them into movement and clips.");
      if (DragDouble("Start at", &a.actionsStart, 0.01f, -1e6, 1e6, "%.2f s"))
        s.Commit("Directions start", true);
      ImGui::SameLine();
      if (ImGui::SmallButton("Playhead"))
      {
        a.actionsStart = s.playhead;
        s.Commit("Directions start");
      }
      for (std::size_t i = 0; i < a.actions.size(); ++i)
      {
        auto& ac = a.actions[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%zu. %s", i + 1, iris::Describe(ac.kind).c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 62);
        if (ImGui::SmallButton("^") && i > 0)
        {
          std::swap(a.actions[i], a.actions[i - 1]);
          s.Commit("Reorder directions");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(Icons::TRASH))
        {
          a.actions.erase(a.actions.begin() + static_cast<std::ptrdiff_t>(i));
          s.Commit("Delete direction");
          ImGui::PopID();
          break;
        }
        ImGui::Indent();
        using K = iris::ActionKind;
        if (ac.kind == K::Wait || ac.kind == K::PlayAnimation || ac.kind == K::Crouch || ac.kind == K::AimAt ||
            ac.kind == K::FollowActor)
          if (DragDouble("Seconds", &ac.duration, 0.01f, 0.05, 600.0, "%.2f"))
            s.Commit("Direction", true);
        if (ac.kind == K::WalkTo || ac.kind == K::RunTo || ac.kind == K::TurnTo || ac.kind == K::AimAt)
        {
          DragVec3("Target", &ac.target, 1.0f, "%.0f");
          if (ImGui::SmallButton("Where the camera looks"))
          {
            ac.target = PlacementPoint(s);
            s.Commit("Direction target");
          }
          if (Pro(s))
          {
            ImGui::SameLine();
            DragDouble("Speed", &ac.speed, 1.0f, 0.0, 1000.0, ac.speed > 0 ? "%.0f u/s" : "default");
          }
        }
        if (ac.kind == K::FaceActor || ac.kind == K::FollowActor)
          ActorCombo(s, ac.targetActor, "Actor", a.id);
        if (ac.kind == K::PlayAnimation || Pro(s))
          AnimCombo(s, ac.anim, ac.kind == K::PlayAnimation ? "Animation" : "Animation override");
        ImGui::Unindent();
        ImGui::PopID();
      }
      if (ImGui::BeginCombo("##adddir", "Add a direction ..."))
      {
        for (int k = 0; k <= static_cast<int>(iris::ActionKind::FollowActor); ++k)
          if (ImGui::Selectable(iris::Describe(static_cast<iris::ActionKind>(k)).c_str()))
          {
            iris::ActorAction ac;
            ac.kind = static_cast<iris::ActionKind>(k);
            ac.target = PlacementPoint(s);
            a.actions.push_back(ac);
            s.Commit("Add direction");
          }
        ImGui::EndCombo();
      }
      ImGui::BeginDisabled(a.actions.empty());
      if (ImGui::Button((std::string(Icons::CHECK) + "  Apply directions").c_str()))
      {
        iris::DirectionContext ctx;
        ctx.roles = iris::FindAnimationRoles(Catalog().animationNames);
        const auto& scene = s.project.scene;
        ctx.actorPosition = [&scene](std::string_view id, double t) -> std::optional<iris::Vec3> {
          if (const auto* o = scene.FindActor(id))
            return iris::EvaluateActor(*o, t).transform.position;
          return std::nullopt;
        };
        const iris::Project& proj = s.project;
        ctx.cameraPosition = [&proj](double t) -> std::optional<iris::Vec3> {
          const auto* cam = proj.CameraAt(t);
          if (!cam || cam->path.Empty())
            return std::nullopt;
          const auto st = cam->path.Evaluate(t);
          return st ? std::optional<iris::Vec3>(st->position) : std::nullopt;
        };
        auto compiled = iris::CompileDirections(a, ctx);
        a.track = std::move(compiled.track);
        a.clips = std::move(compiled.clips);
        if (!compiled.visibility.Empty())
          a.visibility = compiled.visibility;
        for (const auto& w : compiled.warnings)
          Notify(w);
        s.Commit("Apply directions");
        Notify("Directions applied: " + std::to_string(a.clips.size()) + " clip(s) until " +
               std::to_string(static_cast<int>(std::round(compiled.end))) + " s");
      }
      ImGui::EndDisabled();
      ImGui::SetItemTooltip("Replaces this actor's movement keys and clips");

      SectionHeader(Icons::CAMERA, "Shots");
      const auto& types = iris::AllShotTypes();
      ui.shotType = std::clamp(ui.shotType, 0, static_cast<int>(types.size()) - 1);
      if (ImGui::BeginCombo("Shot", iris::ShotLabel(types[static_cast<std::size_t>(ui.shotType)]).c_str()))
      {
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
          if (ImGui::Selectable(iris::ShotLabel(types[static_cast<std::size_t>(i)]).c_str(), i == ui.shotType))
            ui.shotType = i;
        ImGui::EndCombo();
      }
      DragDouble("Duration", &ui.shotDuration, 0.05f, 0.5, 120.0, "%.1f s");
      if (ImGui::Button((std::string(Icons::PLUS) + "  Create shot").c_str()))
      {
        const iris::ActorDef* other = nullptr;
        for (const auto& o : s.project.scene.actors)
          if (o.id != a.id)
          {
            other = &o;
            break;
          }
        const iris::ActorDef copy = a;
        const auto camId = iris::AddActorShot(s.project, copy, types[static_cast<std::size_t>(ui.shotType)], s.playhead,
                                              ui.shotDuration, other);
        s.project.activeCameraId = camId;
        s.selectedCamera = camId;
        s.Commit("Create shot");
        Notify("Shot created and made active");
      }
      if (a.locked)
        ImGui::EndDisabled();
    }

    void PropInspector(Session& s, iris::PropDef& p)
    {
      ImGui::TextColored(CurrentPalette().accent, "%s  Prop", Icons::CUBE);
      if (ImGui::InputText("Name", &p.name))
        s.Commit("Rename prop", true);
      ImGui::TextDisabled("%s", Pro(s) ? p.model.c_str() : iris::ClassifyModel(p.model).category.c_str());
      CommonFlags(s, p);
      if (p.locked)
        ImGui::BeginDisabled();
      SectionHeader(Icons::ARROWS, "Placement");
      if (p.attachActor.empty())
        TransformEditor(s, p.transform, p.track, false, true);
      SectionHeader(Icons::USER, "Attach to an actor");
      ActorCombo(s, p.attachActor, "Actor");
      if (!p.attachActor.empty())
        AttachCombo(s, p.attachTag, "Bone");
      if (p.locked)
        ImGui::EndDisabled();
    }

    void EffectInspector(Session& s, iris::EffectDef& e)
    {
      ImGui::TextColored(CurrentPalette().accent, "%s  Effect", Icons::MAGIC);
      if (ImGui::InputText("Name", &e.name))
        s.Commit("Rename effect", true);
      ImGui::TextDisabled("%s", Pro(s) ? e.fx.c_str() : iris::ClassifyEffect(e.fx).category.c_str());
      if (ImGui::Checkbox("Enabled", &e.enabled))
        s.Commit("Effect enabled");
      bool c = DragDouble("Plays at", &e.start, 0.01f, -1e6, 1e6, "%.2f s");
      ImGui::SameLine();
      if (ImGui::SmallButton("Playhead"))
      {
        e.start = s.playhead;
        c = true;
      }
      c |= DragDouble("Repeat every", &e.repeat, 0.01f, 0.0, 600.0, e.repeat > 0 ? "%.2f s" : "once");
      if (e.repeat > 0)
      {
        int count = e.count;
        if (ImGui::SliderInt("Times", &count, 0, 200, count == 0 ? "until the end" : "%d"))
        {
          e.count = count;
          c = true;
        }
      }
      c |= DragVec3("Position", &e.transform.position, 1.0f, "%.1f");
      iris::Vec3 a{e.transform.angles.pitch, e.transform.angles.yaw, e.transform.angles.roll};
      if (DragVec3("Direction", &a, 0.5f, "%.1f"))
      {
        e.transform.angles = {a.x, a.y, a.z};
        c = true;
      }
      if (ImGui::Button("Move to view target"))
      {
        e.transform.position = PlacementPoint(s);
        c = true;
      }
      ActorCombo(s, e.attachActor, "On actor");
      if (!e.attachActor.empty())
        AttachCombo(s, e.attachTag, "Bone");
      if (c)
        s.Commit("Effect", true);
      ImGui::TextDisabled("Effects play when playback passes them; particles are simulated by the game and cannot be scrubbed.");
    }

    void MaterialEditor(Session& s, iris::MaterialDef& m, const char* id)
    {
      ImGui::PushID(id);
      const auto& presets = iris::MaterialPresets();
      std::string current = m.preset;
      for (const auto& p : presets)
        if (p.id == m.preset)
          current = p.label;
      if (ImGui::BeginCombo("Material", current.c_str()))
      {
        for (const auto& p : presets)
          if (ImGui::Selectable(p.label.c_str(), p.id == m.preset))
          {
            m = p.material;
            s.Commit("Material");
          }
        ImGui::EndCombo();
      }
      bool c = ColorVec3("Colour", &m.color);
      if (Pro(s))
      {
        c |= SliderDouble("Opacity", &m.opacity, 0.0, 1.0, "%.2f");
        c |= SliderDouble("Specular", &m.specular, 0.0, 1.0, "%.2f");
        c |= SliderDouble("Gloss", &m.gloss, 1.0, 256.0, "%.0f");
        c |= SliderDouble("Metallic tint", &m.metallic, 0.0, 1.0, "%.2f");
        c |= ColorVec3("Emission", &m.emission);
        c |= SliderDouble("Emission strength", &m.emissionStrength, 0.0, 4.0, "%.2f");
      }
      if (c)
      {
        m.preset = "custom";
        s.Commit("Material", true);
      }
      ImGui::PopID();
    }

    void GraphicInspector(Session& s, iris::GraphicDef& g)
    {
      auto& ui = Ui();
      ImGui::TextColored(CurrentPalette().accent, "%s  %s", Icons::LAYER,
                         g.kind == iris::GraphicKind::Text    ? "3D text"
                         : g.kind == iris::GraphicKind::Logo  ? "Logo"
                         : g.kind == iris::GraphicKind::Image ? "Image"
                         : g.kind == iris::GraphicKind::Shape ? "Shape"
                         : g.kind == iris::GraphicKind::Model ? "3D model"
                                                              : "Tracking anchor");
      if (ImGui::InputText("Name", &g.name))
        s.Commit("Rename graphic", true);
      const auto gs = Graphics::GetStatus();
      if (const auto it = gs.errors.find(g.id); it != gs.errors.end())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s  %s", Icons::WARNING, it->second.c_str());
      if (const auto it = gs.warnings.find(g.id); it != gs.warnings.end())
        for (const auto& w : it->second)
          ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s  %s", Icons::INFO, w.c_str());
      CommonFlags(s, g);
      if (g.locked)
        ImGui::BeginDisabled();

      bool geo = false;
      if (g.kind == iris::GraphicKind::Text)
      {
        SectionHeader(Icons::FILE, "Text");
        geo |= ImGui::InputTextMultiline("##text", &g.text, ImVec2(-1, ImGui::GetTextLineHeight() * 3));
        if (ImGui::GetTime() - ui.fontsListedAt > 10.0)
        {
          ui.fonts = Graphics::AvailableFonts();
          ui.fontsListedAt = ImGui::GetTime();
        }
        if (const auto f = TakePicked("font"))
        {
          g.font = *f;
          geo = true;
        }
        if (ImGui::BeginCombo("Font", g.font.empty() ? Graphics::DefaultFont().c_str() : std::filesystem::path(
                                                                                              std::u8string(g.font.begin(), g.font.end())).filename().string().c_str(),
                              ImGuiComboFlags_HeightLarge))
        {
          for (const auto& f : ui.fonts)
            if (ImGui::Selectable(f.c_str(), g.font == f))
            {
              g.font = f;
              geo = true;
            }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
          PickFile("font", kFontFilter);
        ImGui::SetItemTooltip("Import a TrueType / OpenType font file");
        auto& ts = g.textSettings;
        geo |= DragDouble("Size", &ts.size, 0.2f, 0.5, 2000.0, "%.1f u");
        geo |= DragDouble("Letter spacing", &ts.letterSpacing, 0.05f, -100.0, 500.0, "%.2f");
        if (Pro(s) || g.text.find('\n') != std::string::npos)
          geo |= DragDouble("Line spacing", &ts.lineSpacing, 0.01f, 0.3, 5.0, "%.2f");
        int align = static_cast<int>(ts.align);
        if (ImGui::Combo("Align", &align, "Left\0Centre\0Right\0"))
        {
          ts.align = static_cast<iris::TextAlign>(align);
          geo = true;
        }
        int layout = static_cast<int>(ts.layout);
        if (ImGui::Combo("Layout", &layout, "Straight\0Vertical\0On a circle\0"))
        {
          ts.layout = static_cast<iris::TextLayoutKind>(layout);
          geo = true;
        }
        if (ts.layout == iris::TextLayoutKind::Circle)
          geo |= DragDouble("Radius", &ts.radius, 0.5f, 5.0, 10000.0, "%.0f u");
        if (Pro(s))
        {
          geo |= ImGui::Checkbox("Kerning", &ts.kerning);
          geo |= ImGui::Checkbox("Animate words, not letters", &g.separateWords);
        }
      }
      if (g.kind == iris::GraphicKind::Logo || g.kind == iris::GraphicKind::Image || g.kind == iris::GraphicKind::Model)
      {
        SectionHeader(Icons::FILE, "Source");
        const char* purpose = g.kind == iris::GraphicKind::Logo ? "logo-src" : g.kind == iris::GraphicKind::Image ? "image-src" : "model-src";
        if (const auto f = TakePicked(purpose))
        {
          g.source = *f;
          geo = true;
        }
        ImGui::SetNextItemWidth(-40);
        geo |= ImGui::InputText("##src", &g.source);
        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
          PickFile(purpose, g.kind == iris::GraphicKind::Logo ? kLogoFilter : g.kind == iris::GraphicKind::Image ? kImageFilter : kModelFilter);
        if (g.kind != iris::GraphicKind::Model)
          geo |= DragDouble("Width", &g.width, 0.5f, 0.5, 20000.0, "%.0f u");
        if (g.kind == iris::GraphicKind::Logo)
        {
          int mode = g.logoMode == iris::LogoMode::Flat ? 1 : 0;
          if (ImGui::Combo("Geometry", &mode, "Extruded 3D\0Flat shapes\0"))
          {
            g.logoMode = mode ? iris::LogoMode::Flat : iris::LogoMode::Extruded;
            geo = true;
          }
          const bool raster = g.source.size() > 4 && g.source.find(".svg") == std::string::npos && g.source.find(".SVG") == std::string::npos;
          if (raster)
          {
            int thr = g.alphaThreshold;
            if (ImGui::SliderInt("Alpha threshold", &thr, 1, 255))
            {
              g.alphaThreshold = thr;
              geo = true;
            }
            geo |= DragDouble("Simplify", &g.simplify, 0.01f, 0.0, 10.0, "%.2f px");
            int sm = g.smoothing;
            if (ImGui::SliderInt("Smoothing", &sm, 0, 4))
            {
              g.smoothing = sm;
              geo = true;
            }
          }
          if (ImGui::Checkbox("Use the artwork's colours", &g.useLogoColors))
            s.Commit("Logo colours");
        }
        if (g.kind == iris::GraphicKind::Model)
        {
          geo |= DragDouble("Scale", &g.modelScale, 0.01f, 0.001, 10000.0, "%.3f");
          int up = g.modelUp == iris::ModelUpAxis::Z ? 1 : 0;
          if (ImGui::Combo("Up axis", &up, "Y (Blender default)\0Z\0"))
          {
            g.modelUp = up ? iris::ModelUpAxis::Z : iris::ModelUpAxis::Y;
            geo = true;
          }
        }
      }
      if (g.kind == iris::GraphicKind::Shape)
      {
        SectionHeader(Icons::CUBE, "Shape");
        int k = static_cast<int>(g.shape);
        if (ImGui::Combo("Kind", &k, "Box\0Cylinder\0Sphere\0Plane\0Ring\0"))
        {
          g.shape = static_cast<iris::ShapeKind>(k);
          geo = true;
        }
        geo |= DragVec3("Size", &g.shapeSize, 0.5f, "%.1f");
      }
      if (g.kind == iris::GraphicKind::Text || (g.kind == iris::GraphicKind::Logo && g.logoMode == iris::LogoMode::Extruded))
      {
        SectionHeader(Icons::LAYER, "Extrusion");
        geo |= DragDouble("Depth", &g.geometry.depth, 0.05f, 0.01, 1000.0, "%.2f u");
        geo |= DragDouble("Bevel", &g.geometry.bevel, 0.01f, 0.0, 100.0, "%.2f u");
        if (Pro(s))
        {
          int seg = g.geometry.bevelSegments;
          if (ImGui::SliderInt("Bevel segments", &seg, 1, 8))
          {
            g.geometry.bevelSegments = seg;
            geo = true;
          }
          geo |= DragDouble("Curve detail", &g.geometry.detail, 0.001f, 0.005, 5.0, "%.3f u");
        }
      }
      if (geo)
        s.Commit("Graphic geometry", true);

      if (g.kind != iris::GraphicKind::Anchor && g.kind != iris::GraphicKind::Image)
      {
        SectionHeader(Icons::PALETTE, "Look");
        if (Pro(s) && g.kind != iris::GraphicKind::Model && g.kind != iris::GraphicKind::Shape)
          if (ImGui::Checkbox("Separate materials for sides and bevels", &g.materials.separate))
            s.Commit("Material parts");
        MaterialEditor(s, g.materials.front, "front");
        if (g.materials.separate)
        {
          ImGui::SeparatorText("Sides");
          MaterialEditor(s, g.materials.side, "side");
          ImGui::SeparatorText("Bevels");
          MaterialEditor(s, g.materials.bevel, "bevel");
          ImGui::SeparatorText("Back");
          MaterialEditor(s, g.materials.back, "back");
        }
        ImGui::TextDisabled("Materials approximate metal / glass looks with specular lighting; not physically based.");
      }

      SectionHeader(Icons::EYE, "Rendering");
      int mode = static_cast<int>(g.renderMode);
      if (ImGui::Combo("Visibility", &mode, "Hidden behind walls and characters\0Always on top\0X-ray (faint where hidden)\0"))
      {
        g.renderMode = static_cast<iris::GraphicRenderMode>(mode);
        s.Commit("Render mode");
      }
      if (g.renderMode != iris::GraphicRenderMode::OnTop && !gs.depthAvailable && !gs.depth.empty())
      {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s  Occlusion unavailable (%s): %s", Icons::WARNING,
                           gs.depth.c_str(), gs.previewWithoutOcclusion ? "previewed on top" : "not drawn");
        if (ImGui::SmallButton("Use \"Always on top\""))
        {
          g.renderMode = iris::GraphicRenderMode::OnTop;
          s.Commit("Render mode");
        }
      }
      if (ImGui::Checkbox("Double-sided", &g.doubleSided))
        s.Commit("Double-sided");

      SectionHeader(Icons::CROSSHAIRS, "Tracking");
      int tm = static_cast<int>(g.tracking.mode);
      if (ImGui::Combo("Follows", &tm, "Fixed in the world\0An actor\0Faces the camera (billboard)\0The camera lens\0"))
      {
        g.tracking.mode = static_cast<iris::TrackingMode>(tm);
        s.Commit("Tracking");
      }
      if (g.tracking.mode == iris::TrackingMode::Actor)
      {
        ActorCombo(s, g.tracking.actor, "Actor");
        AttachCombo(s, g.tracking.attach, "Bone");
        if (ImGui::Checkbox("Rotate with it", &g.tracking.inheritRotation))
          s.Commit("Tracking");
        ImGui::TextDisabled("Position below is the offset from the bone.");
      }
      if (g.tracking.mode == iris::TrackingMode::Billboard)
        if (ImGui::Checkbox("Stay upright", &g.tracking.yawOnly))
          s.Commit("Tracking");
      if (g.tracking.mode == iris::TrackingMode::Camera)
        if (DragDouble("Distance", &g.tracking.distance, 0.5f, 5.0, 10000.0, "%.0f u"))
          s.Commit("Tracking", true);
      if (Pro(s))
      {
        std::string parentName = "(none)";
        if (const auto* pg = s.project.scene.FindGraphic(g.parent))
          parentName = pg->name;
        if (ImGui::BeginCombo("Parent", parentName.c_str()))
        {
          if (ImGui::Selectable("(none)", g.parent.empty()))
          {
            g.parent.clear();
            s.Commit("Parent");
          }
          for (const auto& o : s.project.scene.graphics)
            if (o.id != g.id && o.parent != g.id && ImGui::Selectable((o.name + "##" + o.id).c_str(), g.parent == o.id))
            {
              g.parent = o.id;
              s.Commit("Parent");
            }
          ImGui::EndCombo();
        }
      }

      SectionHeader(Icons::ARROWS, "Transform");
      TransformEditor(s, g.transform, g.track, true, true);

      SectionHeader(Icons::MAGIC, "Animation");
      const auto& presets = iris::AnimationPresets();
      for (std::size_t i = 0; i < g.animations.size(); ++i)
      {
        auto& an = g.animations[i];
        ImGui::PushID(static_cast<int>(i));
        std::string label = an.preset;
        for (const auto& p : presets)
          if (p.id == an.preset)
            label = p.label;
        bool c = ImGui::Checkbox("##en", &an.enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
        if (ImGui::BeginCombo("##preset", label.c_str()))
        {
          for (const auto& p : presets)
            if (ImGui::Selectable(p.label.c_str(), p.id == an.preset))
            {
              an.preset = p.id;
              c = true;
            }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(Icons::TRASH))
        {
          g.animations.erase(g.animations.begin() + static_cast<std::ptrdiff_t>(i));
          s.Commit("Delete animation");
          ImGui::PopID();
          break;
        }
        ImGui::Indent();
        c |= DragDouble("Starts", &an.start, 0.01f, -1e6, 1e6, "%.2f s");
        ImGui::SameLine();
        if (ImGui::SmallButton("Playhead"))
        {
          an.start = s.playhead;
          c = true;
        }
        c |= DragDouble("Duration", &an.duration, 0.01f, 0.01, 600.0, "%.2f s");
        c |= DragDouble("Stagger", &an.stagger, 0.002f, 0.0, 5.0, "%.3f s");
        c |= SliderDouble("Intensity", &an.intensity, 0.0, 4.0, "%.2f");
        if (Pro(s))
        {
          const auto& easings = iris::EasingNames();
          if (ImGui::BeginCombo("Easing", an.easing.c_str()))
          {
            for (const auto& e : easings)
              if (ImGui::Selectable(e.c_str(), e == an.easing))
              {
                an.easing = e;
                c = true;
              }
            ImGui::EndCombo();
          }
          c |= DragVec3("Direction", &an.direction, 0.01f, "%.2f");
          int seed = static_cast<int>(an.seed);
          if (ImGui::DragInt("Seed", &seed, 1.0f, 0, 1 << 30))
          {
            an.seed = static_cast<std::uint32_t>(seed);
            c = true;
          }
        }
        ImGui::Unindent();
        if (c)
          s.Commit("Graphic animation", true);
        ImGui::PopID();
      }
      if (ImGui::BeginCombo("##addanim", "Add an animation ..."))
      {
        for (const auto& p : presets)
          if (ImGui::Selectable(p.label.c_str()))
          {
            iris::GraphicAnimation an;
            an.preset = p.id;
            an.start = s.playhead;
            g.animations.push_back(an);
            s.Commit("Add animation");
          }
        ImGui::EndCombo();
      }
      if (ImGui::Button((std::string(Icons::CAMERA) + "  Generate camera orbit").c_str()))
      {
        const auto b = Graphics::Bounds(g.id).value_or(iris::Bounds3D{{-50, -20, -5}, {50, 20, 5}});
        const auto camId = iris::AddGraphicOrbit(s.project, g, b, s.playhead, 6.0);
        s.project.activeCameraId = camId;
        s.selectedCamera = camId;
        s.Commit("Graphic orbit");
        Notify("Orbit camera created and made active");
      }
      if (g.locked)
        ImGui::EndDisabled();
    }
  }  // namespace

  void DrawObjectInspector(Session& s)
  {
    if (!BeginPanel(Panel::ObjectInspector))
    {
      ImGui::End();
      return;
    }
    auto& sc = s.project.scene;
    if (auto* a = sc.FindActor(s.selectedObject))
      ActorInspector(s, *a);
    else if (auto* p = sc.FindProp(s.selectedObject))
      PropInspector(s, *p);
    else if (auto* e = sc.FindEffect(s.selectedObject))
      EffectInspector(s, *e);
    else if (auto* g = sc.FindGraphic(s.selectedObject))
      GraphicInspector(s, *g);
    else
    {
      ImGui::TextDisabled("Select an actor, prop, effect or graphic: click it in the view or in the Scene panel.");
      ImGui::TextDisabled("G move, R rotate, S size, F focus, Ctrl+D duplicate, Del delete.");
    }
    if (ObjectExists(s, s.selectedObject))
    {
      ImGui::Spacing();
      ImGui::Separator();
      const std::string id = s.selectedObject;
      if (ImGui::Button((std::string(Icons::COPY) + "  Duplicate").c_str()))
        DuplicateObject(s, id);
      ImGui::SameLine();
      if (ImGui::Button((std::string(Icons::TRASH) + "  Delete").c_str()))
        DeleteObject(s, id);
    }
    ImGui::End();
  }

  // ---------------------------------------------------------------- scene panel section

  void DrawSceneObjects(Session& s)
  {
    auto& sc = s.project.scene;
    SectionHeader(Icons::USER, "Scene objects");
    if (sc.Empty())
    {
      ImGui::TextDisabled(Director::InFilmSet() ? "Add characters, props, effects or graphics from their panels."
                                                : "Create a new scene (Director panel) to add characters and props.");
      return;
    }
    std::string pendingDelete, pendingDuplicate;
    auto row = [&](const char* icon, const std::string& id, std::string& name, bool& visible, bool& locked) {
      ImGui::PushID(id.c_str());
      if (ImGui::SmallButton(visible ? Icons::EYE : Icons::EYE_SLASH))
      {
        visible = !visible;
        s.Commit("Visibility");
      }
      ImGui::SameLine();
      if (ImGui::SmallButton(locked ? Icons::LOCK : Icons::UNLOCK))
      {
        locked = !locked;
        s.Commit("Lock");
      }
      ImGui::SameLine();
      if (ImGui::Selectable((std::string(icon) + "  " + name).c_str(), s.selectedObject == id))
        SelectObject(s, id);
      if (ImGui::BeginPopupContextItem("##objctx"))
      {
        ImGui::SetNextItemWidth(180);
        if (ImGui::InputText("##rename", &name))
          s.Commit("Rename", true);
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
          pendingDuplicate = id;
        if (ImGui::MenuItem("Delete", "Del"))
          pendingDelete = id;
        ImGui::EndPopup();
      }
      ImGui::PopID();
    };
    for (auto& a : sc.actors)
      row(Icons::USER, a.id, a.name, a.visible, a.locked);
    for (auto& p : sc.props)
      row(Icons::CUBE, p.id, p.name, p.visible, p.locked);
    for (auto& e : sc.effects)
    {
      bool locked = false;
      row(Icons::MAGIC, e.id, e.name, e.enabled, locked);
    }
    for (auto& g : sc.graphics)
      row(Icons::LAYER, g.id, g.name, g.visible, g.locked);
    if (!pendingDuplicate.empty())
      DuplicateObject(s, pendingDuplicate);
    if (!pendingDelete.empty())
      DeleteObject(s, pendingDelete);
  }

  // ---------------------------------------------------------------- viewport

  namespace
  {
    struct Box
    {
      iris::Bounds3D local;
      iris::Vec3 position;
      iris::Mat3 axis;
      iris::Vec3 scale{1, 1, 1};
      bool graphicSpace = false;  // local is in graphic space (x right, y up, z front)
    };

    iris::Vec3 BoxPoint(const Box& b, const iris::Vec3& l)
    {
      if (b.graphicSpace)
        return iris::GraphicToWorld(l, b.position, b.axis, b.scale);
      return b.position + b.axis * iris::Vec3{l.x * b.scale.x, l.y * b.scale.y, l.z * b.scale.z};
    }

    std::optional<Box> ObjectBox(Session& s, const std::string& id)
    {
      auto& sc = s.project.scene;
      Box b;
      if (const auto* a = sc.FindActor(id))
      {
        const auto st = iris::EvaluateActor(*a, s.playhead);
        b.position = st.transform.position;
        b.axis = iris::AnglesToAxis({0, st.transform.angles.yaw, 0});
        b.local = {{-iris::kActorRadius, -iris::kActorRadius, 0}, {iris::kActorRadius, iris::kActorRadius, iris::kActorHeight}};
        return b;
      }
      if (const auto* p = sc.FindProp(id))
      {
        const auto st = iris::EvaluateProp(*p, s.playhead);
        b.position = st.transform.position;
        b.axis = iris::AnglesToAxis(st.transform.angles);
        b.local = {{-12, -12, 0}, {12, 12, 24}};
        return b;
      }
      if (const auto* e = sc.FindEffect(id))
      {
        b.position = e->transform.position;
        b.local = {{-8, -8, -8}, {8, 8, 8}};
        return b;
      }
      if (sc.FindGraphic(id))
      {
        const auto pose = Graphics::LastPose(id);
        const auto bounds = Graphics::Bounds(id);
        if (!pose)
          return std::nullopt;
        b.position = pose->world.position;
        b.axis = iris::AnglesToAxis(pose->world.angles);
        b.scale = pose->world.scale;
        b.local = bounds.value_or(iris::Bounds3D{{-8, -8, -8}, {8, 8, 8}});
        b.graphicSpace = true;
        return b;
      }
      return std::nullopt;
    }

    // Applies a world transform change to whichever object `id` is.
    void ApplyGizmo(Session& s, const std::string& id, const iris::Transform& t)
    {
      auto& sc = s.project.scene;
      if (auto* a = sc.FindActor(id); a && !a->locked)
        SetTransform(s, a->transform, a->track, t);
      else if (auto* p = sc.FindProp(id); p && !p->locked)
        SetTransform(s, p->transform, p->track, t);
      else if (auto* e = sc.FindEffect(id))
        e->transform = t;
      else if (auto* g = sc.FindGraphic(id); g && !g->locked && g->tracking.mode == iris::TrackingMode::World)
        SetTransform(s, g->transform, g->track, t);
    }

    std::optional<iris::Transform> CurrentTransform(Session& s, const std::string& id)
    {
      auto& sc = s.project.scene;
      if (const auto* a = sc.FindActor(id))
        return a->track.Evaluate(s.playhead, a->transform);
      if (const auto* p = sc.FindProp(id))
        return p->track.Evaluate(s.playhead, p->transform);
      if (const auto* e = sc.FindEffect(id))
        return e->transform;
      if (const auto* g = sc.FindGraphic(id))
        return g->track.Evaluate(s.playhead, g->transform);
      return std::nullopt;
    }

    void DrawGuides(Session& s, ImDrawList* dl, const ImVec2& size, const iris::CameraState& cam)
    {
      const auto& g = s.settings;
      const ImU32 line = IM_COL32(255, 255, 255, 70);
      float x0 = 0, y0 = 0, x1 = size.x, y1 = size.y;
      if (g.guideAspect != 0)
      {
        const float aspects[] = {0.0f, 2.39f, 1.85f, 9.0f / 16.0f, 1.0f, 4.0f / 5.0f};
        const float target = aspects[g.guideAspect];
        const float current = size.x / std::max(size.y, 1.0f);
        const ImU32 mask = IM_COL32(0, 0, 0, 170);
        if (target > current)
        {
          const float h = size.x / target;
          y0 = (size.y - h) * 0.5f;
          y1 = y0 + h;
          dl->AddRectFilled(ImVec2(0, 0), ImVec2(size.x, y0), mask);
          dl->AddRectFilled(ImVec2(0, y1), ImVec2(size.x, size.y), mask);
        }
        else
        {
          const float w = size.y * target;
          x0 = (size.x - w) * 0.5f;
          x1 = x0 + w;
          dl->AddRectFilled(ImVec2(0, 0), ImVec2(x0, size.y), mask);
          dl->AddRectFilled(ImVec2(x1, 0), ImVec2(size.x, size.y), mask);
        }
      }
      const float w = x1 - x0, h = y1 - y0;
      if (g.guideThirds)
        for (int i = 1; i <= 2; ++i)
        {
          dl->AddLine(ImVec2(x0 + w * i / 3.0f, y0), ImVec2(x0 + w * i / 3.0f, y1), line);
          dl->AddLine(ImVec2(x0, y0 + h * i / 3.0f), ImVec2(x1, y0 + h * i / 3.0f), line);
        }
      if (g.guideCentre)
      {
        const ImVec2 c(x0 + w * 0.5f, y0 + h * 0.5f);
        dl->AddLine(ImVec2(c.x - 14, c.y), ImVec2(c.x + 14, c.y), line, 1.5f);
        dl->AddLine(ImVec2(c.x, c.y - 14), ImVec2(c.x, c.y + 14), line, 1.5f);
      }
      if (g.guideSafeAreas)
      {
        dl->AddRect(ImVec2(x0 + w * 0.05f, y0 + h * 0.05f), ImVec2(x1 - w * 0.05f, y1 - h * 0.05f), IM_COL32(120, 200, 255, 90));
        dl->AddRect(ImVec2(x0 + w * 0.1f, y0 + h * 0.1f), ImVec2(x1 - w * 0.1f, y1 - h * 0.1f), IM_COL32(255, 200, 90, 90));
      }
      if (g.guideHorizon)
      {
        // The camera's roll tilts the horizon; level when it is horizontal.
        const double roll = cam.angles.roll * iris::kDegToRad;
        const ImVec2 c(x0 + w * 0.5f, y0 + h * 0.5f);
        const float dx = static_cast<float>(std::cos(roll)) * w * 0.2f, dy = static_cast<float>(std::sin(roll)) * w * 0.2f;
        const bool level = std::abs(cam.angles.roll) < 0.3;
        dl->AddLine(ImVec2(c.x - dx, c.y - dy), ImVec2(c.x + dx, c.y + dy), level ? IM_COL32(120, 230, 140, 200) : IM_COL32(255, 140, 90, 200), 2.0f);
      }
    }
  }  // namespace

  namespace
  {
    // Everything here is in game coordinates (0..display size); the
    // caller maps it onto the shown game picture.
    void DrawSceneOverlayInGameSpace(Session& s, ImDrawList* dl)
  {
    auto& ui = Ui();
    const auto cam = Runtime::LastAppliedCamera();
    if (!cam)
      return;
    const auto& io = ImGui::GetIO();
    const ImVec2 size = io.DisplaySize;
    const ImVec2 mouse = GameMouse();
    DrawGuides(s, dl, size, *cam);
    auto& sc = s.project.scene;
    if (sc.Empty() || Runtime::IsFlying())
      return;
    const iris::ViewParams view = iris::MakeViewParams(*cam, size.x / std::max(size.y, 1.0f));
    auto project = [&](const iris::Vec3& p) -> std::optional<ImVec2> {
      const auto sp = iris::ProjectToScreen(view, p, size.x, size.y);
      if (!sp || std::abs(sp->x) > 1e5 || std::abs(sp->y) > 1e5)
        return std::nullopt;
      return ImVec2(static_cast<float>(sp->x), static_cast<float>(sp->y));
    };
    auto drawBox = [&](const Box& b, ImU32 color, float thickness) {
      iris::Vec3 c[8];
      for (int i = 0; i < 8; ++i)
        c[i] = BoxPoint(b, {(i & 1) ? b.local.max.x : b.local.min.x, (i & 2) ? b.local.max.y : b.local.min.y,
                            (i & 4) ? b.local.max.z : b.local.min.z});
      static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
      for (const auto& e : edges)
      {
        const auto a = project(c[e[0]]), bb = project(c[e[1]]);
        if (a && bb)
          dl->AddLine(*a, *bb, color, thickness);
      }
    };
    const ImU32 accent = ImGui::GetColorU32(CurrentPalette().accent);
    auto all = [&](auto&& fn) {
      for (const auto& a : sc.actors)
        fn(a.id, a.name);
      for (const auto& p : sc.props)
        fn(p.id, p.name);
      for (const auto& e : sc.effects)
        fn(e.id, e.name);
      for (const auto& g : sc.graphics)
        fn(g.id, g.name);
    };
    // Outlines and names; movement paths of actors.
    all([&](const std::string& id, const std::string& name) {
      const auto box = ObjectBox(s, id);
      if (!box)
        return;
      const bool selected = id == s.selectedObject;
      drawBox(*box, selected ? accent : IM_COL32(255, 255, 255, 60), selected ? 2.0f : 1.0f);
      if (const auto top = project(BoxPoint(*box, {0, 0, box->graphicSpace ? box->local.max.y : box->local.max.z})))
        dl->AddText(ImVec2(top->x - ImGui::CalcTextSize(name.c_str()).x * 0.5f, top->y - 18), selected ? accent : IM_COL32(255, 255, 255, 140),
                    name.c_str());
    });
    for (const auto& a : sc.actors)
    {
      if (a.track.Empty() || a.id != s.selectedObject)
        continue;
      const auto times = a.track.KeyTimes();
      std::optional<ImVec2> prev;
      for (double t = times.front(); t <= times.back() + 1e-9; t += std::max((times.back() - times.front()) / 80.0, 0.02))
      {
        const auto p = project(a.track.Evaluate(t, a.transform).position + iris::Vec3{0, 0, 2});
        if (p && prev)
          dl->AddLine(*prev, *p, IM_COL32(120, 200, 255, 180), 2.0f);
        prev = p;
      }
      for (const double t : times)
        if (const auto p = project(a.track.Evaluate(t, a.transform).position + iris::Vec3{0, 0, 2}))
          dl->AddCircleFilled(*p, 4.0f, IM_COL32(120, 200, 255, 255));
    }

    // Gizmo on the selected object.
    const bool overViewport = (!io.WantCaptureMouse && MouseOverGame()) || ui.gizmoDragging;
    const auto current = CurrentTransform(s, s.selectedObject);
    std::optional<ImVec2> origin;
    iris::Vec3 originWorld;
    if (current)
    {
      originWorld = current->position;
      if (const auto* g = sc.FindGraphic(s.selectedObject))
        if (const auto pose = Graphics::LastPose(g->id))
          originWorld = pose->world.position;
      origin = project(originWorld);
    }
    int hoverAxis = -1;
    const iris::Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const ImU32 axisColor[3] = {IM_COL32(235, 80, 80, 255), IM_COL32(110, 210, 90, 255), IM_COL32(90, 150, 255, 255)};
    const double worldLen = origin ? std::max(iris::Distance(cam->position, originWorld) * 0.12, 4.0) : 1.0;
    ImVec2 axisEnd[3];
    if (origin)
    {
      for (auto& e : axisEnd)
        e = *origin;
      const ImVec2 m = mouse;
      if (s.gizmoMode == 0)
      {
        for (int i = 0; i < 3; ++i)
        {
          const auto e = project(originWorld + axes[i] * worldLen);
          if (!e)
            continue;
          axisEnd[i] = *e;
          if (iris::DistanceToSegment2D(m.x, m.y, origin->x, origin->y, e->x, e->y) < 7.0)
            hoverAxis = i;
        }
      }
      else if (s.gizmoMode == 1)
      {
        const float r = std::hypot(m.x - origin->x, m.y - origin->y);
        if (std::abs(r - 56.0f) < 8.0f)
          hoverAxis = 4;
      }
      else if (std::hypot(m.x - origin->x, m.y - origin->y) < 12.0f)
        hoverAxis = 5;
      if (ui.gizmoDragging)
        hoverAxis = ui.gizmoAxis;
      // Draw
      if (s.gizmoMode == 0)
        for (int i = 0; i < 3; ++i)
        {
          const ImU32 c = hoverAxis == i ? IM_COL32(255, 230, 120, 255) : axisColor[i];
          dl->AddLine(*origin, axisEnd[i], c, 3.0f);
          dl->AddCircleFilled(axisEnd[i], 5.0f, c);
        }
      else if (s.gizmoMode == 1)
        dl->AddCircle(*origin, 56.0f, hoverAxis == 4 ? IM_COL32(255, 230, 120, 255) : IM_COL32(90, 150, 255, 220), 64, 3.0f);
      else
        dl->AddRectFilled(ImVec2(origin->x - 7, origin->y - 7), ImVec2(origin->x + 7, origin->y + 7),
                          hoverAxis == 5 ? IM_COL32(255, 230, 120, 255) : IM_COL32(230, 230, 230, 230));
    }

    // Mouse: start / continue / end gizmo drags, otherwise click selection.
    if (ui.gizmoDragging)
    {
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        ui.gizmoDragging = false;
        s.Commit("Transform object");
      }
      else if (current && origin)
      {
        const ImVec2 d(mouse.x - ui.gizmoMouseStart.x, mouse.y - ui.gizmoMouseStart.y);
        iris::Transform t = ui.gizmoStart;
        if (ui.gizmoAxis >= 0 && ui.gizmoAxis < 3)
        {
          // Move along the axis: the mouse motion projected on the axis' screen direction.
          const auto o = project(ui.gizmoStart.position);
          const auto e = project(ui.gizmoStart.position + axes[ui.gizmoAxis] * worldLen);
          if (o && e)
          {
            const float ax = e->x - o->x, ay = e->y - o->y;
            const float len2 = std::max(ax * ax + ay * ay, 1.0f);
            const double amount = (d.x * ax + d.y * ay) / len2 * worldLen;
            t.position = ui.gizmoStart.position + axes[ui.gizmoAxis] * amount;
          }
        }
        else if (ui.gizmoAxis == 4)
          t.angles.yaw = ui.gizmoStart.angles.yaw - d.x * 0.5;
        else if (ui.gizmoAxis == 5)
        {
          const double f = std::exp(-d.y * 0.01);
          t.scale = ui.gizmoStart.scale * f;
        }
        if (io.KeyCtrl)
        {
          // Snapping: 8 units, 15 degrees.
          t.position = {std::round(t.position.x / 8) * 8, std::round(t.position.y / 8) * 8, std::round(t.position.z / 8) * 8};
          t.angles.yaw = std::round(t.angles.yaw / 15) * 15;
        }
        ApplyGizmo(s, s.selectedObject, t);
      }
    }
    else if (overViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ui.viewportClickConsumed)
    {
      if (hoverAxis >= 0 && current)
      {
        ui.gizmoDragging = true;
        ui.gizmoAxis = hoverAxis;
        ui.gizmoMouseStart = mouse;
        ui.gizmoStart = *current;
      }
      else
      {
        // Click selection: the nearest object box under the mouse.
        const iris::Ray ray = iris::ScreenRay(view, mouse.x, mouse.y, size.x, size.y);
        double best = 1e300;
        std::string hit;
        all([&](const std::string& id, const std::string&) {
          const auto box = ObjectBox(s, id);
          if (!box)
            return;
          std::optional<double> d;
          if (box->graphicSpace)
            d = iris::IntersectRayOrientedBox(ray, box->local, box->position, box->axis, box->scale);
          else
          {
            // Our boxes are in object axes (forward, left, up); convert to graphic space order.
            const iris::Bounds3D g{{box->local.min.y, box->local.min.z, box->local.min.x},
                                   {box->local.max.y, box->local.max.z, box->local.max.x}};
            d = iris::IntersectRayOrientedBox(ray, g, box->position, box->axis, box->scale);
          }
          if (d && *d < best)
          {
            best = *d;
            hit = id;
          }
        });
        if (!hit.empty())
          SelectObject(s, hit);
      }
    }
  }
  }  // namespace

  void DrawSceneOverlay(Session& s)
  {
    auto* dl = ImGui::GetBackgroundDrawList();
    const auto space = BeginGameSpace(dl);
    DrawSceneOverlayInGameSpace(s, dl);
    EndGameSpace(dl, space);
  }

  bool HandleObjectShortcuts(Session& s)
  {
    if (!ObjectExists(s, s.selectedObject))
      return false;
    auto& io = ImGui::GetIO();
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    const std::string id = s.selectedObject;
    if (io.KeyCtrl && pressed(ImGuiKey_D))
    {
      DuplicateObject(s, id);
      return true;
    }
    if (io.KeyCtrl)
      return false;
    if (pressed(ImGuiKey_G))
      s.gizmoMode = 0;
    else if (pressed(ImGuiKey_R))
      s.gizmoMode = 1;
    else if (pressed(ImGuiKey_S))
      s.gizmoMode = 2;
    else if (pressed(ImGuiKey_Delete) && s.selectedKeys.empty())
      DeleteObject(s, id);
    else if (pressed(ImGuiKey_Escape))
      s.selectedObject.clear();
    else if (pressed(ImGuiKey_F))
    {
      // Focus: put the free camera in front of the object, looking at it.
      if (const auto box = ObjectBox(s, id))
      {
        const iris::Vec3 centre = BoxPoint(*box, box->local.Center());
        const double radius = std::max(iris::Length(box->local.Size()) * std::max({box->scale.x, box->scale.y, box->scale.z}), 24.0);
        const auto cam = Runtime::LastAppliedCamera();
        iris::Vec3 dir = cam ? iris::Normalize(cam->position - centre) : iris::Vec3{1, 0, 0};
        if (iris::Length(dir) < 0.5)
          dir = {1, 0, 0};
        iris::CameraState st = cam.value_or(iris::CameraState{});
        st.position = centre + dir * (radius * 1.6);
        st.angles = iris::LookAt(st.position, centre);
        s.freeCamera.SetState(st);
        s.freeCameraPlaced = true;
        s.freeView = true;  // look through the navigation camera; the shot is unchanged
      }
    }
    else
      return false;
    return true;
  }
}

namespace IW4Iris::Editor
{
  // The file dialog for other panels (the music file).
  void PickAnyFile(const char* purpose, const wchar_t* filter)
  {
    PickFile(purpose, filter);
  }

  std::optional<std::string> TakePickedFile(const char* purpose)
  {
    return TakePicked(purpose);
  }
}
