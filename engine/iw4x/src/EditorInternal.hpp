// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Editor internals shared by the editor's source files (render thread only).
#pragma once

#include "Director.hpp"
#include "Editor.hpp"
#include "Icons.hpp"
#include "Session.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <string>
#include <vector>

namespace IW4Iris::Editor
{
  enum class Panel
  {
    Outliner,
    Inspector,
    Timeline,
    Look,
    Demos,
    Recording,
    RenderQueue,
    Export,
    Presets,
    Actors,
    Lighting,
    Settings,
    Diagnostics,
    Shortcuts,
    GettingStarted,
    History,
    Director,        // Director Mode home: film set, wizard, templates
    Animations,      // animation library
    Assets,          // props and effects
    Graphics,        // motion graphics: add / list / renderer status
    ObjectInspector, // the selected actor / prop / effect / graphic
    Players,         // the demo's players: select, watch, cut to
    Count,
  };

  struct TimelineView
  {
    double start = 0.0;       // visible range, seconds
    double end = 10.0;
    bool graph = false;       // graph editor instead of the dope sheet
    bool fitRequested = true;
    // dragging
    bool draggingKeys = false;
    bool boxSelecting = false;
    ImVec2 boxStart;
    double dragStartTime = 0.0;
    std::vector<double> dragOriginalTimes;
    std::string dragCamera;
    bool scrubbing = false;
    int draggingMarker = -1;
    int graphDragKey = -1;
    int graphDragHandle = 0;  // 0 key, -1 in-handle, +1 out-handle
    double graphMin = 0.0, graphMax = 1.0;
    // navigation
    bool follow = true;       // keep the playhead in view while playing
    bool magnet = true;       // snap drags to markers, keys, cuts, in/out (Alt: off)
    float scrollY = 0.0f;     // rows scrolled up, pixels
    float contentHeight = 0.0f;  // height of all rows (last frame)
    int rangeDrag = 0;        // 0 none, 1 in, 2 out, 3 move, 4 new range
    double rangeAnchor = 0.0; // press time (new range) / offset (move)
    double rangeOrigIn = 0.0, rangeOrigOut = 0.0;
    int overviewDrag = 0;     // 0 none, 1 pan, 2 left edge, 3 right edge
    double overviewGrab = 0.0;
    double overviewA = 0.0, overviewB = 1.0;  // range the overview showed when the drag began
    bool vScrollDrag = false;
    bool pendingClick = false;  // camera row: press on empty space, a click unless it becomes a box
    double lastLiveSeek = 0.0;  // ImGui time of the last seek while scrubbing a demo
    double lastScrubTime = -1.0;
    double scrubRestTime = 0.0;
    char timeText[32]{}, inText[32]{}, outText[32]{};
    bool timeEdit = false, inEdit = false, outEdit = false;
  };

  struct GameView
  {
    bool active = false;  // the game is shown scaled (else: full screen, identity mapping)
    ImVec2 min{};         // top-left of the image on screen
    ImVec2 size{};        // its size on screen
    float scale = 1.0f;   // screen pixels per game pixel
  };

  struct UiState
  {
    bool show[static_cast<int>(Panel::Count)]{};
    std::string workspace = "Cinematic";
    bool layoutBuilt = false;
    bool resetLayout = false;
    ImGuiID dockspace = 0;
    TimelineView timeline;

    std::string demoSearch;
    bool demoFavoritesOnly = false;
    std::string newDemoFolder;
    bool demoScanRunning = false;

    std::string openPath;
    std::string saveAsPath;
    bool openPopup = false;
    bool homeOpen = true;     // the start screen (closed once something is chosen or loaded)
    GameView gameView;        // where the game picture is shown this frame
    bool viewportClickConsumed = false;  // a player name took this click (no object selection then)
    bool saveAsPopup = false;
    std::string message;          // transient status line
    double messageTime = 0.0;

    std::vector<iris::CameraKey> clipboard;
    double clipboardOrigin = 0.0;

    // presets
    int presetIndex = 0;
    int presetSubject = 0;        // 0 view target, 1 player
    int presetPlayer = 0;
    double presetDuration = 4.0;
    bool presetNewCamera = true;

    // export
    int exportFormat = 0;         // 0 Blender, 1 After Effects, 2 CSV
    std::string lastExport;

    // recording validation cache
    double lastCheck = -10.0;
    std::vector<iris::Issue> issues;
    bool ffmpegFound = false;
    std::string ffmpegPathFound;
    bool queueRunning = false;

    // Director Mode
    bool wizardOpen = false;
    int wizardStep = 0;
    std::string mapSearch;
    bool mapFavoritesOnly = false;
    std::string wizardMap;
    std::string wizardTemplate = "empty";
    std::vector<Director::MapInfo> maps;
    bool mapsRequested = false;
    Director::Catalog catalog;
    int catalogVersion = -1;
    std::string characterSearch, characterCategory;
    bool characterFavorites = false;
    std::string animationSearch, animationCategory;
    std::string assetSearch, assetCategory;
    int assetTab = 0;             // 0 props, 1 effects
    int shotType = 1;
    double shotDuration = 5.0;
    std::string titleText = "IW4IRIS";
    int titleTemplate = 0;
    std::vector<std::string> fonts;
    double fontsListedAt = -100.0;
    std::string pendingFile;      // a file chosen in the open dialog, for `pendingFileFor`
    std::string pendingFileFor;

    // Timeline: dragging scene clips / bars
    std::string objDragId;
    int objDragIndex = -1;
    int objDragMode = 0;          // 0 move, 1 resize end
    double objDragOffset = 0.0;

    // Viewport gizmo
    int gizmoAxis = -1;           // 0 x, 1 y, 2 z, 3 screen plane, 4 yaw ring, 5 uniform scale
    bool gizmoDragging = false;
    ImVec2 gizmoMouseStart;
    iris::Transform gizmoStart;
  };

  UiState& Ui();
  void Notify(std::string message);

  // The game view. With the editor shown, the game frame is drawn scaled
  // into the free space between the panels (the dock's central node).
  // Overlays are drawn in game coordinates (0..display size, what the
  // projection maths gives) between BeginGameSpace / EndGameSpace, which
  // maps them onto the shown image; mouse positions go the other way.
  const GameView& CurrentGameView();
  ImVec2 GameToScreen(ImVec2 p);
  ImVec2 ScreenToGame(ImVec2 p);
  ImVec2 GameMouse();       // the mouse in game coordinates
  bool MouseOverGame();     // the mouse is over the game image
  struct GameSpace
  {
    int vtx = 0;
    int cmd = 0;
  };
  GameSpace BeginGameSpace(ImDrawList* dl);
  void EndGameSpace(ImDrawList* dl, const GameSpace& space);

  // Panels (each locks nothing: Draw() holds the session lock)
  void DrawTimeline(Session& s);
  void DrawOverlay(Session& s);
  bool& Shown(Panel p);
  const char* PanelName(Panel p);

  // Director Mode (IrisEditorDirector.cpp)
  void DrawDirector(Session& s);
  void DrawCharacters(Session& s);
  void DrawAnimations(Session& s);
  void DrawAssets(Session& s);
  void DrawGraphics(Session& s);
  void DrawObjectInspector(Session& s);
  void DrawSceneObjects(Session& s);  // section of the Scene panel
  void DrawSceneWizard(Session& s);
  void DrawSceneOverlay(Session& s);  // viewport: objects, selection, gizmo, guides
  bool HandleObjectShortcuts(Session& s);
  void SelectObject(Session& s, const std::string& id);
  void DeleteObject(Session& s, const std::string& id);
  void DuplicateObject(Session& s, const std::string& id);
  bool ObjectExists(const Session& s, const std::string& id);
  // Where a new object goes: the surface the camera looks at (film sets),
  // else in front of the camera.
  iris::Vec3 PlacementPoint(Session& s, double distance = 220.0);

  // Shared actions (render thread; game work is posted to the main thread)
  void RequestSeek(Session& s, double t);
  // Navigation camera vs shot camera.
  void ShowShot(Session& s);           // leave the free view: the viewport shows the shot again
  void CameraFromView(Session& s);     // a new static camera where the view is, made active
  void MoveCameraToView(Session& s);   // the selected (or active) camera takes the view
  // Timeline navigation shared with the shortcuts.
  std::vector<double> EditPoints(const Session& s);  // markers, cuts, keys, clip ends, in/out ...
  void ZoomTimeline(Session& s, double factor);       // around the playhead
  void FitTimeline(Session& s);
  void SetInPoint(Session& s, double t);
  void SetOutPoint(Session& s, double t);
  void TogglePlay(Session& s);
  void StepFrames(Session& s, int frames);
  void DeleteSelectedKeys(Session& s);
  void CopySelectedKeys(Session& s);
  void PasteKeys(Session& s);
  void AddMarker(Session& s);
  void SaveProject(Session& s);        // Ctrl+S: save, or "save as" for a new project
  void UseWorkspace(const std::string& name);
  std::vector<std::string> WorkspaceNames();
  // Quick creation (IrisEditorDirector.cpp)
  void QuickAddText(Session& s);       // a 3D title in front of the camera, selected
  void QuickImportLogo(Session& s);    // file picker; the logo is added when a file is chosen
  // Command palette (IrisEditorPalette.cpp): Ctrl+K
  void OpenCommandPalette();
  // Diagnostics (IrisEditorDiagnostics.cpp): capability states, self-test, support bundle
  void DrawDiagnosticsOverview(Session& s);
  // Demo players (IrisEditorPlayers.cpp)
  void DrawPlayers(Session& s);
  void DrawPlayerTags(Session& s);          // viewport: names over players, click to select
  void StepPlayer(Session& s, int direction);  // next / previous connected player
  // Makes (or reuses) a camera of `mode` on the selected player and shows it;
  // with `cut`, also adds a camera cut to it at the playhead. Returns its id.
  std::string WatchSelectedPlayer(Session& s, iris::CameraMode mode, bool cut);
  void DrawCommandPalette(Session& s);

  // Widgets
  bool DragDouble(const char* label, double* v, float speed, double min, double max, const char* format = "%.3f");
  bool SliderDouble(const char* label, double* v, double min, double max, const char* format = "%.3f");
  bool DragVec3(const char* label, iris::Vec3* v, float speed, const char* format = "%.2f");
  bool ColorVec3(const char* label, iris::Vec3* v);
  void HelpMarker(const char* text);
  void SectionHeader(const char* icon, const char* title);
  ImU32 ParseColor(const std::string& hex, float alpha = 1.0f);
  bool IconButton(const char* icon, const char* tooltip, bool active = false);
  void Unavailable(const char* what, const char* why);
}
