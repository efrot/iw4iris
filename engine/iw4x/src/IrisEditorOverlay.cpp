// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Camera paths, keys and frustums drawn over the game view. They are drawn
// by the editor after the capture has read the frame, so they never appear
// in recordings.
#include "EditorInternal.hpp"
#include "Runtime.hpp"

namespace IW4Iris::Editor
{
  namespace
  {
    struct Projector
    {
      iris::Vec3 position;
      iris::Mat3 axis;
      double tanX = 1.0, tanY = 1.0;
      ImVec2 size;

      // Screen position of a world point; none behind the camera.
      std::optional<ImVec2> Project(const iris::Vec3& p) const
      {
        const iris::Vec3 d = p - position;
        const double x = iris::Dot(d, axis.col[0]);
        if (x < 1.0)
          return std::nullopt;
        const double y = iris::Dot(d, axis.col[1]);
        const double z = iris::Dot(d, axis.col[2]);
        const double sx = 0.5 * size.x * (1.0 - (y / x) / tanX);
        const double sy = 0.5 * size.y * (1.0 - (z / x) / tanY);
        if (std::abs(sx) > 1e5 || std::abs(sy) > 1e5)
          return std::nullopt;
        return ImVec2(static_cast<float>(sx), static_cast<float>(sy));
      }
    };
  }

  void DrawOverlay(Session& s)
  {
    if (!s.settings.showPathOverlay)
      return;
    const auto view = Runtime::LastAppliedCamera();
    if (!view)
      return;
    const auto& io = ImGui::GetIO();
    Projector pr;
    pr.position = view->position;
    pr.axis = iris::AnglesToAxis(view->angles);
    pr.size = io.DisplaySize;
    pr.tanX = std::tan(iris::Clamp(view->horizontalFov, 1.0, 179.0) * 0.5 * iris::kDegToRad);
    pr.tanY = pr.tanX * (pr.size.y / std::max(1.0f, pr.size.x));
    auto* dl = ImGui::GetBackgroundDrawList();

    for (const auto& cam : s.project.cameras)
    {
      if (cam.mode != iris::CameraMode::Dolly || cam.path.Size() < 1)
        continue;
      if (const auto it = s.project.trackStates.find(cam.id); it != s.project.trackStates.end() && it->second.hidden)
        continue;
      const bool selected = cam.id == s.selectedCamera;
      const ImU32 color = ParseColor(cam.color, selected ? 0.95f : 0.45f);
      const auto samples = cam.path.Sample(32);
      std::optional<ImVec2> prev;
      for (const auto& smp : samples)
      {
        const auto p = pr.Project(smp.position);
        if (p && prev)
          dl->AddLine(*prev, *p, color, selected ? 2.0f : 1.2f);
        prev = p;
      }
      const auto& keys = cam.path.Keys();
      for (std::size_t i = 0; i < keys.size(); ++i)
      {
        const auto& k = keys[i];
        const auto p = pr.Project(k.position);
        if (!p)
          continue;
        const bool sel = selected && s.selectedKeys.contains(i);
        const float r = sel ? 6.0f : 4.5f;
        dl->AddQuadFilled(ImVec2(p->x, p->y - r), ImVec2(p->x + r, p->y), ImVec2(p->x, p->y + r), ImVec2(p->x - r, p->y),
                          sel ? ImGui::GetColorU32(CurrentPalette().accentStrong) : color);
        if (s.settings.showFrustums && selected)
        {
          // A small frustum showing where the key looks.
          const auto axis = iris::AnglesToAxis(k.angles);
          const double len = 24.0;
          const double tx = std::tan(k.horizontalFov * 0.5 * iris::kDegToRad) * len;
          const double ty = tx * 9.0 / 16.0;
          const iris::Vec3 tip = k.position;
          const iris::Vec3 c = tip + axis.col[0] * len;
          const iris::Vec3 corners[4] = {c + axis.col[1] * tx + axis.col[2] * ty, c - axis.col[1] * tx + axis.col[2] * ty,
                                         c - axis.col[1] * tx - axis.col[2] * ty, c + axis.col[1] * tx - axis.col[2] * ty};
          std::optional<ImVec2> pc[4];
          for (int j = 0; j < 4; ++j)
            pc[j] = pr.Project(corners[j]);
          for (int j = 0; j < 4; ++j)
          {
            if (pc[j])
              dl->AddLine(*p, *pc[j], color, 1.0f);
            if (pc[j] && pc[(j + 1) % 4])
              dl->AddLine(*pc[j], *pc[(j + 1) % 4], color, 1.0f);
          }
        }
        char label[16];
        std::snprintf(label, sizeof(label), "%zu", i + 1);
        dl->AddText(ImVec2(p->x + 7, p->y - 7), IM_COL32(230, 230, 235, 200), label);
      }
      // Where the camera is at the playhead.
      if (const auto now = cam.path.Evaluate(s.playhead))
        if (const auto p = pr.Project(now->position))
          dl->AddCircle(*p, 7.0f, ImGui::GetColorU32(CurrentPalette().accentStrong), 16, 2.0f);
    }

    // Targets of orbit / follow / static cameras.
    for (const auto& cam : s.project.cameras)
      if (cam.target.kind == iris::TargetKind::Point)
        if (const auto p = pr.Project(cam.target.point))
        {
          dl->AddCircle(*p, 6.0f, ParseColor(cam.color, 0.8f), 12, 1.5f);
          dl->AddLine(ImVec2(p->x - 9, p->y), ImVec2(p->x + 9, p->y), ParseColor(cam.color, 0.8f));
          dl->AddLine(ImVec2(p->x, p->y - 9), ImVec2(p->x, p->y + 9), ParseColor(cam.color, 0.8f));
        }
  }
}
