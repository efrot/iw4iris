// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The editor UI (Dear ImGui, docking). Built on the render thread between
// ImGui::NewFrame and ImGui::Render; it reads and edits the Session and
// posts anything that touches the game to the main thread.
#pragma once

#include <imgui.h>

namespace IW4Iris::Editor
{
  void Initialize();  // fonts, theme, ini location (render thread, context current)
  // The finished game frame (copied before the editor draws), so the
  // editor can show it scaled into the free space between its panels
  // instead of under them. Render thread; null when there is no copy.
  void SetGameImage(void* texture, int width, int height, void* device);
  void Draw();

  // Thread-safe queries for the main thread.
  bool ViewportHovered();
  double TakeWheelFov();

  // Theme
  struct Palette
  {
    ImVec4 accent;
    ImVec4 accentSoft;
    ImVec4 accentStrong;
  };
  const Palette& CurrentPalette();
  void ApplyTheme(int accent, float scale);
  const char* AccentName(int accent);
  int AccentCount();

  ImFont* BoldFont();
}
