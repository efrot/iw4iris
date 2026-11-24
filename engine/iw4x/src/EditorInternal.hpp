// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Editor internals shared by the editor's source files (render thread only).
#pragma once

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
  };

  UiState& Ui();
  void Notify(std::string message);

  // Panels (each locks nothing: Draw() holds the session lock)
  void DrawTimeline(Session& s);
  void DrawOverlay(Session& s);

  // Shared actions (render thread; game work is posted to the main thread)
  void RequestSeek(Session& s, double t);
  void TogglePlay(Session& s);
  void StepFrames(Session& s, int frames);
  void DeleteSelectedKeys(Session& s);
  void CopySelectedKeys(Session& s);
  void PasteKeys(Session& s);
  void AddMarker(Session& s);

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
