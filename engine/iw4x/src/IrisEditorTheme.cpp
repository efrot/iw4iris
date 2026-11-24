// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Editor.hpp"

namespace IW4Iris::Editor
{
  namespace
  {
    struct Accent
    {
      const char* name;
      ImVec4 color;
    };

    const Accent kAccents[] = {
      {"Iris violet", ImVec4(0.545f, 0.486f, 0.965f, 1.0f)},
      {"Steel blue", ImVec4(0.369f, 0.631f, 0.969f, 1.0f)},
      {"Teal", ImVec4(0.231f, 0.741f, 0.702f, 1.0f)},
      {"Amber", ImVec4(0.969f, 0.725f, 0.333f, 1.0f)},
      {"Rose", ImVec4(0.933f, 0.408f, 0.553f, 1.0f)},
    };

    Palette palette;

    ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
    {
      return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    }

    ImVec4 Alpha(ImVec4 c, float a)
    {
      c.w = a;
      return c;
    }
  }

  const Palette& CurrentPalette()
  {
    return palette;
  }

  int AccentCount()
  {
    return static_cast<int>(std::size(kAccents));
  }

  const char* AccentName(int accent)
  {
    return kAccents[std::clamp(accent, 0, AccentCount() - 1)].name;
  }

  void ApplyTheme(int accentIndex, float scale)
  {
    const ImVec4 accent = kAccents[std::clamp(accentIndex, 0, AccentCount() - 1)].color;
    palette.accent = accent;
    palette.accentSoft = Alpha(accent, 0.35f);
    palette.accentStrong = Mix(accent, ImVec4(1, 1, 1, 1), 0.25f);

    // Graphite / charcoal surfaces
    const ImVec4 bg0(0.071f, 0.075f, 0.086f, 0.97f);   // window
    const ImVec4 bg1(0.098f, 0.102f, 0.118f, 1.00f);   // child / frame
    const ImVec4 bg2(0.133f, 0.137f, 0.157f, 1.00f);   // hover
    const ImVec4 bg3(0.169f, 0.173f, 0.200f, 1.00f);   // active
    const ImVec4 line(0.200f, 0.208f, 0.239f, 1.00f);
    const ImVec4 text(0.886f, 0.894f, 0.918f, 1.00f);
    const ImVec4 textDim(0.525f, 0.541f, 0.584f, 1.00f);

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10, 9);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 9.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.DockingSeparatorSize = 2.0f;

    auto* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = bg0;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.090f, 0.106f, 0.98f);
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = bg1;
    c[ImGuiCol_FrameBgHovered] = bg2;
    c[ImGuiCol_FrameBgActive] = bg3;
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg0;
    c[ImGuiCol_TitleBgCollapsed] = bg0;
    c[ImGuiCol_MenuBarBg] = ImVec4(0.059f, 0.063f, 0.071f, 0.98f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = Mix(bg3, accent, 0.3f);
    c[ImGuiCol_ScrollbarGrabActive] = Mix(bg3, accent, 0.5f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = Mix(accent, bg3, 0.2f);
    c[ImGuiCol_SliderGrabActive] = palette.accentStrong;
    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = Mix(bg2, accent, 0.35f);
    c[ImGuiCol_ButtonActive] = Mix(bg2, accent, 0.55f);
    c[ImGuiCol_Header] = Alpha(accent, 0.22f);
    c[ImGuiCol_HeaderHovered] = Alpha(accent, 0.32f);
    c[ImGuiCol_HeaderActive] = Alpha(accent, 0.45f);
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_SeparatorHovered] = Alpha(accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = Alpha(accent, 0.5f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = bg0;
    c[ImGuiCol_TabHovered] = Mix(bg2, accent, 0.25f);
    c[ImGuiCol_TabSelected] = bg2;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg0;
    c[ImGuiCol_TabDimmedSelected] = bg1;
    c[ImGuiCol_TabDimmedSelectedOverline] = Alpha(accent, 0.4f);
    c[ImGuiCol_DockingPreview] = Alpha(accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = bg1;
    c[ImGuiCol_TableBorderStrong] = line;
    c[ImGuiCol_TableBorderLight] = Alpha(line, 0.6f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TextSelectedBg] = Alpha(accent, 0.35f);
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
  }
}
