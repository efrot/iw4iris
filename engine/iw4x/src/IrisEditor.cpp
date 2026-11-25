// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "EditorInternal.hpp"
#include "Capture.hpp"
#include "Clock.hpp"
#include "DemoControl.hpp"
#include "Director.hpp"
#include "Log.hpp"
#include "OfflineGuard.hpp"
#include "Runtime.hpp"

#include <iris/exporters.hpp>
#include <iris/presets.hpp>
#include <iris/timeline_view.hpp>
#include <iris/version.hpp>

#include <atomic>
#include <thread>

#ifndef IW4IRIS_IW4X_REVISION
#define IW4IRIS_IW4X_REVISION "unknown"
#endif

namespace IW4Iris::Editor
{
  namespace
  {
    UiState ui;
    std::atomic<bool> viewportHovered{false};
    std::atomic<double> wheelFov{0.0};
    ImFont* boldFont = nullptr;
    std::string iniPath;
    bool initialized = false;

    struct WorkspaceDef
    {
      const char* name;
      const char* icon;
      std::vector<Panel> panels;
    };

    const std::vector<WorkspaceDef>& Workspaces()
    {
      static const std::vector<WorkspaceDef> w{
        {"Cinematic", Icons::FILM, {Panel::Outliner, Panel::Demos, Panel::Inspector, Panel::Timeline, Panel::Look}},
        {"Timeline", Icons::CHART, {Panel::Outliner, Panel::Inspector, Panel::Timeline}},
        {"Cameras", Icons::CAMERA, {Panel::Outliner, Panel::Inspector, Panel::Timeline, Panel::Presets}},
        {"Director", Icons::USER, {Panel::Outliner, Panel::Director, Panel::Actors, Panel::ObjectInspector, Panel::Timeline}},
        {"Animation", Icons::ROUTE, {Panel::Outliner, Panel::Actors, Panel::Animations, Panel::ObjectInspector, Panel::Timeline}},
        {"Motion Graphics", Icons::LAYER, {Panel::Outliner, Panel::Graphics, Panel::ObjectInspector, Panel::Timeline}},
        {"Lighting", Icons::LIGHTBULB, {Panel::Outliner, Panel::Lighting, Panel::Look, Panel::Timeline}},
        {"Environment", Icons::SUN, {Panel::Look, Panel::Assets, Panel::ObjectInspector, Panel::Timeline}},
        {"Effects", Icons::MAGIC, {Panel::Look, Panel::Presets, Panel::Timeline}},
        {"Recording", Icons::RECORD, {Panel::Recording, Panel::Timeline, Panel::Export}},
        {"Render Queue", Icons::LIST, {Panel::RenderQueue, Panel::Recording, Panel::Timeline}},
        {"Settings", Icons::COG, {Panel::Settings, Panel::Diagnostics}},
      };
      return w;
    }

  }  // namespace

  const char* PanelName(Panel p)
  {
    switch (p)
    {
    case Panel::Outliner: return "Scene###Outliner";
    case Panel::Inspector: return "Inspector###Inspector";
    case Panel::Timeline: return "Timeline###Timeline";
    case Panel::Look: return "Look###Look";
    case Panel::Demos: return "Demos###Demos";
    case Panel::Recording: return "Recording###Recording";
    case Panel::RenderQueue: return "Render Queue###RenderQueue";
    case Panel::Export: return "Export###Export";
    case Panel::Presets: return "Shot Presets###Presets";
    case Panel::Actors: return "Characters###Actors";
    case Panel::Lighting: return "Lighting###Lighting";
    case Panel::Settings: return "Settings###Settings";
    case Panel::Diagnostics: return "Diagnostics###Diagnostics";
    case Panel::Shortcuts: return "Keyboard Shortcuts###Shortcuts";
    case Panel::GettingStarted: return "Getting Started###GettingStarted";
    case Panel::History: return "History###History";
    case Panel::Director: return "Director###Director";
    case Panel::Animations: return "Animations###Animations";
    case Panel::Assets: return "Props & Effects###Assets";
    case Panel::Graphics: return "Motion Graphics###Graphics";
    case Panel::ObjectInspector: return "Object###ObjectInspector";
    default: return "###unknown";
    }
  }

  bool& Shown(Panel p)
  {
    return ui.show[static_cast<int>(p)];
  }

  namespace
  {
    void ApplyWorkspace(const std::string& name)
    {
      for (const auto& w : Workspaces())
        if (name == w.name)
        {
          for (int i = 0; i < static_cast<int>(Panel::Count); ++i)
            if (static_cast<Panel>(i) != Panel::GettingStarted && static_cast<Panel>(i) != Panel::Shortcuts)
              ui.show[i] = false;
          for (const auto p : w.panels)
            Shown(p) = true;
          ui.workspace = name;
          for (const auto p : w.panels)
            ImGui::SetWindowFocus(PanelName(p));
          return;
        }
    }

    void BuildDefaultLayout(ImGuiID dockspace)
    {
      ImGui::DockBuilderRemoveNode(dockspace);
      ImGui::DockBuilderAddNode(dockspace, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode);
      ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
      ImGuiID center = dockspace;
      const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
      const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
      const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Timeline), bottom);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Outliner), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Demos), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Actors), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Inspector), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Look), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Lighting), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Recording), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::RenderQueue), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Export), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Presets), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Settings), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Diagnostics), right);
      ImGui::DockBuilderDockWindow(PanelName(Panel::History), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Director), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Animations), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Assets), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::Graphics), left);
      ImGui::DockBuilderDockWindow(PanelName(Panel::ObjectInspector), right);
      ImGui::DockBuilderFinish(dockspace);
    }

    double Now()
    {
      return ImGui::GetTime();
    }

    std::string Fmt(double t, const iris::Project& p)
    {
      return iris::FormatTime(t, p.timeline.frameRate, p.timeline.display);
    }

    // ------------------------------------------------------------ menus

    void SaveCurrent(Session& s, const std::string& path)
    {
      s.Post([path] {
        std::string error;
        if (!Runtime::SaveProject(path, &error))
        {
          Log::Error("Save failed: {}", error);
          Notify("Save failed: " + error);
        }
        else
        {
          Notify("Project saved");
        }
      });
    }

    void OpenProject(Session& s, const std::string& path)
    {
      s.Post([path] {
        std::string error;
        if (!Runtime::LoadProject(path, &error))
        {
          Log::Error("Open failed: {}", error);
          Notify("Open failed: " + error);
        }
      });
    }

    void DrawMenuBar(Session& s)
    {
      if (!ImGui::BeginMainMenuBar())
        return;
      ImGui::PushFont(boldFont, 0.0f);
      ImGui::TextColored(CurrentPalette().accent, "IW4Iris");
      ImGui::PopFont();
      ImGui::TextDisabled("Cinematic Studio");
      ImGui::Separator();

      if (ImGui::BeginMenu("File"))
      {
        if (ImGui::MenuItem("New project", "Ctrl+N"))
          s.Post([] { Runtime::NewProject(); });
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
          ui.openPopup = true;
        if (ImGui::BeginMenu("Open recent", !s.settings.recentProjects.empty()))
        {
          for (const auto& r : s.settings.recentProjects)
            if (ImGui::MenuItem(r.c_str()))
              OpenProject(s, r);
          ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
          if (s.projectFile.empty())
            ui.saveAsPopup = true;
          else
            SaveCurrent(s, s.projectFile.string());
        }
        if (ImGui::MenuItem("Save as..."))
          ui.saveAsPopup = true;
        if (ImGui::MenuItem("Save a copy (portable)"))
        {
          auto copy = s.ProjectsDirectory() / (iris::SanitizeFileName(s.project.name) + "-copy.iw4iris");
          auto project = s.project;
          s.Post([copy, project] {
            std::string error;
            if (iris::SaveProjectFile(copy, project, &error))
              Notify("Copy saved to " + copy.string());
            else
              Notify("Copy failed: " + error);
          });
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import IWXMVM / T6Iris campath..."))
        {
          ui.openPath = (s.baseDirectory / "import.json").string();
          ui.openPopup = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Hide editor", "F1"))
          Runtime::ToggleEditor();
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Edit"))
      {
        if (ImGui::MenuItem(("Undo " + s.history.UndoLabel()).c_str(), "Ctrl+Z", false, s.history.CanUndo()))
          s.Undo();
        if (ImGui::MenuItem(("Redo " + s.history.RedoLabel()).c_str(), "Ctrl+Y", false, s.history.CanRedo()))
          s.Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Copy keys", "Ctrl+C", false, !s.selectedKeys.empty()))
          CopySelectedKeys(s);
        if (ImGui::MenuItem("Paste keys at playhead", "Ctrl+V", false, !ui.clipboard.empty()))
          PasteKeys(s);
        if (ImGui::MenuItem("Delete keys", "Del", false, !s.selectedKeys.empty()))
          DeleteSelectedKeys(s);
        ImGui::Separator();
        if (ImGui::MenuItem("Add key from view", "K"))
          s.Post([] { Runtime::AddKeyFromView(); });
        if (ImGui::MenuItem("Add marker", "M"))
          AddMarker(s);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset game look to defaults"))
          s.Post([] {
            Runtime::ResetLook();
            Notify("Every setting IW4Iris changed was restored");
          });
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("View"))
      {
        for (int i = 0; i < static_cast<int>(Panel::Count); ++i)
        {
          const auto p = static_cast<Panel>(i);
          std::string name = PanelName(p);
          name = name.substr(0, name.find("###"));
          ImGui::MenuItem(name.c_str(), nullptr, &ui.show[i]);
        }
        ImGui::Separator();
        ImGui::MenuItem("Camera path overlay", nullptr, &s.settings.showPathOverlay);
        ImGui::MenuItem("Camera frustums", nullptr, &s.settings.showFrustums);
        if (ImGui::BeginMenu("Composition guides"))
        {
          ImGui::MenuItem("Rule of thirds", nullptr, &s.settings.guideThirds);
          ImGui::MenuItem("Centre", nullptr, &s.settings.guideCentre);
          ImGui::MenuItem("Safe areas", nullptr, &s.settings.guideSafeAreas);
          ImGui::MenuItem("Horizon level", nullptr, &s.settings.guideHorizon);
          const char* aspects[] = {"No aspect mask", "2.39:1 letterbox", "1.85:1", "9:16 vertical", "1:1", "4:5"};
          for (int i = 0; i < 6; ++i)
            if (ImGui::MenuItem(aspects[i], nullptr, s.settings.guideAspect == i))
              s.settings.guideAspect = i;
          ImGui::TextDisabled("Guides are never recorded.");
          ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Beginner mode", nullptr, &s.settings.beginnerMode))
          SaveSettings(s);
        if (ImGui::MenuItem("Reset layout"))
          ui.resetLayout = true;
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Workspace"))
      {
        for (const auto& w : Workspaces())
          if (ImGui::MenuItem((std::string(w.icon) + "  " + w.name).c_str(), nullptr, ui.workspace == w.name))
            ApplyWorkspace(w.name);
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Help"))
      {
        ImGui::MenuItem("Getting started", nullptr, &Shown(Panel::GettingStarted));
        ImGui::MenuItem("Keyboard shortcuts", "?", &Shown(Panel::Shortcuts));
        ImGui::MenuItem("Diagnostics", nullptr, &Shown(Panel::Diagnostics));
        ImGui::Separator();
        ImGui::TextDisabled("%s", iris::kVersionString.data());
        ImGui::TextDisabled("IW4x %s", IW4IRIS_IW4X_REVISION);
        ImGui::EndMenu();
      }

      // Workspace tabs on the right of the menu bar
      ImGui::Separator();
      for (const auto& w : Workspaces())
      {
        const bool active = ui.workspace == w.name;
        if (active)
          ImGui::PushStyleColor(ImGuiCol_Text, CurrentPalette().accent);
        if (ImGui::MenuItem((std::string(w.icon) + "##ws" + w.name).c_str()))
          ApplyWorkspace(w.name);
        if (active)
          ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s workspace", w.name);
      }

      // Status on the far right
      const auto guard = OfflineGuard::GetStatus();
      const auto progress = Capture::GetProgress();
      char status[256];
      std::snprintf(status, sizeof(status), "%s %s   %s %.0f FPS   %s", Icons::SHIELD,
                    guard.installed && guard.engineSendBlocked ? "Offline" : "GUARD FAILED", Icons::CLOCK, s.status.fps,
                    Capture::IsActive() ? Capture::ToString(progress.state) : (s.status.demoPlaying ? "Demo" : "Idle"));
      const float w = ImGui::CalcTextSize(status).x + 16.0f;
      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - w);
      ImGui::TextColored(guard.installed ? ImVec4(0.55f, 0.85f, 0.6f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", status);
      ImGui::EndMainMenuBar();
    }

    void DrawFilePopups(Session& s)
    {
      if (ui.openPopup)
      {
        ImGui::OpenPopup("Open project");
        ui.openPopup = false;
        if (ui.openPath.empty())
          ui.openPath = s.ProjectsDirectory().string() + "\\";
      }
      ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal("Open project", nullptr, ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::TextDisabled("A .iw4iris project, or an IWXMVM / T6Iris campath (.json) to import into the selected dolly.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##path", &ui.openPath);
        ImGui::SeparatorText("Projects folder");
        if (ImGui::BeginChild("##files", ImVec2(0, 220), ImGuiChildFlags_Borders))
        {
          std::error_code ec;
          for (std::filesystem::directory_iterator it(s.ProjectsDirectory(), ec), end; it != end; it.increment(ec))
          {
            const auto ext = it->path().extension();
            if (ext != iris::kProjectExtension && ext != ".json")
              continue;
            if (ImGui::Selectable(it->path().filename().string().c_str(), ui.openPath == it->path().string(),
                                  ImGuiSelectableFlags_AllowDoubleClick))
              ui.openPath = it->path().string();
          }
        }
        ImGui::EndChild();
        const bool isJson = std::filesystem::path(ui.openPath).extension() == ".json";
        if (ImGui::Button(isJson ? "Import campath" : "Open", ImVec2(140, 0)))
        {
          if (isJson)
          {
            std::ifstream in(ui.openPath, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string error;
            if (auto path = iris::ImportIwxmvmCampath(text, &error))
            {
              auto& dolly = s.EditTargetDolly();
              path->Shift(s.playhead);
              dolly.path = *path;
              s.Commit("Import campath");
              Notify("Imported " + std::to_string(path->Size()) + " keys into " + dolly.name);
            }
            else
            {
              Notify("Import failed: " + error);
            }
          }
          else
          {
            OpenProject(s, ui.openPath);
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }

      if (ui.saveAsPopup)
      {
        ImGui::OpenPopup("Save project as");
        ui.saveAsPopup = false;
        ui.saveAsPath = (s.projectFile.empty()
                             ? s.ProjectsDirectory() / (iris::SanitizeFileName(s.project.name) + std::string(iris::kProjectExtension))
                             : s.projectFile)
                            .string();
      }
      ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal("Save project as", nullptr, ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::InputText("Name", &s.project.name);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##savepath", &ui.saveAsPath);
        if (ImGui::Button("Save", ImVec2(140, 0)))
        {
          SaveCurrent(s, ui.saveAsPath);
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
    }

    // ------------------------------------------------------------ toolbar

    void DrawToolbar(Session& s)
    {
      const auto* vp = ImGui::GetMainViewport();
      const float height = ImGui::GetFrameHeight() + 14.0f;
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
      ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
      ImGui::SetNextWindowViewport(vp->ID);
      const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 7));
      ImGui::Begin("##toolbar", nullptr, flags);
      ImGui::PopStyleVar();

      auto& p = s.project;
      if (IconButton(Icons::SAVE, "Save (Ctrl+S)"))
      {
        if (s.projectFile.empty())
          ui.saveAsPopup = true;
        else
          SaveCurrent(s, s.projectFile.string());
      }
      ImGui::SameLine();
      if (IconButton(Icons::UNDO, "Undo (Ctrl+Z)") && s.history.CanUndo())
        s.Undo();
      ImGui::SameLine();
      if (IconButton(Icons::REDO, "Redo (Ctrl+Y)") && s.history.CanRedo())
        s.Redo();
      ImGui::SameLine();
      if (IconButton(Icons::SEARCH, "Search every action: type what you want to do (Ctrl+K)"))
        OpenCommandPalette();
      ImGui::SameLine();
      // One place to add anything to the shot.
      if (ImGui::Button((std::string(Icons::PLUS) + " Add").c_str()))
        ImGui::OpenPopup("##add-menu");
      ImGui::SetItemTooltip("Add a camera, a key, a marker, an actor, a prop or effect, 3D text or a logo");
      if (ImGui::BeginPopup("##add-menu"))
      {
        if (ImGui::MenuItem("Camera from the view"))
          CameraFromView(s);
        if (ImGui::MenuItem("Camera key from the view", "K"))
          s.Post([] { Runtime::AddKeyFromView(); });
        if (ImGui::MenuItem("Marker at the playhead", "M"))
          AddMarker(s);
        ImGui::Separator();
        const bool filmSet = Director::InFilmSet();
        if (ImGui::MenuItem(filmSet ? "Actor ..." : "Actor ... (needs a film set)"))
          Shown(filmSet ? Panel::Actors : Panel::Director) = true;
        if (ImGui::MenuItem(filmSet ? "Prop or effect ..." : "Prop or effect ... (needs a film set)"))
          Shown(filmSet ? Panel::Assets : Panel::Director) = true;
        ImGui::Separator();
        if (ImGui::MenuItem("3D text"))
          QuickAddText(s);
        if (ImGui::MenuItem("Logo (SVG / PNG) ..."))
          QuickImportLogo(s);
        if (ImGui::MenuItem("More graphics (image, shape, model, title templates) ..."))
          Shown(Panel::Graphics) = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Shot preset (orbit, reveal, push-in ...) ..."))
          Shown(Panel::Presets) = true;
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();

      if (IconButton(Icons::STEP_BACKWARD, "Previous frame (Left)"))
        StepFrames(s, -1);
      ImGui::SameLine();
      if (IconButton(s.playing ? Icons::PAUSE : Icons::PLAY, "Play / pause (Space)", s.playing))
        TogglePlay(s);
      ImGui::SameLine();
      if (IconButton(Icons::STEP_FORWARD, "Next frame (Right)"))
        StepFrames(s, 1);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90);
      float rate = static_cast<float>(s.playbackRate);
      if (ImGui::SliderFloat("##rate", &rate, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
        s.playbackRate = rate;
      ImGui::SetItemTooltip("Preview speed (multiplies the project's playback-speed curve)");
      ImGui::SameLine();
      ImGui::PushFont(boldFont, 0.0f);
      ImGui::Text("%s", Fmt(s.playhead, p).c_str());
      ImGui::PopFont();
      if (s.status.seeking)
      {
        ImGui::SameLine();
        ImGui::TextColored(CurrentPalette().accent, "%s seeking", Icons::SYNC);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();

      // Active camera
      ImGui::SetNextItemWidth(170);
      const auto* active = p.ActiveCamera();
      if (ImGui::BeginCombo("##camera", active ? active->name.c_str() : "No camera"))
      {
        for (auto& c : p.cameras)
          if (ImGui::Selectable(c.name.c_str(), c.id == p.activeCameraId))
          {
            p.activeCameraId = c.id;
            s.selectedCamera = c.id;
            s.Commit("Change active camera");
          }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("Active camera (C cycles)");
      ImGui::SameLine();
      if (IconButton(Icons::KEY, "Add a camera key from the current view (K)"))
        s.Post([] { Runtime::AddKeyFromView(); });
      ImGui::SameLine();
      if (IconButton(Icons::CROSSHAIRS, "Fly the free camera (F, or hold the right mouse button)", s.flying))
        s.flying = !s.flying;
      ImGui::SameLine();
      // What the viewport shows: the navigation camera or the shot.
      if (s.freeView && !Capture::IsActive())
      {
        ImGui::PushStyleColor(ImGuiCol_Button, CurrentPalette().accentSoft);
        if (ImGui::Button(" Free view: back to the shot (V) "))
          ShowShot(s);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("You are looking through the navigation camera. The shot cameras are unchanged.\n"
                              "Click (or press V) to look through the shot camera again.");
        ImGui::SameLine();
        if (IconButton(Icons::PLUS, "Create a camera from this view (it becomes the active camera)"))
          CameraFromView(s);
        ImGui::SameLine();
        if (IconButton(Icons::SYNC, "Move the selected camera here (a dolly camera gets a key at the playhead)"))
          MoveCameraToView(s);
      }
      else
      {
        ImGui::BeginDisabled(Capture::IsActive());
        if (ImGui::Button(" Shot "))
          s.freeView = true;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("You are looking through the shot camera. Click (or press V) for the free view, to look "
                              "around without changing the shot.");
      }
      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();
      const bool recording = Capture::IsActive();
      if (recording)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
      if (IconButton(Icons::RECORD, recording ? "Cancel the recording" : "Record (opens the Recording panel)", recording))
      {
        if (recording)
          s.Post([] { Capture::Cancel(); });
        else
        {
          Shown(Panel::Recording) = true;
          ImGui::SetWindowFocus(PanelName(Panel::Recording));
        }
      }
      if (recording)
      {
        ImGui::PopStyleColor();
        const auto pr = Capture::GetProgress();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        const float f = pr.framesTotal > 0 ? static_cast<float>(pr.framesCaptured) / static_cast<float>(pr.framesTotal) : 0.0f;
        char label[64];
        std::snprintf(label, sizeof(label), "%lld / %lld", static_cast<long long>(pr.framesCaptured),
                      static_cast<long long>(pr.framesTotal));
        ImGui::ProgressBar(f, ImVec2(160, 0), label);
      }

      // Right side: demo and map
      std::string right = s.status.demoPlaying ? (std::string(Icons::FILM) + "  " + s.status.demoName)
                                               : (s.status.cgameReady ? std::string(Icons::MAP) + "  " + s.status.mapName
                                                                      : std::string("Load a demo or a map"));
      const float w = ImGui::CalcTextSize(right.c_str()).x + 20.0f;
      ImGui::SameLine(ImGui::GetWindowWidth() - w);
      ImGui::TextDisabled("%s", right.c_str());
      ImGui::End();
    }

    // ------------------------------------------------------------ outliner

    void DrawOutliner(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Outliner), &Shown(Panel::Outliner)))
      {
        ImGui::End();
        return;
      }
      auto& p = s.project;
      SectionHeader(Icons::CAMERA, "Cameras");
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
      if (ImGui::SmallButton(Icons::PLUS))
        ImGui::OpenPopup("add-camera");
      ImGui::SetItemTooltip("Add camera");
      if (ImGui::BeginPopup("add-camera"))
      {
        for (auto mode : {iris::CameraMode::Dolly, iris::CameraMode::Free, iris::CameraMode::Orbit, iris::CameraMode::Follow,
                          iris::CameraMode::Static, iris::CameraMode::ThirdPerson, iris::CameraMode::FirstPerson})
        {
          if (ImGui::MenuItem(std::string(iris::ToString(mode)).c_str()))
          {
            iris::CameraDef c;
            c.id = iris::MakeUniqueId(p, "cam");
            c.mode = mode;
            c.name = std::string(iris::ToString(mode)) + " " + c.id.substr(4);
            if (const auto view = Runtime::LastAppliedCamera())
            {
              c.position = view->position;
              c.angles = view->angles;
              c.horizontalFov = view->horizontalFov;
            }
            p.cameras.push_back(c);
            s.selectedCamera = c.id;
            s.Commit("Add camera");
          }
        }
        ImGui::EndPopup();
      }

      for (std::size_t i = 0; i < p.cameras.size(); ++i)
      {
        auto& c = p.cameras[i];
        ImGui::PushID(c.id.c_str());
        const bool active = c.id == p.activeCameraId;
        const bool selected = c.id == s.selectedCamera;
        const ImU32 color = ParseColor(c.color);
        auto* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(pos.x, pos.y + 3), ImVec2(pos.x + 3, pos.y + ImGui::GetTextLineHeight() + 1), color, 1.0f);
        ImGui::Indent(8);
        std::string label = c.name + (c.mode == iris::CameraMode::Dolly ? "  (" + std::to_string(c.path.Size()) + " keys)" : "");
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
          s.selectedCamera = c.id;
          s.selectedKeys.clear();
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          {
            p.activeCameraId = c.id;
            s.Commit("Change active camera");
          }
        }
        if (active)
        {
          ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 40);
          ImGui::TextColored(CurrentPalette().accent, "%s", Icons::VIDEO);
          ImGui::SetItemTooltip("Active camera");
        }
        ImGui::Unindent(8);
        if (ImGui::BeginPopupContextItem())
        {
          if (ImGui::MenuItem("Make active"))
          {
            p.activeCameraId = c.id;
            s.Commit("Change active camera");
          }
          if (ImGui::MenuItem("Duplicate"))
          {
            iris::CameraDef copy = c;
            copy.id = iris::MakeUniqueId(p, "cam");
            copy.name = c.name + " copy";
            p.cameras.insert(p.cameras.begin() + static_cast<std::ptrdiff_t>(i) + 1, copy);
            s.Commit("Duplicate camera");
            ImGui::EndPopup();
            ImGui::PopID();
            break;
          }
          if (ImGui::MenuItem("Add cut here (at the playhead)"))
          {
            p.cuts.AddCut({s.playhead, c.id});
            p.useCameraCuts = true;
            s.Commit("Add camera cut");
          }
          ImGui::BeginDisabled(p.cameras.size() <= 1);
          if (ImGui::MenuItem("Delete"))
          {
            const std::string id = c.id;
            p.cameras.erase(p.cameras.begin() + static_cast<std::ptrdiff_t>(i));
            std::vector<iris::CameraCut> cuts;
            for (const auto& cut : p.cuts.Cuts())
              if (cut.cameraId != id)
                cuts.push_back(cut);
            p.cuts.SetCuts(cuts);
            if (p.activeCameraId == id)
              p.activeCameraId = p.cameras.front().id;
            s.Commit("Delete camera");
            ImGui::EndDisabled();
            ImGui::EndPopup();
            ImGui::PopID();
            break;
          }
          ImGui::EndDisabled();
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }

      ImGui::Spacing();
      if (ImGui::Checkbox("Use camera cuts", &p.useCameraCuts))
        s.Commit("Toggle camera cuts");
      ImGui::SetItemTooltip("Switch between cameras over time with the cuts on the timeline's Cuts track");

      ImGui::Spacing();
      SectionHeader(Icons::FLAG, "Markers");
      for (std::size_t i = 0; i < p.markers.size(); ++i)
      {
        auto& m = p.markers[i];
        ImGui::PushID(m.id.c_str());
        if (ImGui::Selectable((Fmt(m.time, p) + "  " + (m.name.empty() ? m.kind : m.name)).c_str()))
          RequestSeek(s, m.time);
        if (ImGui::BeginPopupContextItem())
        {
          ImGui::InputText("Name", &m.name);
          if (ImGui::BeginCombo("Kind", m.kind.c_str()))
          {
            for (const char* k : {"note", "kill", "trickshot", "cut", "music"})
              if (ImGui::Selectable(k, m.kind == k))
                m.kind = k;
            ImGui::EndCombo();
          }
          if (ImGui::IsItemDeactivatedAfterEdit())
            s.Commit("Edit marker");
          if (ImGui::MenuItem("Delete marker"))
          {
            p.markers.erase(p.markers.begin() + static_cast<std::ptrdiff_t>(i));
            s.Commit("Delete marker");
            ImGui::EndPopup();
            ImGui::PopID();
            break;
          }
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
      if (p.markers.empty())
        ImGui::TextDisabled("No markers. Press M to mark a kill or trickshot.");

      ImGui::Spacing();
      SectionHeader(Icons::CHART, "Property tracks");
      for (auto& t : p.tracks)
      {
        if (ImGui::Selectable((t.property + "  (" + std::to_string(t.curve.Size()) + ")").c_str(), s.selectedTrack == t.property))
          s.selectedTrack = t.property;
      }
      if (p.tracks.empty())
        ImGui::TextDisabled("Key a slider in the Look panel to animate it.");

      ImGui::Spacing();
      DrawSceneObjects(s);
      ImGui::End();
    }

    // ------------------------------------------------------------ inspector

    void KeyEditor(Session& s, iris::CameraDef& cam)
    {
      auto& keys = cam.path.MutableKeys();
      if (s.selectedKeys.size() != 1 || *s.selectedKeys.begin() >= keys.size())
      {
        ImGui::TextDisabled(s.selectedKeys.empty() ? "Select a key on the timeline to edit it."
                                                   : "%zu keys selected.", s.selectedKeys.size());
        return;
      }
      const std::size_t index = *s.selectedKeys.begin();
      auto& k = keys[index];
      bool changed = false;
      double time = k.time;
      if (DragDouble("Time", &time, 0.01f, -3600.0, 36000.0, "%.3f s"))
      {
        const auto newIndex = cam.path.MoveKey(index, s.project.timeline.snapToFrames ? s.project.timeline.frameRate.SnapSeconds(time) : time);
        s.selectedKeys = {newIndex};
        s.Commit("Move key", true);
        return;
      }
      changed |= DragVec3("Position", &k.position, 1.0f);
      iris::Vec3 ang{k.angles.pitch, k.angles.yaw, k.angles.roll};
      if (DragVec3("Pitch / yaw / roll", &ang, 0.25f))
      {
        k.angles = {ang.x, ang.y, ang.z};
        changed = true;
      }
      changed |= SliderDouble("Field of view", &k.horizontalFov, 1.0, 170.0, "%.1f deg");
      changed |= DragDouble("Focus distance", &k.focusDistance, 1.0f, 0.0, 100000.0, "%.0f u");
      changed |= SliderDouble("Aperture", &k.fNumber, 0.7, 32.0, "f/%.1f");
      int interp = static_cast<int>(k.interpolation);
      const char* interps[] = {"Hold", "Linear", "Smooth (auto)", "Catmull-Rom", "Bezier"};
      if (ImGui::Combo("Interpolation", &interp, interps, 5))
      {
        k.interpolation = static_cast<iris::Interpolation>(interp);
        changed = true;
      }
      changed |= SliderDouble("Ease in", &k.ease.in, 0.0, 1.0, "%.2f");
      changed |= SliderDouble("Ease out", &k.ease.out, 0.0, 1.0, "%.2f");
      if (k.interpolation == iris::Interpolation::Smooth)
        changed |= SliderDouble("Tension", &k.tension, 0.0, 1.0, "%.2f");
      if (k.interpolation == iris::Interpolation::Bezier)
      {
        changed |= DragVec3("In tangent", &k.inTangent, 0.5f);
        changed |= DragVec3("Out tangent", &k.outTangent, 0.5f);
      }
      if (changed)
      {
        cam.path.InvalidateCache();
        s.Commit("Edit key", true);
      }
      if (ImGui::Button(Icons::CROSSHAIRS))
      {
        if (const auto view = Runtime::LastAppliedCamera())
        {
          k.position = view->position;
          k.angles = view->angles;
          k.horizontalFov = view->horizontalFov;
          cam.path.InvalidateCache();
          s.Commit("Update key from view");
        }
      }
      ImGui::SetItemTooltip("Update the key from the current view (U)");
      ImGui::SameLine();
      if (ImGui::Button("Go to key"))
        RequestSeek(s, k.time);
      ImGui::SameLine();
      if (ImGui::Button("Hold here"))
      {
        k.interpolation = iris::Interpolation::Hold;
        cam.path.InvalidateCache();
        s.Commit("Hold key");
      }
    }

    void TargetEditor(Session& s, iris::CameraDef& cam)
    {
      int kind = static_cast<int>(cam.target.kind);
      const char* kinds[] = {"None", "World point", "Player", "Entity"};
      bool changed = ImGui::Combo("Target", &kind, kinds, 4);
      cam.target.kind = static_cast<iris::TargetKind>(kind);
      if (cam.target.kind == iris::TargetKind::Point)
      {
        changed |= DragVec3("Point", &cam.target.point, 1.0f);
        if (ImGui::SmallButton("Point at the view's focus"))
          if (const auto view = Runtime::LastAppliedCamera())
          {
            cam.target.point = view->position + iris::AnglesToForward(view->angles) * cam.lens.focusDistance;
            changed = true;
          }
      }
      else if (cam.target.kind == iris::TargetKind::Player)
      {
        std::string current = "client " + std::to_string(cam.target.index);
        for (const auto& [num, name] : s.status.players)
          if (num == cam.target.index)
            current = name;
        if (ImGui::BeginCombo("Player", current.c_str()))
        {
          for (const auto& [num, name] : s.status.players)
            if (ImGui::Selectable(name.c_str(), num == cam.target.index))
            {
              cam.target.index = num;
              changed = true;
            }
          ImGui::EndCombo();
        }
      }
      else if (cam.target.kind == iris::TargetKind::Entity)
      {
        changed |= ImGui::InputInt("Entity number", &cam.target.index);
      }
      if (cam.target.kind != iris::TargetKind::None)
        changed |= DragVec3("Offset", &cam.target.offset, 0.5f);
      if (changed)
        s.Commit("Edit target", true);
    }

    void DrawInspector(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Inspector), &Shown(Panel::Inspector)))
      {
        ImGui::End();
        return;
      }
      auto* camPtr = s.SelectedCamera();
      if (!camPtr)
      {
        ImGui::TextDisabled("No camera selected.");
        ImGui::End();
        return;
      }
      auto& cam = *camPtr;
      auto& p = s.project;
      ImGui::PushFont(boldFont, 0.0f);
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ParseColor(cam.color)), "%s", Icons::CAMERA);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputText("##name", &cam.name))
        s.Commit("Rename camera", true);
      ImGui::PopFont();

      int mode = static_cast<int>(cam.mode);
      const char* modes[] = {"First person (game)", "Third person (game)", "Free", "Dolly (keyframed)", "Orbit", "Follow", "Static (tripod)"};
      if (ImGui::Combo("Mode", &mode, modes, 7))
      {
        cam.mode = static_cast<iris::CameraMode>(mode);
        s.Commit("Change camera mode");
      }
      if (cam.id != p.activeCameraId)
      {
        if (ImGui::Button("Make active camera"))
        {
          p.activeCameraId = cam.id;
          s.Commit("Change active camera");
        }
      }

      if (cam.mode == iris::CameraMode::FirstPerson || cam.mode == iris::CameraMode::ThirdPerson)
      {
        ImGui::TextWrapped("Shows the game's own view. Use a Free, Dolly or Orbit camera for cinematic shots.");
        if (cam.mode == iris::CameraMode::ThirdPerson)
        {
          bool c = DragDouble("Range", &cam.orbitDistance, 1.0f, 10.0, 2000.0, "%.0f u");
          c |= DragDouble("Angle", &cam.orbitStartAngle, 1.0f, -360.0, 360.0, "%.0f deg");
          if (c)
            s.Commit("Edit third person", true);
        }
        ImGui::End();
        return;
      }

      // Lens
      if (ImGui::CollapsingHeader((std::string(Icons::CROSSHAIRS) + "  Lens").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      {
        bool c = false;
        c |= ImGui::Checkbox("Field of view from lens", &cam.useLensFov);
        if (cam.useLensFov)
        {
          c |= SliderDouble("Focal length", &cam.lens.focalLengthMm, 8.0, 300.0, "%.0f mm");
          ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
          for (double f : iris::FocalLengthPresets())
          {
            if (f > 200)
              break;
            char label[16];
            std::snprintf(label, sizeof(label), "%.0f", f);
            if (ImGui::SmallButton(label))
            {
              cam.lens.focalLengthMm = f;
              c = true;
            }
            ImGui::SameLine();
          }
          ImGui::PopStyleVar();
          ImGui::NewLine();
          if (ImGui::BeginCombo("Sensor", cam.lens.sensor.name.c_str()))
          {
            for (const auto& sensor : iris::SensorPresets())
              if (ImGui::Selectable(sensor.name.c_str(), sensor.name == cam.lens.sensor.name))
              {
                cam.lens.sensor = sensor;
                c = true;
              }
            ImGui::EndCombo();
          }
          c |= SliderDouble("Anamorphic squeeze", &cam.lens.anamorphicSqueeze, 1.0, 2.0, "%.2fx");
          ImGui::TextDisabled("Horizontal FOV %.1f deg", cam.lens.HorizontalFov());
        }
        else
        {
          c |= SliderDouble("Field of view", &cam.horizontalFov, 1.0, 170.0, "%.1f deg");
          ImGui::TextDisabled("= %.1f mm on Super 35", iris::HorizontalFovToFocalLength(cam.horizontalFov, 24.89));
        }
        c |= SliderDouble("Aperture", &cam.lens.fNumber, 0.7, 32.0, "f/%.1f");
        c |= DragDouble("Focus distance", &cam.lens.focusDistance, 1.0f, 1.0, 100000.0, "%.0f u");
        const double focal = cam.useLensFov ? cam.lens.focalLengthMm : iris::HorizontalFovToFocalLength(cam.horizontalFov, 36.0);
        const auto dof = iris::ComputeDepthOfField(focal, cam.lens.fNumber, cam.lens.focusDistance);
        ImGui::TextDisabled("Sharp from %.0f to %s u (hyperfocal %.0f u)", dof.nearLimit,
                            dof.farIsInfinite ? "infinity" : std::to_string(static_cast<int>(dof.farLimit)).c_str(), dof.hyperfocal);
        HelpMarker("Thin-lens depth of field for a full-frame circle of confusion, 1 unit = 1 inch. The game renders it "
                   "with its own near/far blur (Look > Depth of field > From lens), which approximates these limits.");
        if (c)
          s.Commit("Edit lens", true);
      }

      if (cam.mode == iris::CameraMode::Dolly)
      {
        if (ImGui::CollapsingHeader((std::string(Icons::ROUTE) + "  Path").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
          bool constant = cam.path.IsConstantSpeed();
          if (ImGui::Checkbox("Constant speed", &constant))
          {
            cam.path.SetConstantSpeed(constant);
            s.Commit("Constant speed");
          }
          HelpMarker("Moves along the path at a uniform speed between the first and the last key; the first key's ease "
                     "out and the last key's ease in still soften the start and the end.");
          int rot = cam.path.GetRotationMode() == iris::RotationMode::Euler ? 1 : 0;
          if (ImGui::Combo("Rotation", &rot, "Quaternion (shortest arc)\0Euler (allows spins)\0"))
          {
            if (rot == 1)
              cam.path.UnwrapAngles();
            cam.path.SetRotationMode(rot == 1 ? iris::RotationMode::Euler : iris::RotationMode::Quaternion);
            s.Commit("Rotation mode");
          }
          ImGui::TextDisabled("%zu keys, %.0f units long, %s to %s", cam.path.Size(), cam.path.Length(),
                              Fmt(cam.path.StartTime(), p).c_str(), Fmt(cam.path.EndTime(), p).c_str());
          if (ImGui::Button("Reverse"))
          {
            cam.path.Reverse();
            s.Commit("Reverse path");
          }
          ImGui::SameLine();
          if (ImGui::Button("Smooth"))
          {
            cam.path.SmoothPositions(0.5);
            s.Commit("Smooth path");
          }
          ImGui::SameLine();
          if (ImGui::Button("Slower"))
          {
            cam.path.Retime(cam.path.StartTime(), 1.25);
            s.Commit("Retime path");
          }
          ImGui::SameLine();
          if (ImGui::Button("Faster"))
          {
            cam.path.Retime(cam.path.StartTime(), 0.8);
            s.Commit("Retime path");
          }
          if (ImGui::Button("Start path at playhead"))
          {
            cam.path.Shift(s.playhead - cam.path.StartTime());
            s.Commit("Shift path");
          }
          ImGui::SameLine();
          if (ImGui::Button("Clear keys"))
          {
            cam.path.Clear();
            s.selectedKeys.clear();
            s.Commit("Clear keys");
          }
        }
        if (ImGui::CollapsingHeader((std::string(Icons::KEY) + "  Selected key").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          KeyEditor(s, cam);
      }
      else if (cam.mode == iris::CameraMode::Free || cam.mode == iris::CameraMode::Static)
      {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
          if (cam.mode == iris::CameraMode::Free)
          {
            auto state = s.freeCamera.State();
            bool c = DragVec3("Position", &state.position, 1.0f);
            iris::Vec3 a{state.angles.pitch, state.angles.yaw, state.angles.roll};
            if (DragVec3("Pitch / yaw / roll", &a, 0.25f))
            {
              state.angles = {a.x, a.y, a.z};
              c = true;
            }
            c |= SliderDouble("Field of view", &state.horizontalFov, 5.0, 150.0, "%.1f deg");
            if (c)
              s.freeCamera.SetState(state);
            ImGui::TextDisabled("Hold the right mouse button in the view and use WASD, Space / Ctrl, Q / E (roll), "
                                "Shift (fast), Alt (slow), the wheel (zoom).");
          }
          else
          {
            bool c = DragVec3("Position", &cam.position, 1.0f);
            iris::Vec3 a{cam.angles.pitch, cam.angles.yaw, cam.angles.roll};
            if (DragVec3("Pitch / yaw / roll", &a, 0.25f))
            {
              cam.angles = {a.x, a.y, a.z};
              c = true;
            }
            if (ImGui::Button("Set from current view"))
              if (const auto view = Runtime::LastAppliedCamera())
              {
                cam.position = view->position;
                cam.angles = view->angles;
                cam.horizontalFov = view->horizontalFov;
                c = true;
              }
            if (c)
              s.Commit("Edit transform", true);
            TargetEditor(s, cam);
          }
        }
      }
      else if (cam.mode == iris::CameraMode::Orbit || cam.mode == iris::CameraMode::Follow)
      {
        if (ImGui::CollapsingHeader("Rig", ImGuiTreeNodeFlags_DefaultOpen))
        {
          TargetEditor(s, cam);
          bool c = DragDouble("Distance", &cam.orbitDistance, 1.0f, 1.0, 10000.0, "%.0f u");
          c |= DragDouble("Height", &cam.orbitHeight, 1.0f, -5000.0, 5000.0, "%.0f u");
          if (cam.mode == iris::CameraMode::Orbit)
            c |= DragDouble("Speed", &cam.orbitSpeed, 0.5f, -720.0, 720.0, "%.1f deg/s");
          c |= DragDouble("Angle", &cam.orbitStartAngle, 0.5f, -360.0, 360.0, "%.1f deg");
          double roll = cam.angles.roll;
          if (DragDouble("Roll", &roll, 0.25f, -180.0, 180.0, "%.1f deg"))
          {
            cam.angles.roll = roll;
            c = true;
          }
          if (c)
            s.Commit("Edit rig", true);
        }
      }

      if (ImGui::CollapsingHeader((std::string(Icons::SYNC) + "  Handheld shake").c_str()))
      {
        bool c = ImGui::Checkbox("Enabled", &cam.shake.enabled);
        c |= SliderDouble("Position", &cam.shake.amplitudePosition, 0.0, 10.0, "%.2f u");
        c |= SliderDouble("Rotation", &cam.shake.amplitudeRotation, 0.0, 5.0, "%.2f deg");
        c |= SliderDouble("Frequency", &cam.shake.frequency, 0.05, 10.0, "%.2f Hz");
        int seed = static_cast<int>(cam.shake.seed);
        if (ImGui::InputInt("Seed", &seed))
        {
          cam.shake.seed = static_cast<std::uint32_t>(std::max(0, seed));
          c = true;
        }
        HelpMarker("Deterministic noise of the cinematic time: the same frame always shakes the same way.");
        if (c)
          s.Commit("Edit shake", true);
      }
      ImGui::End();
    }

    // ------------------------------------------------------------ look

    bool Available(const Session& s, const char* dvar)
    {
      return s.status.availableDvars.contains(dvar);
    }

    void KeyButton(Session& s, const char* property, double value)
    {
      ImGui::SameLine();
      ImGui::PushID(property);
      const auto* track = s.project.FindTrack(property);
      const bool keyed = track && !track->curve.Empty();
      if (keyed)
        ImGui::PushStyleColor(ImGuiCol_Text, CurrentPalette().accent);
      if (ImGui::SmallButton(Icons::DIAMOND))
      {
        auto* t = s.project.FindTrack(property);
        if (!t)
        {
          iris::PropertyTrack nt;
          nt.property = property;
          nt.curve.SetDefaultValue(value);
          s.project.tracks.push_back(nt);
          t = &s.project.tracks.back();
        }
        iris::ScalarKey k;
        k.time = s.playhead;
        k.value = value;
        t->curve.AddKey(k);
        t->curve.UpdateAutoHandles();
        s.selectedTrack = property;
        s.Commit("Key " + std::string(property));
      }
      if (keyed)
        ImGui::PopStyleColor();
      ImGui::SetItemTooltip("Add a key for this value at the playhead");
      ImGui::PopID();
    }

    void DrawLook(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Look), &Shown(Panel::Look)))
      {
        ImGui::End();
        return;
      }
      auto& v = s.project.visuals;
      bool c = false;
      auto keyed = [&](const char* property, double* value, double min, double max, const char* label, const char* fmt) {
        const double shown = s.project.PropertyValue(property, s.playhead, *value);
        double edit = shown;
        if (SliderDouble(label, &edit, min, max, fmt))
        {
          if (auto* t = s.project.FindTrack(property); t && !t->curve.Empty())
          {
            iris::ScalarKey k;
            k.time = s.playhead;
            k.value = edit;
            t->curve.AddKey(k);
            t->curve.UpdateAutoHandles();
          }
          else
            *value = edit;
          c = true;
        }
        KeyButton(s, property, edit);
      };

      if (ImGui::CollapsingHeader((std::string(Icons::PALETTE) + "  Color (film tweaks)").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      {
        if (!Available(s, "r_filmTweakBrightness"))
          Unavailable("Film tweaks", "the r_filmTweak dvars were not found in this game");
        c |= ImGui::Checkbox("Override##film", &v.filmOverride);
        ImGui::BeginDisabled(!v.filmOverride);
        keyed("film.brightness", &v.filmBrightness, -1.0, 1.0, "Brightness", "%.3f");
        keyed("film.contrast", &v.filmContrast, 0.0, 4.0, "Contrast", "%.3f");
        keyed("film.desaturation", &v.filmDesaturation, 0.0, 1.0, "Desaturation", "%.3f");
        c |= ColorVec3("Light tint", &v.filmLightTint);
        c |= ColorVec3("Dark tint", &v.filmDarkTint);
        c |= ImGui::Checkbox("Invert", &v.filmInvert);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Lift/gamma/gain, curves and LUTs need a post-process pass (planned).");
      }

      if (ImGui::CollapsingHeader((std::string(Icons::CROSSHAIRS) + "  Depth of field").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      {
        if (!Available(s, "r_dof_enable"))
          Unavailable("Depth of field", "the r_dof dvars were not found in this game");
        c |= ImGui::Checkbox("Override##dof", &v.dofOverride);
        ImGui::BeginDisabled(!v.dofOverride);
        c |= ImGui::Checkbox("From the camera's lens", &v.dofFromLens);
        HelpMarker("Places the engine's near and far blur ramps at the lens' limits of acceptable sharpness.");
        ImGui::BeginDisabled(v.dofFromLens);
        c |= DragDouble("Near start", &v.dofNearStart, 1.0f, 0.0, 10000.0, "%.0f");
        c |= DragDouble("Near end", &v.dofNearEnd, 1.0f, 0.0, 10000.0, "%.0f");
        c |= DragDouble("Far start", &v.dofFarStart, 5.0f, 0.0, 100000.0, "%.0f");
        c |= DragDouble("Far end", &v.dofFarEnd, 5.0f, 0.0, 100000.0, "%.0f");
        ImGui::EndDisabled();
        keyed("dof.nearBlur", &v.dofNearBlur, 4.0, 10.0, "Near blur", "%.2f");
        keyed("dof.farBlur", &v.dofFarBlur, 0.0, 10.0, "Far blur", "%.2f");
        c |= SliderDouble("Bias", &v.dofBias, 0.1, 10.0, "%.2f");
        ImGui::EndDisabled();
      }

      if (ImGui::CollapsingHeader((std::string(Icons::SUN) + "  Sun").c_str()))
      {
        if (!Available(s, "r_lightTweakSunLight"))
          Unavailable("Sun", "the r_lightTweakSun dvars were not found in this game");
        c |= ImGui::Checkbox("Override##sun", &v.sunOverride);
        ImGui::BeginDisabled(!v.sunOverride);
        c |= ColorVec3("Color", &v.sunColor);
        keyed("sun.intensity", &v.sunIntensity, 0.0, 8.0, "Intensity", "%.2f");
        c |= DragDouble("Elevation", &v.sunAngles.pitch, 0.25f, -90.0, 90.0, "%.1f deg");
        c |= DragDouble("Direction", &v.sunAngles.yaw, 0.5f, -360.0, 360.0, "%.1f deg");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Experimental: whether the engine re-reads the sun tweaks live is not verified yet.");
      }

      if (ImGui::CollapsingHeader((std::string(Icons::EYE) + "  Fog").c_str()))
      {
        if (!Available(s, "r_fog"))
          Unavailable("Fog", "r_fog was not found in this game");
        c |= ImGui::Checkbox("Override##fog", &v.fogOverride);
        ImGui::BeginDisabled(!v.fogOverride);
        bool fogOn = v.fogOpacity > 0.0;
        if (ImGui::Checkbox("Fog visible", &fogOn))
        {
          v.fogOpacity = fogOn ? 1.0 : 0.0;
          c = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Fog colour and distances are set by map scripts in IW4; only on/off is exposed.");
      }

      if (ImGui::CollapsingHeader((std::string(Icons::LAYER) + "  HUD and overlays").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      {
        c |= ImGui::Checkbox("Hide HUD", &v.hideHud);
        c |= ImGui::Checkbox("Hide the gun in cinematic cameras", &v.hideGunInModCameras);
        c |= ImGui::Checkbox("Hide crosshair", &v.hideCrosshair);
      }

      ImGui::Spacing();
      if (ImGui::Button((std::string(Icons::UNDO) + "  Reset the game's look to defaults").c_str()))
      {
        v = iris::VisualSettings{};
        s.Post([] { Runtime::ResetLook(); });
        c = true;
      }
      ImGui::SetItemTooltip("Restores every dvar IW4Iris changed to the value it had before");
      if (c)
        s.Commit("Edit look", true);
      ImGui::End();
    }

    // ------------------------------------------------------------ demos

    void ScanDemos(Session& s)
    {
      if (ui.demoScanRunning)
        return;
      ui.demoScanRunning = true;
      std::vector<std::filesystem::path> folders;
      for (const auto& f : s.settings.demoFolders)
        folders.emplace_back(f);
      std::thread([folders] {
        auto list = iris::FindDemos(folders, true);
        auto& session = GetSession();
        std::lock_guard _(session.mutex);
        session.demoList = std::move(list);
        Ui().demoScanRunning = false;
      }).detach();
    }

    void DrawDemos(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Demos), &Shown(Panel::Demos)))
      {
        ImGui::End();
        return;
      }
      static bool scanned = false;
      if (!scanned)
      {
        ScanDemos(s);
        scanned = true;
      }
      if (IconButton(Icons::SYNC, "Rescan the demo folders"))
        ScanDemos(s);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 34);
      ImGui::InputTextWithHint("##search", (std::string(Icons::SEARCH) + "  name, map, mode, author").c_str(), &ui.demoSearch);
      ImGui::SameLine();
      if (IconButton(Icons::STAR, "Favourites only", ui.demoFavoritesOnly))
        ui.demoFavoritesOnly = !ui.demoFavoritesOnly;

      if (ImGui::BeginTable("##demos", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                            ImVec2(0, ImGui::GetContentRegionAvail().y - 120)))
      {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 18);
        ImGui::TableSetupColumn("Demo");
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (const auto& d : s.demoList)
        {
          const auto key = d.path.string();
          const bool fav = s.settings.favoriteDemos.contains(key);
          if (ui.demoFavoritesOnly && !fav)
            continue;
          const auto label = s.settings.demoLabels.contains(key) ? s.settings.demoLabels[key] : std::string();
          if (!iris::DemoMatches(d, ui.demoSearch) && label.find(ui.demoSearch) == std::string::npos)
            continue;
          ImGui::PushID(key.c_str());
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          if (ImGui::SmallButton(fav ? Icons::STAR : "-"))
          {
            if (fav)
              s.settings.favoriteDemos.erase(key);
            else
              s.settings.favoriteDemos.insert(key);
            SaveSettings(s);
          }
          ImGui::TableNextColumn();
          const bool loaded = s.loadedDemoPath && *s.loadedDemoPath == d.path;
          if (ImGui::Selectable(d.name.c_str(), loaded, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
              ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          {
            const auto path = d.path;
            s.Post([path] {
              std::string error;
              if (!DemoControl::Play(path, &error))
                Notify(error);
            });
          }
          if (ImGui::BeginPopupContextItem())
          {
            std::string edit = label;
            if (ImGui::InputText("Label", &edit))
            {
              s.settings.demoLabels[key] = edit;
              SaveSettings(s);
            }
            ImGui::TextDisabled("%s", key.c_str());
            ImGui::EndPopup();
          }
          ImGui::SetItemTooltip("%s\n%s", key.c_str(), d.meta ? ("by " + d.meta->author).c_str() : "no IW4x metadata");
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(d.meta ? d.meta->mapName.c_str() : "?");
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(d.meta ? d.meta->gametype.c_str() : "");
          ImGui::TableNextColumn();
          if (d.meta && d.meta->lengthMs > 0)
            ImGui::Text("%d:%02d", static_cast<int>(d.meta->lengthMs / 60000), static_cast<int>(d.meta->lengthMs / 1000 % 60));
          ImGui::TableNextColumn();
          ImGui::TextDisabled("%s", label.c_str());
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if (ui.demoScanRunning)
        ImGui::TextDisabled("Scanning...");
      else
        ImGui::TextDisabled("%zu demo(s). Double-click to play; the file is copied, never modified.", s.demoList.size());

      ImGui::SeparatorText("Demo folders");
      for (std::size_t i = 0; i < s.settings.demoFolders.size(); ++i)
      {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(Icons::TIMES))
        {
          s.settings.demoFolders.erase(s.settings.demoFolders.begin() + static_cast<std::ptrdiff_t>(i));
          SaveSettings(s);
          ImGui::PopID();
          break;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(s.settings.demoFolders[i].c_str());
        ImGui::PopID();
      }
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60);
      ImGui::InputTextWithHint("##newfolder", "Add a folder (paste a path)", &ui.newDemoFolder);
      ImGui::SameLine();
      if (ImGui::Button("Add") && !ui.newDemoFolder.empty())
      {
        s.settings.demoFolders.push_back(ui.newDemoFolder);
        ui.newDemoFolder.clear();
        SaveSettings(s);
        ScanDemos(s);
      }
      ImGui::End();
    }

    // ------------------------------------------------------------ recording

    void RecordingSettingsEditor(Session& s, iris::RecordingSettings& r)
    {
      bool c = false;
      const iris::OutputFormat formats[] = {iris::OutputFormat::PngSequence, iris::OutputFormat::JpegSequence,
                                            iris::OutputFormat::H264, iris::OutputFormat::H265,
                                            iris::OutputFormat::ProRes422, iris::OutputFormat::ProRes422HQ,
                                            iris::OutputFormat::ProRes4444, iris::OutputFormat::ProRes4444XQ,
                                            iris::OutputFormat::Ffv1Lossless};
      if (ImGui::BeginCombo("Format", iris::DisplayName(r.format).data()))
      {
        for (auto f : formats)
          if (ImGui::Selectable(iris::DisplayName(f).data(), f == r.format))
          {
            r.format = f;
            c = true;
          }
        ImGui::EndCombo();
      }
      if (iris::IsImageSequence(r.format))
        c |= ImGui::Checkbox("Encode with FFmpeg instead of the built-in writer", &r.useFfmpeg);

      if (ImGui::BeginCombo("Frame rate", r.frameRate.ToString().c_str()))
      {
        for (const auto& fr : iris::StandardFrameRates())
          if (ImGui::Selectable(fr.ToString().c_str(), fr == r.frameRate))
          {
            r.frameRate = fr;
            c = true;
          }
        ImGui::EndCombo();
      }
      double fps = r.frameRate.Fps();
      if (DragDouble("Custom FPS", &fps, 0.5f, 1.0, 1000.0, "%.3f"))
      {
        r.frameRate = iris::FrameRate::FromFps(fps);
        c = true;
      }

      ImGui::Text("Resolution: %d x %d (game)", s.status.renderWidth, s.status.renderHeight);
      HelpMarker("Frames are read from the game's back buffer, so recordings use the game resolution (r_mode). "
                 "Off-screen rendering above the monitor resolution is being researched.");

      int range = static_cast<int>(r.range);
      if (ImGui::Combo("Range", &range, "In / out points\0Whole timeline\0Custom\0"))
      {
        r.range = static_cast<iris::RangeMode>(range);
        c = true;
      }
      if (r.range == iris::RangeMode::Custom)
      {
        c |= DragDouble("Start", &r.customStart, 0.01f, -3600.0, 36000.0, "%.3f s");
        c |= DragDouble("End", &r.customEnd, 0.01f, -3600.0, 36000.0, "%.3f s");
      }
      if (r.format == iris::OutputFormat::H264 || r.format == iris::OutputFormat::H265)
      {
        c |= ImGui::SliderInt("CRF", &r.crf, 0, 51);
        c |= ImGui::InputText("Preset", &r.preset);
        c |= ImGui::Checkbox("4:4:4 chroma", &r.chroma444);
      }
      if (r.format == iris::OutputFormat::JpegSequence)
        c |= ImGui::SliderInt("JPEG quality", &r.jpegQuality, 1, 100);
      c |= ImGui::InputTextWithHint("Output folder", s.RecordingsDirectory().string().c_str(), &r.outputDirectory);
      c |= ImGui::InputText("Name pattern", &r.namePattern);
      HelpMarker("{project} {camera} {pass} {date} {fps} {w} {h}");

      if (ImGui::TreeNode("Render passes"))
      {
        for (auto kind : {iris::PassKind::Beauty, iris::PassKind::WorldOnly, iris::PassKind::PlayersOnly,
                          iris::PassKind::NoWeapons, iris::PassKind::WeaponsOnly, iris::PassKind::Greenscreen,
                          iris::PassKind::Depth, iris::PassKind::Normals, iris::PassKind::ObjectId})
        {
          const bool beauty = kind == iris::PassKind::Beauty;
          ImGui::BeginDisabled(!beauty);
          bool on = beauty;
          ImGui::Checkbox(std::string(iris::ToString(kind)).c_str(), &on);
          ImGui::EndDisabled();
          if (!beauty)
            ImGui::SetItemTooltip("Not implemented for IW4x yet (needs renderer research)");
        }
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("Advanced"))
      {
        c |= ImGui::SliderInt("Frame queue", &r.queueDepth, 1, 32);
        HelpMarker("Frames in flight between the game and the encoder. More is faster but uses the game's 32-bit memory.");
        c |= ImGui::SliderInt("Warm-up frames", &r.warmupFrames, 3, 60);
        c |= ImGui::InputTextWithHint("FFmpeg", "auto-detect", &r.ffmpegPath);
        ImGui::TreePop();
      }
      if (c)
        s.Commit("Edit recording settings", true);
    }

    void DrawRecording(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Recording), &Shown(Panel::Recording)))
      {
        ImGui::End();
        return;
      }
      auto& r = s.project.recording;
      RecordingSettingsEditor(s, r);

      if (Now() - ui.lastCheck > 1.0)
      {
        ui.lastCheck = Now();
        ui.issues = Capture::Check(r, &s.project);
        const auto ff = Capture::FindFfmpeg(r.ffmpegPath.empty() ? s.settings.ffmpegPath : r.ffmpegPath);
        ui.ffmpegFound = ff.has_value();
        ui.ffmpegPathFound = ff.value_or("");
      }
      ImGui::Spacing();
      ImGui::TextDisabled("FFmpeg: %s", ui.ffmpegFound ? ui.ffmpegPathFound.c_str() : "not found (only PNG / JPEG available)");
      const auto timeRange = iris::ResolveRange(r, s.project.timeline);
      const auto frames = iris::FramesInRange(timeRange.start, timeRange.end, r.frameRate);
      const auto size = iris::ResolveSize(r, s.status.renderWidth, s.status.renderHeight);
      ImGui::Text("%lld frames, %s to %s", static_cast<long long>(frames.count), Fmt(timeRange.start, s.project).c_str(),
                  Fmt(timeRange.end, s.project).c_str());
      ImGui::TextDisabled("About %s on disk", iris::FormatBytes(iris::EstimateDiskUsage(r, size.width, size.height, frames.count)).c_str());

      for (const auto& i : ui.issues)
        ImGui::TextColored(i.severity == iris::Issue::Severity::Error ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                           "%s %s", i.severity == iris::Issue::Severity::Error ? Icons::TIMES : Icons::WARNING, i.message.c_str());

      ImGui::Spacing();
      const auto progress = Capture::GetProgress();
      if (Capture::IsActive())
      {
        const float f = progress.framesTotal > 0 ? static_cast<float>(progress.framesCaptured) / static_cast<float>(progress.framesTotal) : 0.0f;
        ImGui::ProgressBar(f, ImVec2(-1, 0));
        ImGui::Text("%s - %lld / %lld frames, %lld encoded, %.0f s", Capture::ToString(progress.state),
                    static_cast<long long>(progress.framesCaptured), static_cast<long long>(progress.framesTotal),
                    static_cast<long long>(progress.framesEncoded), progress.elapsedSeconds);
        ImGui::TextDisabled("%s", progress.message.c_str());
        if (ImGui::Button((std::string(Icons::STOP) + "  Cancel").c_str(), ImVec2(-1, 0)))
          s.Post([] { Capture::Cancel(); });
      }
      else
      {
        ImGui::BeginDisabled(iris::HasErrors(ui.issues));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.22f, 0.25f, 1.0f));
        if (ImGui::Button((std::string(Icons::RECORD) + "  Start recording").c_str(), ImVec2(-1, 30)))
        {
          auto project = s.project;
          s.Post([project] {
            std::string error;
            if (!Capture::Start(project, project.recording, &error))
              Notify("Recording did not start: " + error);
          });
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if (progress.state == Capture::State::Done || progress.state == Capture::State::Failed ||
            progress.state == Capture::State::Cancelled)
        {
          ImGui::TextColored(progress.state == Capture::State::Done ? ImVec4(0.5f, 0.9f, 0.6f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                             "Last recording %s: %s", Capture::ToString(progress.state), progress.message.c_str());
          ImGui::TextDisabled("%s", progress.outputPath.c_str());
          ImGui::TextDisabled("Frame sync %s, renderer latency %d frame(s), clock drift %lld ms",
                              progress.syncVerified ? "verified" : "NOT verified", progress.latencyFrames,
                              static_cast<long long>(progress.clockDriftMs));
          if (ImGui::SmallButton("Open folder"))
            ShellExecuteW(nullptr, L"open", std::filesystem::path(progress.outputPath).parent_path().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
      }
      ImGui::End();
    }

    void DrawRenderQueue(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::RenderQueue), &Shown(Panel::RenderQueue)))
      {
        ImGui::End();
        return;
      }
      auto& q = s.project.renderQueue;
      if (ImGui::Button((std::string(Icons::PLUS) + "  Add current settings").c_str()))
      {
        iris::RenderJob job;
        job.id = iris::MakeUniqueId(s.project, "job");
        job.name = s.project.name + " " + job.id;
        job.cameraId = s.project.activeCameraId;
        job.settings = s.project.recording;
        q.push_back(job);
        s.Commit("Queue render");
      }
      ImGui::SameLine();
      if (ui.queueRunning)
      {
        if (ImGui::Button("Stop queue"))
          ui.queueRunning = false;
      }
      else if (ImGui::Button((std::string(Icons::PLAY) + "  Render queue").c_str()))
        ui.queueRunning = true;

      if (ImGui::BeginTable("##queue", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
      {
        ImGui::TableSetupColumn("Job");
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < q.size(); ++i)
        {
          auto& job = q[i];
          ImGui::PushID(job.id.c_str());
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1);
          ImGui::InputText("##n", &job.name);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(iris::DisplayName(job.settings.format).data());
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(job.status.c_str());
          if (!job.message.empty())
            ImGui::SetItemTooltip("%s", job.message.c_str());
          ImGui::TableNextColumn();
          if (ImGui::SmallButton(Icons::TRASH))
          {
            q.erase(q.begin() + static_cast<std::ptrdiff_t>(i));
            s.Commit("Remove render job");
            ImGui::PopID();
            break;
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      // Queue runner: start the next queued job when no capture runs.
      if (ui.queueRunning && !Capture::IsActive())
      {
        const auto progress = Capture::GetProgress();
        for (auto& job : q)
          if (job.status == "rendering")
          {
            job.status = progress.state == Capture::State::Done ? "done" : (progress.state == Capture::State::Cancelled ? "cancelled" : "failed");
            job.message = progress.message;
          }
        auto next = std::find_if(q.begin(), q.end(), [](const iris::RenderJob& j) { return j.status == "queued"; });
        if (next == q.end())
          ui.queueRunning = false;
        else
        {
          next->status = "rendering";
          auto project = s.project;
          if (!next->cameraId.empty())
            project.activeCameraId = next->cameraId;
          const auto settings = next->settings;
          s.Post([project, settings] {
            std::string error;
            if (!Capture::Start(project, settings, &error))
            {
              Notify("Render job failed to start: " + error);
              auto& session = GetSession();
              std::lock_guard _(session.mutex);
              for (auto& j : session.project.renderQueue)
                if (j.status == "rendering")
                {
                  j.status = "failed";
                  j.message = error;
                }
            }
          });
        }
      }
      ImGui::TextDisabled("Jobs render one after another with their own settings and camera.");
      ImGui::End();
    }

    // ------------------------------------------------------------ export

    void DrawExport(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Export), &Shown(Panel::Export)))
      {
        ImGui::End();
        return;
      }
      ImGui::Combo("Format", &ui.exportFormat, "Blender (camera JSON + importer add-on)\0After Effects (.jsx)\0CSV\0");
      auto& e = s.project.exports;
      DragDouble("Units per metre", &e.unitsPerMetre, 0.1f, 1.0, 1000.0, "%.3f");
      HelpMarker("IW4 units are treated as inches (39.37 per metre).");
      if (ui.exportFormat == 1)
        DragDouble("Units per pixel", &e.aeUnitsPerPixel, 0.01f, 0.01, 100.0, "%.2f");
      const auto range = iris::ResolveRange(s.project.recording, s.project.timeline);
      const auto rate = s.project.recording.frameRate;
      const auto frames = iris::FramesInRange(range.start, range.end, rate);
      ImGui::Text("Exports the shot's camera for %lld frames at %s FPS (the recording range).",
                  static_cast<long long>(frames.count), rate.ToString().c_str());
      if (ImGui::Button((std::string(Icons::EXPORT) + "  Export").c_str(), ImVec2(-1, 0)))
      {
        const auto ctx = Runtime::MakeRigContextFor(s);
        const auto& project = s.project;
        auto samples = iris::SampleFrames(rate, frames.first, frames.count, [&](double t) -> std::optional<iris::CameraState> {
          const auto* cam = project.CameraAt(t);
          if (!cam)
            return std::nullopt;
          if (cam->mode == iris::CameraMode::Free)
            return s.freeCamera.State();
          return iris::EvaluateCamera(*cam, t, ctx);
        });
        iris::ExportInfo info;
        info.projectName = project.name;
        info.mapName = s.status.mapName;
        info.frameRate = rate;
        info.width = s.status.renderWidth > 0 ? s.status.renderWidth : 1920;
        info.height = s.status.renderHeight > 0 ? s.status.renderHeight : 1080;
        info.unitsPerMetre = e.unitsPerMetre;
        info.unitsPerPixel = e.aeUnitsPerPixel;
        std::string text, ext;
        switch (ui.exportFormat)
        {
        case 0: text = iris::ExportBlenderJson(info, samples); ext = ".iw4cam.json"; break;
        case 1: text = iris::ExportAfterEffectsJsx(info, samples); ext = ".jsx"; break;
        default: text = iris::ExportCsv(samples); ext = ".csv"; break;
        }
        std::error_code ec;
        std::filesystem::create_directories(s.ExportsDirectory(), ec);
        const auto file = s.ExportsDirectory() / (iris::SanitizeFileName(project.name) + ext);
        std::ofstream(file, std::ios::binary) << text;
        ui.lastExport = file.string();
        Notify("Exported " + std::to_string(samples.size()) + " frames to " + ui.lastExport);
      }
      if (!ui.lastExport.empty())
      {
        ImGui::TextDisabled("%s", ui.lastExport.c_str());
        if (ImGui::SmallButton("Open folder"))
          ShellExecuteW(nullptr, L"open", s.ExportsDirectory().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      ImGui::Spacing();
      ImGui::TextWrapped("Blender: install iw4iris/tools/blender/iw4iris_import.py (Edit > Preferences > Add-ons), then "
                         "File > Import > IW4Iris camera. After Effects: File > Scripts > Run Script File. See docs/EXPORT.md.");
      ImGui::End();
    }

    // ------------------------------------------------------------ presets

    void DrawPresets(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Presets), &Shown(Panel::Presets)))
      {
        ImGui::End();
        return;
      }
      const auto& presets = iris::ShotPresets();
      ui.presetIndex = std::clamp(ui.presetIndex, 0, static_cast<int>(presets.size()) - 1);
      if (ImGui::BeginListBox("##presets", ImVec2(-1, 200)))
      {
        for (int i = 0; i < static_cast<int>(presets.size()); ++i)
          if (ImGui::Selectable(presets[static_cast<std::size_t>(i)].name.c_str(), i == ui.presetIndex))
          {
            ui.presetIndex = i;
            ui.presetDuration = presets[static_cast<std::size_t>(i)].defaultDuration;
          }
        ImGui::EndListBox();
      }
      const auto& preset = presets[static_cast<std::size_t>(ui.presetIndex)];
      ImGui::TextWrapped("%s", preset.description.c_str());
      ImGui::Combo("Subject", &ui.presetSubject, "What the view looks at (focus distance)\0A player\0");
      if (ui.presetSubject == 1)
      {
        if (s.status.players.empty())
          ImGui::TextDisabled("No players in the scene.");
        else
        {
          ui.presetPlayer = std::clamp(ui.presetPlayer, 0, static_cast<int>(s.status.players.size()) - 1);
          if (ImGui::BeginCombo("Player", s.status.players[static_cast<std::size_t>(ui.presetPlayer)].second.c_str()))
          {
            for (int i = 0; i < static_cast<int>(s.status.players.size()); ++i)
              if (ImGui::Selectable(s.status.players[static_cast<std::size_t>(i)].second.c_str(), i == ui.presetPlayer))
                ui.presetPlayer = i;
            ImGui::EndCombo();
          }
        }
      }
      DragDouble("Duration", &ui.presetDuration, 0.05f, 0.25, 60.0, "%.2f s");
      ImGui::Checkbox("Create a new dolly camera", &ui.presetNewCamera);
      ImGui::TextDisabled("Starts at the playhead; ramps centre on the next marker, else the middle.");
      if (ImGui::Button((std::string(Icons::MAGIC) + "  Build shot").c_str(), ImVec2(-1, 0)))
      {
        iris::PresetSubject subject;
        const auto view = Runtime::LastAppliedCamera();
        if (ui.presetSubject == 1 && !s.status.players.empty())
        {
          const auto num = s.status.players[static_cast<std::size_t>(ui.presetPlayer)].first;
          const iris::TargetRef ref{iris::TargetKind::Player, num, {}, {0, 0, 60}};
          if (const auto pos = Runtime::MakeRigContextFor(s).resolveTarget(ref))
            subject.position = *pos;
          subject.facingYaw = view ? view->angles.yaw : 0.0;
        }
        else if (view)
        {
          subject.position = view->position + iris::AnglesToForward(view->angles) * std::max(64.0, view->focusDistance);
          subject.facingYaw = view->angles.yaw + 180.0;
        }
        double moment = s.playhead + ui.presetDuration * 0.5;
        for (const auto& m : s.project.markers)
          if (m.time > s.playhead && m.time < s.playhead + ui.presetDuration)
          {
            moment = m.time;
            break;
          }
        auto result = iris::BuildPreset(preset.id, subject, s.playhead, ui.presetDuration, moment);
        iris::CameraDef* target = nullptr;
        if (ui.presetNewCamera)
        {
          iris::CameraDef c;
          c.id = iris::MakeUniqueId(s.project, "cam");
          c.name = preset.name;
          c.mode = iris::CameraMode::Dolly;
          s.project.cameras.push_back(c);
          target = &s.project.cameras.back();
        }
        else
          target = &s.EditTargetDolly();
        for (const auto& k : result.cameraKeys)
          target->path.AddKey(k);
        target->path.SetConstantSpeed(result.constantSpeed);
        target->path.SetRotationMode(result.rotationMode);
        for (const auto& k : result.speedKeys)
          s.project.timeRemap.Speed().AddKey(k);
        for (auto m : result.markers)
        {
          m.id = iris::MakeUniqueId(s.project, "marker");
          s.project.markers.push_back(m);
        }
        s.project.activeCameraId = target->id;
        s.selectedCamera = target->id;
        s.Commit("Shot preset: " + preset.name);
        Notify("Built '" + preset.name + "' - every key stays editable");
      }
      ImGui::End();
    }

    // ------------------------------------------------------------ actors / lighting (research)

    void DrawLighting(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Lighting), &Shown(Panel::Lighting)))
      {
        ImGui::End();
        return;
      }
      Unavailable("Dynamic lights", "IW4's primary lights are baked per map and its dynamic-light API is not verified yet");
      ImGui::TextWrapped("Available today: the sun override and colour grading in the Look panel. Lighting presets "
                         "apply to those:");
      struct LookPreset
      {
        const char* name;
        iris::Vec3 sun;
        double intensity, pitch, brightness, contrast, desat;
        iris::Vec3 light, dark;
      };
      static const LookPreset looks[] = {
        {"Golden hour", {1.0, 0.72, 0.42}, 1.4, -12, 0.02, 1.15, 0.05, {1.08, 0.96, 0.85}, {0.95, 0.92, 1.0}},
        {"Blue hour", {0.55, 0.65, 1.0}, 0.6, -4, -0.05, 1.1, 0.15, {0.85, 0.92, 1.1}, {0.8, 0.85, 1.05}},
        {"Moonlight", {0.6, 0.7, 1.0}, 0.35, -40, -0.12, 1.2, 0.45, {0.85, 0.9, 1.1}, {0.7, 0.75, 0.95}},
        {"Warm sunset", {1.0, 0.55, 0.3}, 1.2, -6, 0.0, 1.2, 0.0, {1.1, 0.9, 0.75}, {0.9, 0.8, 0.85}},
        {"Cold winter", {0.85, 0.92, 1.0}, 1.0, -25, 0.03, 1.05, 0.35, {0.92, 0.98, 1.08}, {0.85, 0.9, 1.0}},
        {"Dark and moody", {0.8, 0.8, 0.85}, 0.5, -30, -0.1, 1.35, 0.3, {0.95, 0.95, 1.0}, {0.85, 0.85, 0.9}},
        {"High contrast", {1.0, 0.98, 0.95}, 1.6, -50, 0.0, 1.6, 0.1, {1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}},
      };
      for (const auto& l : looks)
        if (ImGui::Button(l.name, ImVec2(-1, 0)))
        {
          auto& v = s.project.visuals;
          v.sunOverride = true;
          v.sunColor = l.sun;
          v.sunIntensity = l.intensity;
          v.sunAngles.pitch = l.pitch;
          v.filmOverride = true;
          v.filmBrightness = l.brightness;
          v.filmContrast = l.contrast;
          v.filmDesaturation = l.desat;
          v.filmLightTint = l.light;
          v.filmDarkTint = l.dark;
          s.Commit(std::string("Look preset: ") + l.name);
        }
      ImGui::End();
    }

    // ------------------------------------------------------------ settings / diagnostics / help

    void DrawSettings(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Settings), &Shown(Panel::Settings)))
      {
        ImGui::End();
        return;
      }
      auto& u = s.settings;
      bool c = false;
      ImGui::SeparatorText("Interface");
      float scale = static_cast<float>(u.uiScale);
      if (ImGui::SliderFloat("UI scale", &scale, 0.75f, 2.0f, "%.2f"))
      {
        u.uiScale = scale;
        ApplyTheme(u.accent, scale);
        ImGui::GetStyle().FontScaleMain = scale;
        c = true;
      }
      if (ImGui::BeginCombo("Accent", AccentName(u.accent)))
      {
        for (int i = 0; i < AccentCount(); ++i)
          if (ImGui::Selectable(AccentName(i), i == u.accent))
          {
            u.accent = i;
            ApplyTheme(i, static_cast<float>(u.uiScale));
            ImGui::GetStyle().FontScaleMain = static_cast<float>(u.uiScale);
            c = true;
          }
        ImGui::EndCombo();
      }
      c |= ImGui::Checkbox("Show getting-started guide", &u.onboarding);
      c |= ImGui::Checkbox("Show the start screen when nothing is loaded", &u.showHome);
      c |= ImGui::Checkbox("Camera path overlay", &u.showPathOverlay);
      c |= ImGui::Checkbox("Camera frustums", &u.showFrustums);
      ImGui::SeparatorText("Free camera");
      c |= SliderDouble("Speed", &u.freeCamera.speed, 10.0, 3000.0, "%.0f u/s");
      c |= SliderDouble("Acceleration", &u.freeCamera.acceleration, 1.0, 40.0, "%.1f");
      c |= SliderDouble("Look smoothing", &u.freeCamera.lookSmoothing, 0.0, 0.9, "%.2f");
      c |= SliderDouble("Roll speed", &u.freeCamera.rollSpeed, 5.0, 180.0, "%.0f deg/s");
      c |= SliderDouble("Mouse sensitivity", &u.mouseSensitivity, 0.01, 0.6, "%.3f");
      c |= ImGui::Checkbox("Fly in view direction", &u.freeCamera.flyInViewDirection);
      ImGui::SeparatorText("Project");
      c |= ImGui::Checkbox("Autosave", &u.autosave);
      c |= ImGui::SliderInt("Autosave every", &u.autosaveSeconds, 10, 600, "%d s");
      c |= ImGui::InputTextWithHint("FFmpeg", "auto-detect", &u.ffmpegPath);
      auto& t = s.project.timeline;
      int display = static_cast<int>(t.display);
      if (ImGui::Combo("Time display", &display, "Timecode\0Seconds\0Frames\0"))
      {
        t.display = static_cast<iris::TimeDisplay>(display);
        s.Commit("Time display");
      }
      if (ImGui::BeginCombo("Timeline frame rate", t.frameRate.ToString().c_str()))
      {
        for (const auto& fr : iris::StandardFrameRates())
          if (ImGui::Selectable(fr.ToString().c_str(), fr == t.frameRate))
          {
            t.frameRate = fr;
            s.Commit("Timeline frame rate");
          }
        ImGui::EndCombo();
      }
      if (ImGui::Checkbox("Snap keys to frames", &t.snapToFrames))
        s.Commit("Snapping");
      if (c)
        SaveSettings(s);
      ImGui::End();
    }

    void DrawDiagnostics(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::Diagnostics), &Shown(Panel::Diagnostics)))
      {
        ImGui::End();
        return;
      }
      DrawDiagnosticsOverview(s);
      ImGui::Spacing();
      if (!ImGui::CollapsingHeader("Technical details"))
      {
        ImGui::End();
        return;
      }
      const auto g = OfflineGuard::GetStatus();
      ImGui::SeparatorText("Build");
      ImGui::Text("%s", iris::kVersionString.data());
      ImGui::Text("IW4x revision %s", IW4IRIS_IW4X_REVISION);
      ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
      ImGui::SeparatorText("Offline guard");
      ImGui::Text("Engine packet sender blocked: %s", g.engineSendBlocked ? "yes" : "NO");
      ImGui::Text("Winsock imports guarded: %d, HTTP imports guarded: %d", g.winsockImportsGuarded, g.httpImportsGuarded);
      ImGui::Text("Dropped packets: %llu, blocked connections: %llu, blocked HTTP: %llu",
                  static_cast<unsigned long long>(g.droppedPackets), static_cast<unsigned long long>(g.blockedConnections),
                  static_cast<unsigned long long>(g.blockedHttp));
      ImGui::Text("Session: %s", g.sessionViolation ? "VIOLATION - tools disabled" : "local");
      if (!g.lastBlocked.empty())
        ImGui::TextDisabled("Last blocked: %s", g.lastBlocked.c_str());
      ImGui::SeparatorText("Engine");
      const auto& st = s.status;
      ImGui::Text("cgame %s, connection state %d, demo %s", st.cgameReady ? "ready" : "not ready", st.connectionState,
                  st.demoPlaying ? st.demoName.c_str() : "none");
      ImGui::Text("client time %d, snapshot %d, scene %.3f s, paused %s", st.clientServerTime, st.snapshotServerTime,
                  st.sceneTime, st.paused ? "yes" : "no");
      if (s.demoIndex)
        ImGui::Text("demo index: %zu snapshots, %.2f s, %u messages", s.demoIndex->snapshots.size(),
                    s.demoIndex->DurationSeconds(), s.demoIndex->networkMessages);
      ImGui::Text("render %d x %d, render clock %s", st.renderWidth, st.renderHeight, Clock::Installed() ? "available" : "UNAVAILABLE");
      ImGui::SeparatorText("Dvars used by the editor");
      if (ImGui::BeginTable("##dvars", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 160)))
      {
        for (const auto& name : s.status.checkedDvars)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(name.c_str());
          ImGui::TableNextColumn();
          const bool found = s.status.availableDvars.contains(name);
          ImGui::TextColored(found ? ImVec4(0.5f, 0.9f, 0.6f, 1) : ImVec4(1, 0.5f, 0.5f, 1), found ? "found" : "missing");
        }
        ImGui::EndTable();
      }
      ImGui::SeparatorText("Log");
      if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders))
      {
        for (const auto& line : Log::Tail(200))
          ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
          ImGui::SetScrollHereY(1.0f);
      }
      ImGui::EndChild();
      ImGui::End();
    }

    void DrawHistory(Session& s)
    {
      if (!ImGui::Begin(PanelName(Panel::History), &Shown(Panel::History)))
      {
        ImGui::End();
        return;
      }
      const auto labels = s.history.Labels();
      for (std::size_t i = 0; i < labels.size(); ++i)
        ImGui::Selectable(labels[i].c_str(), i == s.history.Cursor());
      ImGui::End();
    }

    void DrawShortcuts()
    {
      if (!Shown(Panel::Shortcuts))
        return;
      ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin(PanelName(Panel::Shortcuts), &Shown(Panel::Shortcuts)))
      {
        ImGui::End();
        return;
      }
      static const char* rows[][2] = {
        {"F1", "Show / hide the editor"}, {"Space", "Play / pause"}, {"Left / Right", "Previous / next frame (Shift: 1 s)"},
        {"Home / End", "Go to the in / out point (Shift: timeline start / end)"},
        {"Up / Down", "Previous / next edit point (key, marker, cut, clip, in/out)"},
        {"I / O", "Set the in / out point at the playhead (B / N also work)"},
        {"+ / -", "Zoom the timeline in / out"}, {"\\", "Show the whole timeline"},
        {"Timeline: click / drag", "Move the playhead (ruler or empty rows)"},
        {"Timeline: wheel", "Zoom (Shift: scroll sideways, Ctrl or over names: scroll rows)"},
        {"Timeline: middle drag", "Scroll the timeline"},
        {"Alt while dragging", "Ignore the magnet (move freely)"},
        {"K", "Add a camera key from the view (also while flying)"},
        {"U", "Update the selected key from the view"}, {"Del", "Delete the selected keys"}, {"Ctrl+C / Ctrl+V", "Copy / paste keys"},
        {"Ctrl+K", "Command palette: type what you want to do"}, {"Ctrl+Z / Ctrl+Y", "Undo / redo"}, {"Ctrl+S", "Save"}, {"Ctrl+O", "Open"}, {"Ctrl+N", "New project"},
        {"M", "Add a marker"}, {"C", "Next camera"}, {"1 - 9", "Make camera 1 - 9 active"},
        {"V", "Free view / shot camera (flying never changes the shot)"},
        {"F", "Toggle flying the free camera (Esc also stops)"}, {"Right mouse", "Hold to fly the free camera"},
        {"W A S D", "Move (Space up, Ctrl down)"}, {"Q / E", "Roll"}, {"Shift / Alt", "Faster / slower"},
        {"Wheel (flying)", "Zoom (field of view)"}, {"G", "Toggle the graph editor"}, {"?", "This list"},
      };
      if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg))
      {
        for (const auto& r : rows)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextColored(CurrentPalette().accent, "%s", r[0]);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(r[1]);
        }
        ImGui::EndTable();
      }
      ImGui::End();
    }

    void DrawGettingStarted(Session& s)
    {
      if (!Shown(Panel::GettingStarted) || !s.settings.onboarding)
        return;
      const auto* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.35f),
                              ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin(PanelName(Panel::GettingStarted), &Shown(Panel::GettingStarted)))
      {
        ImGui::End();
        return;
      }
      ImGui::PushFont(boldFont, 0.0f);
      ImGui::TextColored(CurrentPalette().accent, "Welcome to IW4Iris");
      ImGui::PopFont();
      // Read-only lookup: drawing the guide must not create cameras.
      const iris::CameraDef* dolly = nullptr;
      for (const auto& c : s.project.cameras)
        if (c.mode == iris::CameraMode::Dolly && (!dolly || c.id == s.selectedCamera))
          dolly = &c;
      const struct
      {
        bool done;
        const char* text;
      } steps[] = {
        {s.status.demoPlaying || s.status.cgameReady, "Load a demo: View > Demos, double-click one."},
        {s.status.gameView.has_value(), "Fly: hold the right mouse button in the view, move with WASD."},
        {dolly && dolly->path.Size() >= 3, "Add three or more keys with K at different times (Space plays)."},
        {dolly && s.project.activeCameraId == dolly->id,
         "The dolly is the active camera (the first key does it; else pick it in the toolbar's camera box)."},
        {!s.projectFile.empty(), "Save the project (Ctrl+S)."},
        {Capture::GetProgress().state == Capture::State::Done, "Record it in the Recording panel."},
      };
      for (const auto& st : steps)
      {
        ImGui::TextColored(st.done ? ImVec4(0.5f, 0.9f, 0.6f, 1.0f) : ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s",
                           st.done ? Icons::CHECK : Icons::INFO);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", st.text);
      }
      ImGui::Spacing();
      if (ImGui::Button("Do not show again"))
      {
        s.settings.onboarding = false;
        SaveSettings(s);
      }
      ImGui::End();
    }

    // The start screen: the four ways in, recent projects, and where help is.
    void DrawHome(Session& s)
    {
      if (!ui.homeOpen || !s.settings.showHome)
        return;
      // Close by itself once there is something to work on.
      if (s.status.demoPlaying || s.status.cgameReady || !s.projectFile.empty())
      {
        ui.homeOpen = false;
        return;
      }
      const auto* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f),
                              ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(std::min(620.0f, vp->WorkSize.x - 40.0f), 0.0f), ImGuiCond_Appearing);
      bool open = true;
      if (!ImGui::Begin("IW4Iris Studio###Home", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                                            ImGuiWindowFlags_AlwaysAutoResize))
      {
        ImGui::End();
        ui.homeOpen = open;
        return;
      }
      ImGui::PushFont(boldFont, 0.0f);
      ImGui::TextColored(CurrentPalette().accent, "What do you want to make?");
      ImGui::PopFont();
      ImGui::Spacing();
      const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      const ImVec2 size(w, 64);
      auto tile = [&](const char* icon, const char* title, const char* text) {
        const bool clicked = ImGui::Button((std::string(icon) + "  " + title + "##" + title).c_str(), size);
        ImGui::SetItemTooltip("%s", text);
        return clicked;
      };
      if (tile(Icons::FILM, "Edit a recorded demo", "Fly cameras through a demo you recorded: camera paths, slow motion, "
                                                    "cuts, look, depth of field. Then render it."))
      {
        ApplyWorkspace("Cinematic");
        Shown(Panel::Demos) = true;
        ImGui::SetWindowFocus(PanelName(Panel::Demos));
        Notify("Double-click a demo in the Demos panel to load it");
        ui.homeOpen = false;
      }
      ImGui::SameLine();
      if (tile(Icons::USER, "Create a cinematic on a map", "An offline film set: place characters, props, effects and 3D "
                                                           "titles on any installed map, then film them."))
      {
        ApplyWorkspace("Director");
        Shown(Panel::Director) = true;
        ui.wizardOpen = true;
        ui.wizardStep = 0;
        ui.mapsRequested = false;
        ui.homeOpen = false;
      }
      if (tile(Icons::FOLDER_OPEN, "Open a project", "Continue a saved .iw4iris project"))
      {
        ui.openPopup = true;
        ui.homeOpen = false;
      }
      ImGui::SameLine();
      if (tile(Icons::MAGIC, "Browse templates", "Ready-made scenes (duel, squad, title card ...) built on a film set of "
                                                "your choice"))
      {
        ApplyWorkspace("Director");
        Shown(Panel::Director) = true;
        ui.wizardOpen = true;
        ui.wizardStep = 0;
        ui.mapsRequested = false;
        ui.homeOpen = false;
      }
      if (!s.settings.recentProjects.empty())
      {
        ImGui::Spacing();
        ImGui::TextDisabled("Recent projects");
        int shown = 0;
        for (const auto& r : s.settings.recentProjects)
        {
          if (shown++ >= 5)
            break;
          const std::string name = std::filesystem::path(std::u8string(r.begin(), r.end())).filename().string();
          if (ImGui::Selectable((std::string(Icons::FILE) + "  " + name + "##" + r).c_str()))
          {
            OpenProject(s, r);
            ui.homeOpen = false;
          }
          ImGui::SetItemTooltip("%s", r.c_str());
        }
      }
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextDisabled("Ctrl+K: search every action   ?: keyboard shortcuts   F1: hide / show the editor");
      bool showAtStart = s.settings.showHome;
      if (ImGui::Checkbox("Show this at start", &showAtStart))
      {
        s.settings.showHome = showAtStart;
        SaveSettings(s);
      }
      ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 80.0f);
      if (ImGui::Button("Close", ImVec2(80, 0)))
        open = false;
      ImGui::End();
      if (!open)
        ui.homeOpen = false;
    }

    void DrawNotification(Session& s)
    {
      if (!s.notice.empty())
      {
        Notify(s.notice);
        s.notice.clear();
      }
      if (ui.message.empty() || Now() - ui.messageTime > 6.0)
        return;
      const auto* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 70.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.92f);
      ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);
      ImGui::TextColored(CurrentPalette().accent, "%s", Icons::INFO);
      ImGui::SameLine();
      ImGui::TextUnformatted(ui.message.c_str());
      ImGui::End();
    }

    void HandleShortcuts(Session& s)
    {
      auto& io = ImGui::GetIO();
      if (io.WantTextInput)
        return;
      auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
      if (Runtime::IsFlying())
      {
        // While flying the keys move the camera. Only keys that do not
        // steer it work: leaving fly mode (toggled with F; the cursor is
        // hidden) and adding a key from the view.
        if (s.flying && (pressed(ImGuiKey_F) || pressed(ImGuiKey_Escape)))
          s.flying = false;
        if (pressed(ImGuiKey_K))
          s.Post([] { Runtime::AddKeyFromView(); });
        return;
      }
      // With a scene object selected, G / R / S / F / Del / Ctrl+D act on it.
      if (HandleObjectShortcuts(s))
        return;
      const bool ctrl = io.KeyCtrl;
      if (ctrl && pressed(ImGuiKey_Z))
        io.KeyShift ? (void)s.Redo() : (void)s.Undo();
      else if (ctrl && pressed(ImGuiKey_Y))
        s.Redo();
      else if (ctrl && pressed(ImGuiKey_S))
      {
        if (s.projectFile.empty())
          ui.saveAsPopup = true;
        else
          SaveCurrent(s, s.projectFile.string());
      }
      else if (ctrl && pressed(ImGuiKey_O))
        ui.openPopup = true;
      else if (ctrl && (pressed(ImGuiKey_K) || (io.KeyShift && pressed(ImGuiKey_P))))
        OpenCommandPalette();
      else if (ctrl && pressed(ImGuiKey_N))
        s.Post([] { Runtime::NewProject(); });
      else if (ctrl && pressed(ImGuiKey_C))
        CopySelectedKeys(s);
      else if (ctrl && pressed(ImGuiKey_V))
        PasteKeys(s);
      else if (!ctrl)
      {
        auto& t = s.project.timeline;
        if (pressed(ImGuiKey_Space))
          TogglePlay(s);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
          StepFrames(s, io.KeyShift ? -static_cast<int>(std::llround(t.frameRate.Fps())) : -1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
          StepFrames(s, io.KeyShift ? static_cast<int>(std::llround(t.frameRate.Fps())) : 1);
        if (pressed(ImGuiKey_Home))
          RequestSeek(s, io.KeyShift ? t.start : t.inPoint);
        if (pressed(ImGuiKey_End))
          RequestSeek(s, io.KeyShift ? t.end : t.outPoint);
        // Up / Down: previous / next edit point (keys, markers, cuts, clips, in/out).
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
          if (const auto prev = iris::PreviousPoint(s.playhead, EditPoints(s)))
            RequestSeek(s, *prev);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
          if (const auto next = iris::NextPoint(s.playhead, EditPoints(s)))
            RequestSeek(s, *next);
        // In / out at the playhead: I and O (B and N still work).
        if (pressed(ImGuiKey_I) || pressed(ImGuiKey_B))
          SetInPoint(s, s.playhead);
        if (pressed(ImGuiKey_O) || pressed(ImGuiKey_N))
          SetOutPoint(s, s.playhead);
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
          ZoomTimeline(s, 0.7);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
          ZoomTimeline(s, 1.0 / 0.7);
        if (pressed(ImGuiKey_Backslash))
          FitTimeline(s);
        if (pressed(ImGuiKey_K))
          s.Post([] { Runtime::AddKeyFromView(); });
        if (pressed(ImGuiKey_U))
          if (auto* cam = s.SelectedCamera(); cam && s.selectedKeys.size() == 1)
            if (const auto view = Runtime::LastAppliedCamera())
              if (*s.selectedKeys.begin() < cam->path.Size())
              {
                auto& k = cam->path.MutableKeys()[*s.selectedKeys.begin()];
                k.position = view->position;
                k.angles = view->angles;
                k.horizontalFov = view->horizontalFov;
                cam->path.InvalidateCache();
                s.Commit("Update key from view");
              }
        if (pressed(ImGuiKey_Delete))
          DeleteSelectedKeys(s);
        if (pressed(ImGuiKey_M))
          AddMarker(s);
        if (pressed(ImGuiKey_V) && !Capture::IsActive())
        {
          if (s.freeView)
            ShowShot(s);
          else
            s.freeView = true;
        }
        if (pressed(ImGuiKey_F))
          s.flying = !s.flying;
        if (pressed(ImGuiKey_G))
          ui.timeline.graph = !ui.timeline.graph;
        if (pressed(ImGuiKey_Slash) && io.KeyShift)
          Shown(Panel::Shortcuts) = !Shown(Panel::Shortcuts);
        if (pressed(ImGuiKey_C) && !s.project.cameras.empty())
        {
          auto& cams = s.project.cameras;
          auto it = std::find_if(cams.begin(), cams.end(), [&](const iris::CameraDef& c) { return c.id == s.project.activeCameraId; });
          const auto next = it == cams.end() || it + 1 == cams.end() ? cams.begin() : it + 1;
          s.project.activeCameraId = next->id;
          s.selectedCamera = next->id;
          s.Commit("Change active camera");
        }
        for (int i = 0; i < 9; ++i)
          if (pressed(static_cast<ImGuiKey>(ImGuiKey_1 + i)) && i < static_cast<int>(s.project.cameras.size()))
          {
            s.project.activeCameraId = s.project.cameras[static_cast<std::size_t>(i)].id;
            s.selectedCamera = s.project.activeCameraId;
            s.Commit("Change active camera");
          }
      }
    }
  }

  // ------------------------------------------------------------ shared

  UiState& Ui()
  {
    return ui;
  }

  void Notify(std::string message)
  {
    ui.message = std::move(message);
    ui.messageTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    Log::Info("{}", ui.message);
  }

  void SaveProject(Session& s)
  {
    if (s.projectFile.empty())
      ui.saveAsPopup = true;
    else
      SaveCurrent(s, s.projectFile.string());
  }

  void UseWorkspace(const std::string& name)
  {
    ApplyWorkspace(name);
  }

  std::vector<std::string> WorkspaceNames()
  {
    std::vector<std::string> names;
    for (const auto& w : Workspaces())
      names.push_back(w.name);
    return names;
  }

  void RequestSeek(Session& s, double t)
  {
    auto& tl = s.project.timeline;
    t = std::clamp(t, tl.start, tl.end);
    if (tl.snapToFrames)
      t = tl.frameRate.SnapSeconds(t);
    s.playhead = t;
    if (s.status.demoPlaying)
    {
      const double scene = s.project.timeRemap.SceneTime(t);
      s.Post([scene] { DemoControl::Seek(scene); });
    }
  }

  void TogglePlay(Session& s)
  {
    if (s.status.demoPlaying)
    {
      const bool pause = !s.status.paused;
      s.Post([pause] { DemoControl::SetPaused(pause); });
      s.status.paused = pause;
      s.playing = !pause;
    }
    else
    {
      s.playing = !s.playing;
    }
    // Playback previews the shot: the free view would show a still camera.
    if (s.playing && s.freeView)
    {
      s.freeView = false;
      Notify("Playing through the shot camera (V: free view)");
    }
  }

  void ShowShot(Session& s)
  {
    s.flying = false;
    s.freeView = false;
  }

  void CameraFromView(Session& s)
  {
    const auto view = Runtime::LastAppliedCamera();
    if (!view)
    {
      Notify("No view yet: load a demo or a map first");
      return;
    }
    auto& p = s.project;
    iris::CameraDef c;
    c.id = iris::MakeUniqueId(p, "cam");
    c.mode = iris::CameraMode::Static;
    c.name = "Camera " + c.id.substr(4);
    c.position = view->position;
    c.angles = view->angles;
    c.horizontalFov = view->horizontalFov;
    p.cameras.push_back(c);
    p.activeCameraId = c.id;
    s.selectedCamera = c.id;
    s.selectedKeys.clear();
    ShowShot(s);
    s.Commit("Camera from view");
    Notify(p.useCameraCuts ? c.name + " created. Camera cuts decide what is shown: add a cut (scissors) to use it."
                           : c.name + " created from the view and made the active camera.");
  }

  void MoveCameraToView(Session& s)
  {
    const auto view = Runtime::LastAppliedCamera();
    auto* cam = s.SelectedCamera();
    if (!cam)
      cam = s.project.FindCamera(s.project.activeCameraId);
    if (!view || !cam)
    {
      Notify(view ? "Select a camera first" : "No view yet: load a demo or a map first");
      return;
    }
    switch (cam->mode)
    {
      case iris::CameraMode::Dolly:
        s.selectedCamera = cam->id;
        s.Post([] { Runtime::AddKeyFromView(); });
        Notify("Key added to " + cam->name + " at the playhead");
        return;  // the key is added on the main thread; stay in the free view to keep placing keys
      case iris::CameraMode::Static:
      case iris::CameraMode::Free:
      case iris::CameraMode::Orbit:
      case iris::CameraMode::Follow:
        cam->position = view->position;
        cam->angles = view->angles;
        cam->horizontalFov = view->horizontalFov;
        s.Commit("Move camera to view");
        Notify(cam->name + " moved to the view");
        ShowShot(s);
        return;
      default:
        Notify(cam->name + " follows the game's own view; use \"Create a camera from this view\" instead");
        return;
    }
  }

  void StepFrames(Session& s, int frames)
  {
    const auto& tl = s.project.timeline;
    const double dt = frames / tl.frameRate.Fps();
    const double target = std::clamp(s.playhead + dt, tl.start, tl.end);
    if (s.status.demoPlaying && frames > 0 && s.status.paused)
    {
      const double sceneDelta = s.project.timeRemap.SceneTime(target) - s.project.timeRemap.SceneTime(s.playhead);
      s.Post([sceneDelta] { DemoControl::Step(sceneDelta); });
      s.playhead = target;
    }
    else
    {
      RequestSeek(s, target);
    }
  }

  void DeleteSelectedKeys(Session& s)
  {
    auto* cam = s.SelectedCamera();
    if (!cam || s.selectedKeys.empty())
      return;
    std::vector<std::size_t> indices(s.selectedKeys.begin(), s.selectedKeys.end());
    std::sort(indices.rbegin(), indices.rend());
    for (const auto i : indices)
      cam->path.RemoveKeyAt(i);
    s.selectedKeys.clear();
    s.Commit("Delete keys");
  }

  void CopySelectedKeys(Session& s)
  {
    auto* cam = s.SelectedCamera();
    if (!cam || s.selectedKeys.empty())
      return;
    ui.clipboard.clear();
    for (const auto i : s.selectedKeys)
      if (i < cam->path.Size())
        ui.clipboard.push_back(cam->path.Keys()[i]);
    ui.clipboardOrigin = ui.clipboard.empty() ? 0.0 : ui.clipboard.front().time;
    Notify("Copied " + std::to_string(ui.clipboard.size()) + " key(s)");
  }

  void PasteKeys(Session& s)
  {
    if (ui.clipboard.empty())
      return;
    auto& dolly = s.EditTargetDolly();
    s.selectedKeys.clear();
    for (auto k : ui.clipboard)
    {
      k.time = k.time - ui.clipboardOrigin + s.playhead;
      s.selectedKeys.insert(dolly.path.AddKey(k));
    }
    s.selectedCamera = dolly.id;
    s.Commit("Paste keys");
  }

  void AddMarker(Session& s)
  {
    iris::Marker m;
    m.id = iris::MakeUniqueId(s.project, "marker");
    m.time = s.playhead;
    m.name = "Marker " + std::to_string(s.project.markers.size() + 1);
    s.project.markers.push_back(m);
    std::sort(s.project.markers.begin(), s.project.markers.end(), [](const iris::Marker& a, const iris::Marker& b) { return a.time < b.time; });
    s.Commit("Add marker");
  }

  bool DragDouble(const char* label, double* v, float speed, double min, double max, const char* format)
  {
    return ImGui::DragScalar(label, ImGuiDataType_Double, v, speed, &min, &max, format);
  }

  bool SliderDouble(const char* label, double* v, double min, double max, const char* format)
  {
    return ImGui::SliderScalar(label, ImGuiDataType_Double, v, &min, &max, format);
  }

  bool DragVec3(const char* label, iris::Vec3* v, float speed, const char* format)
  {
    return ImGui::DragScalarN(label, ImGuiDataType_Double, &v->x, 3, speed, nullptr, nullptr, format);
  }

  bool ColorVec3(const char* label, iris::Vec3* v)
  {
    float c[3] = {static_cast<float>(v->x), static_cast<float>(v->y), static_cast<float>(v->z)};
    if (ImGui::ColorEdit3(label, c, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
    {
      *v = {c[0], c[1], c[2]};
      return true;
    }
    return false;
  }

  void HelpMarker(const char* text)
  {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Icons::QUESTION);
    ImGui::SetItemTooltip("%s", text);
  }

  void SectionHeader(const char* icon, const char* title)
  {
    ImGui::PushFont(boldFont, 0.0f);
    ImGui::TextColored(CurrentPalette().accent, "%s", icon);
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
  }

  ImU32 ParseColor(const std::string& hex, float alpha)
  {
    unsigned int rgb = 0x8B7CF6;
    if (hex.size() == 7 && hex[0] == '#')
      rgb = static_cast<unsigned int>(std::strtoul(hex.c_str() + 1, nullptr, 16));
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast<int>(alpha * 255.0f));
  }

  bool IconButton(const char* icon, const char* tooltip, bool active)
  {
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, CurrentPalette().accentSoft);
    const bool clicked = ImGui::Button(icon, ImVec2(ImGui::GetFrameHeight() + 6.0f, 0));
    if (active)
      ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
  }

  void Unavailable(const char* what, const char* why)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s %s unavailable: %s.", Icons::WARNING, what, why);
    ImGui::PopStyleColor();
  }

  // ------------------------------------------------------------ public

  void Initialize()
  {
    auto& io = ImGui::GetIO();
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    iniPath = (s.baseDirectory / "imgui.ini").string();
    io.IniFilename = iniPath.c_str();
    // No keyboard navigation: Space and the arrows are timeline shortcuts.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.MouseDrawCursor = true;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    const auto fonts = s.baseDirectory / "fonts";
    const float size = 15.0f;
    std::error_code ec;
    ImFont* main = nullptr;
    if (std::filesystem::exists(fonts / "Rubik-Regular.ttf", ec))
    {
      main = io.Fonts->AddFontFromFileTTF((fonts / "Rubik-Regular.ttf").string().c_str(), size);
      if (main && std::filesystem::exists(fonts / "fa-solid-900.ttf", ec))
      {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.GlyphMinAdvanceX = size;
        cfg.GlyphOffset = ImVec2(0, 1);
        static const ImWchar ranges[] = {Icons::kMin, Icons::kMax, 0};
        io.Fonts->AddFontFromFileTTF((fonts / "fa-solid-900.ttf").string().c_str(), size * 0.9f, &cfg, ranges);
      }
      if (std::filesystem::exists(fonts / "Rubik-SemiBold.ttf", ec))
      {
        boldFont = io.Fonts->AddFontFromFileTTF((fonts / "Rubik-SemiBold.ttf").string().c_str(), size);
        if (boldFont && std::filesystem::exists(fonts / "fa-solid-900.ttf", ec))
        {
          ImFontConfig cfg;
          cfg.MergeMode = true;
          cfg.GlyphMinAdvanceX = size;
          static const ImWchar ranges[] = {Icons::kMin, Icons::kMax, 0};
          io.Fonts->AddFontFromFileTTF((fonts / "fa-solid-900.ttf").string().c_str(), size * 0.9f, &cfg, ranges);
        }
      }
    }
    if (!main)
    {
      Log::Warn("Editor fonts not found in {}; using the built-in font (icons will show as boxes)", fonts.string());
      io.Fonts->AddFontDefault();
    }
    if (!boldFont)
      boldFont = io.Fonts->Fonts.front();

    ApplyTheme(s.settings.accent, static_cast<float>(s.settings.uiScale));
    ImGui::GetStyle().FontScaleMain = static_cast<float>(s.settings.uiScale);
    ui.layoutBuilt = std::filesystem::exists(iniPath, ec);
    ApplyWorkspace("Cinematic");
    Shown(Panel::Demos) = true;
    Shown(Panel::GettingStarted) = s.settings.onboarding;
    initialized = true;
  }

  ImFont* BoldFont()
  {
    return boldFont;
  }

  bool ViewportHovered()
  {
    return viewportHovered;
  }

  double TakeWheelFov()
  {
    return wheelFov.exchange(0.0);
  }

  void Draw()
  {
    auto& s = GetSession();
    std::lock_guard _(s.mutex);
    auto& io = ImGui::GetIO();

    // Hide the editor's cursor while flying.
    io.MouseDrawCursor = !Runtime::IsFlying();
    if (Runtime::IsFlying())
    {
      wheelFov = wheelFov.load() - io.MouseWheel * 2.0;
      io.MouseWheel = 0.0f;
    }

    DrawMenuBar(s);
    DrawToolbar(s);

    const auto* vp = ImGui::GetMainViewport();
    const float toolbar = ImGui::GetFrameHeight() + 14.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + toolbar));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - toolbar));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##dockhost", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ui.dockspace = ImGui::GetID("IW4IrisDock");
    if (!ui.layoutBuilt || ui.resetLayout)
    {
      BuildDefaultLayout(ui.dockspace);
      ui.layoutBuilt = true;
      ui.resetLayout = false;
    }
    ImGui::DockSpace(ui.dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    if (Shown(Panel::Outliner))
      DrawOutliner(s);
    if (Shown(Panel::Inspector))
      DrawInspector(s);
    if (Shown(Panel::Timeline))
    {
      if (ImGui::Begin(PanelName(Panel::Timeline), &Shown(Panel::Timeline)))
        DrawTimeline(s);
      ImGui::End();
    }
    if (Shown(Panel::Look))
      DrawLook(s);
    if (Shown(Panel::Demos))
      DrawDemos(s);
    if (Shown(Panel::Recording))
      DrawRecording(s);
    if (Shown(Panel::RenderQueue))
      DrawRenderQueue(s);
    if (Shown(Panel::Export))
      DrawExport(s);
    if (Shown(Panel::Presets))
      DrawPresets(s);
    if (Shown(Panel::Actors))
      DrawCharacters(s);
    if (Shown(Panel::Director))
      DrawDirector(s);
    if (Shown(Panel::Animations))
      DrawAnimations(s);
    if (Shown(Panel::Assets))
      DrawAssets(s);
    if (Shown(Panel::Graphics))
      DrawGraphics(s);
    if (Shown(Panel::ObjectInspector))
      DrawObjectInspector(s);
    DrawSceneWizard(s);
    if (Shown(Panel::Lighting))
      DrawLighting(s);
    if (Shown(Panel::Settings))
      DrawSettings(s);
    if (Shown(Panel::Diagnostics))
      DrawDiagnostics(s);
    if (Shown(Panel::History))
      DrawHistory(s);
    DrawShortcuts();
    DrawHome(s);
    if (!ui.homeOpen || !s.settings.showHome)
      DrawGettingStarted(s);
    DrawFilePopups(s);
    DrawNotification(s);
    DrawCommandPalette(s);
    DrawSceneOverlay(s);
    DrawOverlay(s);
    if (s.freeView && !Capture::IsActive())
    {
      // A banner so it is always clear the viewport is not the shot.
      const auto* mainViewport = ImGui::GetMainViewport();
      const char* text = "FREE VIEW  -  not the shot. V: back to the shot camera";
      const ImVec2 size = ImGui::CalcTextSize(text);
      const ImVec2 pos(mainViewport->WorkPos.x + (mainViewport->WorkSize.x - size.x) * 0.5f,
                       mainViewport->WorkPos.y + mainViewport->WorkSize.y * 0.12f);
      auto* dl = ImGui::GetBackgroundDrawList();
      dl->AddRectFilled(ImVec2(pos.x - 10, pos.y - 5), ImVec2(pos.x + size.x + 10, pos.y + size.y + 5), IM_COL32(0, 0, 0, 140), 4.0f);
      dl->AddText(pos, IM_COL32(255, 220, 140, 230), text);
    }
    HandleShortcuts(s);

    // The game view is "hovered" when the mouse is over no editor window.
    viewportHovered = !io.WantCaptureMouse || Runtime::IsFlying();
  }
}
