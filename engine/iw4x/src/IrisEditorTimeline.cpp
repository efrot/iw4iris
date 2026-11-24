// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "EditorInternal.hpp"
#include "Runtime.hpp"

#include <cmath>

namespace IW4Iris::Editor
{
  namespace
  {
    constexpr float kLabelWidth = 178.0f;
    constexpr float kRulerHeight = 24.0f;
    constexpr float kRowHeight = 22.0f;
    constexpr const char* kSpeedTrack = "@speed";

    struct Canvas
    {
      ImVec2 min, max;
      double start, end;
      float X(double t) const { return min.x + static_cast<float>((t - start) / (end - start)) * (max.x - min.x); }
      double T(float x) const { return start + (x - min.x) / std::max(1.0f, max.x - min.x) * (end - start); }
    };

    double NiceStep(double span, float width)
    {
      const double target = span / std::max(1.0, static_cast<double>(width) / 90.0);
      const double steps[] = {1.0 / 120, 1.0 / 60, 1.0 / 30, 1.0 / 12, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
      for (double s : steps)
        if (s >= target)
          return s;
      return 1200.0;
    }

    void Diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border)
    {
      dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), fill);
      dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), border, 1.0f);
    }

    double Snap(const Session& s, double t)
    {
      return s.project.timeline.snapToFrames ? s.project.timeline.frameRate.SnapSeconds(t) : t;
    }

    void FitView(Session& s)
    {
      auto& v = Ui().timeline;
      const auto& tl = s.project.timeline;
      v.start = tl.start;
      v.end = std::max(tl.end, s.project.ContentEnd() + 0.5);
      if (v.end - v.start < 1.0)
        v.end = v.start + 10.0;
    }

    iris::ScalarCurve* SelectedCurve(Session& s, std::string* name)
    {
      if (s.selectedTrack == kSpeedTrack)
      {
        *name = "Playback speed";
        return &s.project.timeRemap.Speed();
      }
      if (auto* t = s.project.FindTrack(s.selectedTrack))
      {
        *name = t->property;
        return &t->curve;
      }
      return nullptr;
    }

    // ---------------------------------------------------------- graph editor

    void DrawGraph(Session& s, const Canvas& c, ImDrawList* dl)
    {
      auto& v = Ui().timeline;
      std::string name;
      auto* curve = SelectedCurve(s, &name);
      if (!curve)
      {
        dl->AddText(ImVec2(c.min.x + 12, c.min.y + 12), IM_COL32(150, 150, 160, 255),
                    "Select a property track (or the playback-speed track) to edit its curve.");
        return;
      }
      auto& keys = curve->MutableKeys();
      // Value range
      double lo = curve->DefaultValue(), hi = curve->DefaultValue();
      for (int i = 0; i <= 200; ++i)
      {
        const double val = curve->Evaluate(c.start + (c.end - c.start) * i / 200.0);
        lo = std::min(lo, val);
        hi = std::max(hi, val);
      }
      for (const auto& k : keys)
      {
        lo = std::min(lo, k.value);
        hi = std::max(hi, k.value);
      }
      if (hi - lo < 1e-3)
      {
        lo -= 0.5;
        hi += 0.5;
      }
      const double pad = (hi - lo) * 0.12;
      if (v.graphDragKey < 0)
      {
        v.graphMin = lo - pad;
        v.graphMax = hi + pad;
      }
      auto Y = [&](double value) {
        return c.max.y - static_cast<float>((value - v.graphMin) / (v.graphMax - v.graphMin)) * (c.max.y - c.min.y);
      };
      auto V = [&](float y) { return v.graphMin + (c.max.y - y) / std::max(1.0f, c.max.y - c.min.y) * (v.graphMax - v.graphMin); };

      // grid
      for (int i = 0; i <= 4; ++i)
      {
        const double val = v.graphMin + (v.graphMax - v.graphMin) * i / 4.0;
        const float y = Y(val);
        dl->AddLine(ImVec2(c.min.x, y), ImVec2(c.max.x, y), IM_COL32(255, 255, 255, 18));
        char label[32];
        std::snprintf(label, sizeof(label), "%.3f", val);
        dl->AddText(ImVec2(c.min.x + 4, y - 14), IM_COL32(140, 140, 150, 255), label);
      }
      dl->AddText(ImVec2(c.min.x + 4, c.min.y + 4), ImGui::GetColorU32(CurrentPalette().accent), name.c_str());

      // curve
      const ImU32 curveColor = ImGui::GetColorU32(CurrentPalette().accent);
      ImVec2 prev;
      const int segments = static_cast<int>((c.max.x - c.min.x) / 3.0f);
      for (int i = 0; i <= segments; ++i)
      {
        const double t = c.start + (c.end - c.start) * i / segments;
        const ImVec2 p(c.X(t), Y(curve->Evaluate(t)));
        if (i > 0)
          dl->AddLine(prev, p, curveColor, 2.0f);
        prev = p;
      }

      // keys and handles
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const bool hovered = ImGui::IsItemHovered();
      int hoverKey = -1, hoverHandle = 0;
      for (std::size_t i = 0; i < keys.size(); ++i)
      {
        auto& k = keys[i];
        const ImVec2 p(c.X(k.time), Y(k.value));
        const bool handles = k.interpolation == iris::Interpolation::Bezier || k.tangentMode == iris::TangentMode::Free ||
                             k.tangentMode == iris::TangentMode::Aligned;
        if (handles)
        {
          const ImVec2 hi(c.X(k.time + k.inHandle.dt), Y(k.value + k.inHandle.dv));
          const ImVec2 ho(c.X(k.time + k.outHandle.dt), Y(k.value + k.outHandle.dv));
          dl->AddLine(hi, p, IM_COL32(200, 200, 210, 120));
          dl->AddLine(p, ho, IM_COL32(200, 200, 210, 120));
          dl->AddCircleFilled(hi, 4.0f, IM_COL32(220, 220, 230, 255));
          dl->AddCircleFilled(ho, 4.0f, IM_COL32(220, 220, 230, 255));
          if (hovered && ImLengthSqr(ImVec2(mouse.x - hi.x, mouse.y - hi.y)) < 36.0f)
          {
            hoverKey = static_cast<int>(i);
            hoverHandle = -1;
          }
          if (hovered && ImLengthSqr(ImVec2(mouse.x - ho.x, mouse.y - ho.y)) < 36.0f)
          {
            hoverKey = static_cast<int>(i);
            hoverHandle = 1;
          }
        }
        Diamond(dl, p, 5.5f, v.graphDragKey == static_cast<int>(i) ? ImGui::GetColorU32(CurrentPalette().accentStrong) : IM_COL32(235, 235, 240, 255),
                IM_COL32(20, 20, 24, 255));
        if (hovered && hoverHandle == 0 && ImLengthSqr(ImVec2(mouse.x - p.x, mouse.y - p.y)) < 49.0f)
          hoverKey = static_cast<int>(i);
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverKey >= 0)
      {
        v.graphDragKey = hoverKey;
        v.graphDragHandle = hoverHandle;
      }
      if (v.graphDragKey >= 0 && v.graphDragKey < static_cast<int>(keys.size()))
      {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          auto& k = keys[static_cast<std::size_t>(v.graphDragKey)];
          const double t = c.T(mouse.x);
          const double val = V(mouse.y);
          if (v.graphDragHandle == 0)
          {
            k.value = val;
            const double nt = Snap(s, t);
            const double before = v.graphDragKey > 0 ? keys[static_cast<std::size_t>(v.graphDragKey) - 1].time + 1e-3 : -1e9;
            const double after = v.graphDragKey + 1 < static_cast<int>(keys.size()) ? keys[static_cast<std::size_t>(v.graphDragKey) + 1].time - 1e-3 : 1e9;
            k.time = std::clamp(nt, before, after);
          }
          else
          {
            auto& h = v.graphDragHandle < 0 ? k.inHandle : k.outHandle;
            h.dt = v.graphDragHandle < 0 ? std::min(t - k.time, -1e-3) : std::max(t - k.time, 1e-3);
            h.dv = val - k.value;
            if (k.tangentMode == iris::TangentMode::Auto || k.tangentMode == iris::TangentMode::Flat)
              k.tangentMode = iris::TangentMode::Aligned;
            if (k.interpolation != iris::Interpolation::Bezier)
              k.interpolation = iris::Interpolation::Bezier;
            if (k.tangentMode == iris::TangentMode::Aligned)
            {
              // keep the other handle colinear
              auto& o = v.graphDragHandle < 0 ? k.outHandle : k.inHandle;
              const double len = std::sqrt(o.dt * o.dt + o.dv * o.dv);
              const double hl = std::sqrt(h.dt * h.dt + h.dv * h.dv);
              if (hl > 1e-9)
              {
                o.dt = -h.dt / hl * len;
                o.dv = -h.dv / hl * len;
              }
            }
          }
          curve->UpdateAutoHandles();
        }
        else
        {
          v.graphDragKey = -1;
          s.Commit("Edit curve");
        }
      }

      // context menu
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        ImGui::OpenPopup("graph-key");
        v.graphDragHandle = hoverKey;  // remembered key for the popup
      }
      if (ImGui::BeginPopup("graph-key"))
      {
        const int k = v.graphDragHandle;
        if (k >= 0 && k < static_cast<int>(keys.size()))
        {
          auto& key = keys[static_cast<std::size_t>(k)];
          const char* interps[] = {"Hold", "Linear", "Smooth", "Catmull-Rom", "Bezier"};
          for (int i = 0; i < 5; ++i)
            if (ImGui::MenuItem(interps[i], nullptr, static_cast<int>(key.interpolation) == i))
            {
              key.interpolation = static_cast<iris::Interpolation>(i);
              s.Commit("Key interpolation");
            }
          ImGui::Separator();
          const char* tangents[] = {"Auto tangents", "Aligned handles", "Free handles", "Flat"};
          for (int i = 0; i < 4; ++i)
            if (ImGui::MenuItem(tangents[i], nullptr, static_cast<int>(key.tangentMode) == i))
            {
              key.tangentMode = static_cast<iris::TangentMode>(i);
              curve->UpdateAutoHandles();
              s.Commit("Key tangents");
            }
          ImGui::Separator();
          if (ImGui::MenuItem("Delete key"))
          {
            curve->RemoveKeyAt(static_cast<std::size_t>(k));
            s.Commit("Delete curve key");
          }
        }
        else
        {
          if (ImGui::MenuItem("Add key at the mouse"))
          {
            iris::ScalarKey nk;
            nk.time = Snap(s, c.T(ImGui::GetMousePosOnOpeningCurrentPopup().x));
            nk.value = V(ImGui::GetMousePosOnOpeningCurrentPopup().y);
            curve->AddKey(nk);
            curve->UpdateAutoHandles();
            s.Commit("Add curve key");
          }
        }
        ImGui::EndPopup();
      }
    }
  }

  void DrawTimeline(Session& s)
  {
    auto& v = Ui().timeline;
    auto& p = s.project;
    auto& tl = p.timeline;
    if (v.fitRequested)
    {
      FitView(s);
      v.fitRequested = false;
    }

    // Header
    if (IconButton(Icons::EXPAND, "Fit the timeline"))
      FitView(s);
    ImGui::SameLine();
    if (IconButton(Icons::CHART, "Graph editor (G)", v.graph))
      v.graph = !v.graph;
    ImGui::SameLine();
    if (IconButton(Icons::FLAG, "Add marker (M)"))
      AddMarker(s);
    ImGui::SameLine();
    if (IconButton(Icons::SCISSORS, "Add a cut to the active camera at the playhead"))
    {
      p.cuts.AddCut({s.playhead, p.activeCameraId});
      p.useCameraCuts = true;
      s.Commit("Add camera cut");
    }
    ImGui::SameLine();
    if (IconButton(Icons::CLOCK, "Add a playback-speed key at the playhead (speed ramps, slow motion)"))
    {
      iris::ScalarKey k;
      k.time = Snap(s, s.playhead);
      k.value = p.timeRemap.SpeedAt(s.playhead);
      p.timeRemap.Speed().AddKey(k);
      p.timeRemap.Speed().UpdateAutoHandles();
      s.selectedTrack = kSpeedTrack;
      s.Commit("Add speed key");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &tl.snapToFrames);
    ImGui::SameLine();
    if (ImGui::Checkbox("Loop", &tl.loop.enabled))
    {
      if (tl.loop.end <= tl.loop.start)
      {
        tl.loop.start = tl.inPoint;
        tl.loop.end = tl.outPoint;
      }
      s.Commit("Loop region");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("In %s  Out %s  |  %s FPS", iris::FormatTime(tl.inPoint, tl.frameRate, tl.display).c_str(),
                        iris::FormatTime(tl.outPoint, tl.frameRate, tl.display).c_str(), tl.frameRate.ToString().c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    double length = tl.end - tl.start;
    if (DragDouble("##length", &length, 0.1f, 1.0, 36000.0, "Length %.1f s"))
    {
      tl.end = tl.start + length;
      tl.Normalize();
      s.Commit("Timeline length", true);
    }

    // Body: labels | canvas
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < kLabelWidth + 50 || avail.y < kRulerHeight + 20)
      return;
    auto* dl = ImGui::GetWindowDrawList();
    Canvas c{ImVec2(origin.x + kLabelWidth, origin.y), ImVec2(origin.x + avail.x, origin.y + avail.y), v.start, v.end};
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(14, 15, 18, 255));
    dl->AddRectFilled(origin, ImVec2(origin.x + kLabelWidth, origin.y + avail.y), IM_COL32(19, 20, 24, 255));

    // The whole canvas is one item: hit-testing below is manual.
    ImGui::SetCursorScreenPos(c.min);
    ImGui::InvisibleButton("##canvas", ImVec2(c.max.x - c.min.x, c.max.y - c.min.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    // Zoom (wheel) and pan (middle drag / shift+wheel)
    if (hovered && io.MouseWheel != 0.0f)
    {
      const double span = v.end - v.start;
      if (io.KeyShift)
      {
        const double d = -io.MouseWheel * span * 0.1;
        v.start += d;
        v.end += d;
      }
      else
      {
        const double pivot = c.T(mouse.x);
        const double factor = io.MouseWheel > 0 ? 0.85 : 1.0 / 0.85;
        v.start = pivot - (pivot - v.start) * factor;
        v.end = pivot + (v.end - pivot) * factor;
      }
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
      const double d = -io.MouseDelta.x / std::max(1.0f, c.max.x - c.min.x) * (v.end - v.start);
      v.start += d;
      v.end += d;
    }
    if (v.end - v.start < 0.05)
      v.end = v.start + 0.05;
    c.start = v.start;
    c.end = v.end;

    dl->PushClipRect(c.min, c.max, true);

    // In/out and loop shading
    dl->AddRectFilled(ImVec2(c.X(tl.inPoint), c.min.y), ImVec2(c.X(tl.outPoint), c.max.y), IM_COL32(255, 255, 255, 8));
    if (tl.loop.enabled)
      dl->AddRectFilled(ImVec2(c.X(tl.loop.start), c.min.y), ImVec2(c.X(tl.loop.end), c.min.y + kRulerHeight),
                        ImGui::GetColorU32(CurrentPalette().accentSoft));
    dl->AddRectFilled(ImVec2(c.min.x, c.min.y), ImVec2(c.X(tl.start), c.max.y), IM_COL32(0, 0, 0, 90));
    dl->AddRectFilled(ImVec2(c.X(tl.end), c.min.y), ImVec2(c.max.x, c.max.y), IM_COL32(0, 0, 0, 90));

    // Ruler
    const double step = NiceStep(v.end - v.start, c.max.x - c.min.x);
    for (double t = std::floor(v.start / step) * step; t <= v.end; t += step)
    {
      const float x = c.X(t);
      dl->AddLine(ImVec2(x, c.min.y + kRulerHeight * 0.55f), ImVec2(x, c.max.y), IM_COL32(255, 255, 255, 14));
      dl->AddLine(ImVec2(x, c.min.y + kRulerHeight * 0.55f), ImVec2(x, c.min.y + kRulerHeight), IM_COL32(170, 170, 180, 160));
      dl->AddText(ImVec2(x + 3, c.min.y + 2), IM_COL32(150, 150, 160, 255),
                  iris::FormatTime(t, tl.frameRate, tl.display).c_str());
    }
    dl->AddLine(ImVec2(c.min.x, c.min.y + kRulerHeight), ImVec2(c.max.x, c.min.y + kRulerHeight), IM_COL32(60, 62, 72, 255));
    dl->AddLine(ImVec2(c.X(tl.inPoint), c.min.y), ImVec2(c.X(tl.inPoint), c.max.y), ImGui::GetColorU32(CurrentPalette().accentSoft));
    dl->AddLine(ImVec2(c.X(tl.outPoint), c.min.y), ImVec2(c.X(tl.outPoint), c.max.y), ImGui::GetColorU32(CurrentPalette().accentSoft));

    // Scrubbing on the ruler
    const bool onRuler = mouse.y < c.min.y + kRulerHeight;
    if (hovered && onRuler && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      v.scrubbing = true;
    if (v.scrubbing)
    {
      s.scrubbing = true;
      s.playhead = std::clamp(Snap(s, c.T(mouse.x)), tl.start, tl.end);
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        v.scrubbing = false;
        s.scrubbing = false;
        RequestSeek(s, s.playhead);
      }
    }

    float y = c.min.y + kRulerHeight;
    auto label = [&](const char* icon, const std::string& text, ImU32 color, bool selected) {
      if (selected)
        dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + kLabelWidth, y + kRowHeight), IM_COL32(255, 255, 255, 14));
      dl->PopClipRect();
      dl->AddText(ImVec2(origin.x + 8, y + 3), color, icon);
      dl->AddText(ImVec2(origin.x + 28, y + 3), IM_COL32(210, 212, 220, 255), text.c_str());
      dl->PushClipRect(c.min, c.max, true);
      dl->AddLine(ImVec2(c.min.x, y + kRowHeight), ImVec2(c.max.x, y + kRowHeight), IM_COL32(255, 255, 255, 10));
    };
    auto labelClicked = [&]() {
      return ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= origin.x && mouse.x < c.min.x && mouse.y >= y &&
             mouse.y < y + kRowHeight && ImGui::IsWindowHovered();
    };

    if (v.graph)
    {
      Canvas g = c;
      g.min.y = y + 4;
      DrawGraph(s, g, dl);
      y = c.max.y;
    }
    else
    {
      // Markers row
      label(Icons::FLAG, "Markers", IM_COL32(247, 185, 85, 255), false);
      for (std::size_t i = 0; i < p.markers.size(); ++i)
      {
        auto& m = p.markers[i];
        const float x = c.X(m.time);
        const ImU32 col = ParseColor(m.color);
        dl->AddTriangleFilled(ImVec2(x - 5, y + 4), ImVec2(x + 5, y + 4), ImVec2(x, y + 14), col);
        dl->AddLine(ImVec2(x, y + 14), ImVec2(x, c.max.y), (col & 0x00FFFFFF) | 0x30000000);
        if (!m.name.empty())
          dl->AddText(ImVec2(x + 7, y + 3), IM_COL32(200, 200, 205, 200), m.name.c_str());
        const bool hit = hovered && std::abs(mouse.x - x) < 6 && mouse.y >= y && mouse.y < y + kRowHeight;
        if (hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          v.draggingMarker = static_cast<int>(i);
        if (hit && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          RequestSeek(s, m.time);
      }
      if (v.draggingMarker >= 0 && v.draggingMarker < static_cast<int>(p.markers.size()))
      {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
          p.markers[static_cast<std::size_t>(v.draggingMarker)].time = Snap(s, c.T(mouse.x));
        else
        {
          v.draggingMarker = -1;
          s.Commit("Move marker");
        }
      }
      y += kRowHeight;

      // Cuts row
      if (p.useCameraCuts || !p.cuts.Cuts().empty())
      {
        label(Icons::SCISSORS, "Camera cuts", IM_COL32(200, 200, 210, 255), false);
        const auto& cuts = p.cuts.Cuts();
        for (std::size_t i = 0; i < cuts.size(); ++i)
        {
          const double t0 = cuts[i].time;
          const double t1 = i + 1 < cuts.size() ? cuts[i + 1].time : tl.end;
          const auto* cam = p.FindCamera(cuts[i].cameraId);
          const ImU32 col = cam ? ParseColor(cam->color, 0.55f) : IM_COL32(120, 120, 120, 140);
          dl->AddRectFilled(ImVec2(c.X(t0) + 1, y + 3), ImVec2(c.X(t1) - 1, y + kRowHeight - 3), col, 3.0f);
          if (cam)
            dl->AddText(ImVec2(c.X(t0) + 5, y + 3), IM_COL32(240, 240, 245, 255), cam->name.c_str());
          if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouse.x >= c.X(t0) && mouse.x < c.X(t1) &&
              mouse.y >= y && mouse.y < y + kRowHeight)
          {
            ImGui::OpenPopup("cut-menu");
            v.draggingMarker = -100 - static_cast<int>(i);
          }
        }
        y += kRowHeight;
      }
      if (ImGui::BeginPopup("cut-menu"))
      {
        const int index = -100 - v.draggingMarker;
        auto cuts = p.cuts.Cuts();
        if (index >= 0 && index < static_cast<int>(cuts.size()))
        {
          for (const auto& cam : p.cameras)
            if (ImGui::MenuItem(cam.name.c_str(), nullptr, cam.id == cuts[static_cast<std::size_t>(index)].cameraId))
            {
              cuts[static_cast<std::size_t>(index)].cameraId = cam.id;
              p.cuts.SetCuts(cuts);
              s.Commit("Change cut camera");
            }
          ImGui::Separator();
          if (ImGui::MenuItem("Delete cut"))
          {
            p.cuts.RemoveCutAt(static_cast<std::size_t>(index));
            s.Commit("Delete cut");
          }
        }
        ImGui::EndPopup();
      }
      else if (v.draggingMarker <= -100)
        v.draggingMarker = -1;

      // Camera key rows
      for (auto& cam : p.cameras)
      {
        if (cam.mode != iris::CameraMode::Dolly)
          continue;
        auto& state = p.trackStates[cam.id];
        if (state.hidden)
          continue;
        const bool selectedCam = cam.id == s.selectedCamera;
        label(state.locked ? Icons::LOCK : Icons::CAMERA, cam.name, ParseColor(cam.color), selectedCam);
        if (labelClicked())
        {
          s.selectedCamera = cam.id;
          s.selectedKeys.clear();
        }
        const auto& keys = cam.path.Keys();
        // path extent bar
        if (keys.size() >= 2)
          dl->AddRectFilled(ImVec2(c.X(keys.front().time), y + kRowHeight * 0.5f - 1), ImVec2(c.X(keys.back().time), y + kRowHeight * 0.5f + 1),
                            ParseColor(cam.color, 0.5f));
        int hitKey = -1;
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
          const ImVec2 kp(c.X(keys[i].time), y + kRowHeight * 0.5f);
          const bool sel = selectedCam && s.selectedKeys.contains(i);
          Diamond(dl, kp, sel ? 6.5f : 5.5f, sel ? ImGui::GetColorU32(CurrentPalette().accentStrong) : ParseColor(cam.color),
                  IM_COL32(10, 10, 12, 255));
          if (hovered && std::abs(mouse.x - kp.x) < 7 && std::abs(mouse.y - kp.y) < 8)
            hitKey = static_cast<int>(i);
        }
        const bool rowHovered = hovered && mouse.y >= y && mouse.y < y + kRowHeight && !onRuler;
        if (rowHovered && !state.locked)
        {
          if (hitKey >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            RequestSeek(s, keys[static_cast<std::size_t>(hitKey)].time);
          else if (hitKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          {
            if (s.selectedCamera != cam.id)
              s.selectedKeys.clear();
            s.selectedCamera = cam.id;
            const auto k = static_cast<std::size_t>(hitKey);
            if (io.KeyCtrl)
            {
              if (s.selectedKeys.contains(k))
                s.selectedKeys.erase(k);
              else
                s.selectedKeys.insert(k);
            }
            else if (!s.selectedKeys.contains(k))
            {
              if (!io.KeyShift)
                s.selectedKeys.clear();
              s.selectedKeys.insert(k);
            }
            v.draggingKeys = true;
            v.dragCamera = cam.id;
            v.dragStartTime = c.T(mouse.x);
            v.dragOriginalTimes.clear();
            for (const auto& key : keys)
              v.dragOriginalTimes.push_back(key.time);
          }
          else if (hitKey < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          {
            s.selectedCamera = cam.id;
            if (!io.KeyShift && !io.KeyCtrl)
              s.selectedKeys.clear();
            v.boxSelecting = true;
            v.boxStart = mouse;
            v.dragCamera = cam.id;
          }
          if (hitKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
          {
            s.selectedCamera = cam.id;
            if (!s.selectedKeys.contains(static_cast<std::size_t>(hitKey)))
              s.selectedKeys = {static_cast<std::size_t>(hitKey)};
            ImGui::OpenPopup("key-menu");
          }
        }
        // Box selection
        if (v.boxSelecting && v.dragCamera == cam.id)
        {
          const ImVec2 a(std::min(v.boxStart.x, mouse.x), y), b(std::max(v.boxStart.x, mouse.x), y + kRowHeight);
          dl->AddRectFilled(a, b, ImGui::GetColorU32(CurrentPalette().accentSoft));
          if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
          {
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
              const float x = c.X(keys[i].time);
              if (x >= a.x && x <= b.x)
                s.selectedKeys.insert(i);
            }
            v.boxSelecting = false;
          }
        }
        // Dragging keys in time
        if (v.draggingKeys && v.dragCamera == cam.id && v.dragOriginalTimes.size() == keys.size())
        {
          const double delta = c.T(mouse.x) - v.dragStartTime;
          if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
          {
            if (std::abs(delta) > 1e-6 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            {
              auto& mk = cam.path.MutableKeys();
              for (const auto i : s.selectedKeys)
                if (i < mk.size())
                  mk[i].time = Snap(s, v.dragOriginalTimes[i] + delta);
              cam.path.InvalidateCache();
            }
          }
          else
          {
            v.draggingKeys = false;
            // Re-sort and keep the selection on the moved keys.
            std::vector<iris::CameraKey> moved, all = cam.path.Keys();
            for (const auto i : s.selectedKeys)
              if (i < all.size())
                moved.push_back(all[i]);
            cam.path.SetKeys(all);
            s.selectedKeys.clear();
            for (const auto& m : moved)
              for (std::size_t i = 0; i < cam.path.Size(); ++i)
                if (cam.path.Keys()[i] == m)
                  s.selectedKeys.insert(i);
            if (std::abs(delta) > 1e-6)
              s.Commit("Move keys");
          }
        }
        y += kRowHeight;
      }

      // Speed track
      {
        auto& speed = p.timeRemap.Speed();
        const bool sel = s.selectedTrack == kSpeedTrack;
        label(Icons::CLOCK, "Playback speed", IM_COL32(120, 200, 255, 255), sel);
        if (labelClicked())
          s.selectedTrack = kSpeedTrack;
        // speed curve as a filled band (1.0 = half height)
        ImVec2 prev;
        for (int i = 0; i <= 120; ++i)
        {
          const double t = c.start + (c.end - c.start) * i / 120.0;
          const float h = static_cast<float>(std::clamp(p.timeRemap.SpeedAt(t), 0.0, 2.0) / 2.0) * (kRowHeight - 4);
          const ImVec2 pt(c.X(t), y + kRowHeight - 2 - h);
          if (i > 0)
            dl->AddLine(prev, pt, IM_COL32(120, 200, 255, 160), 1.5f);
          prev = pt;
        }
        for (const auto& k : speed.Keys())
          Diamond(dl, ImVec2(c.X(k.time), y + kRowHeight * 0.5f), 4.5f, IM_COL32(120, 200, 255, 255), IM_COL32(10, 10, 12, 255));
        y += kRowHeight;
      }

      // Property tracks
      for (auto& t : p.tracks)
      {
        const bool sel = s.selectedTrack == t.property;
        label(Icons::SLIDERS, t.property, ImGui::GetColorU32(CurrentPalette().accent), sel);
        if (labelClicked())
          s.selectedTrack = t.property;
        for (const auto& k : t.curve.Keys())
          Diamond(dl, ImVec2(c.X(k.time), y + kRowHeight * 0.5f), 4.5f, ImGui::GetColorU32(CurrentPalette().accent), IM_COL32(10, 10, 12, 255));
        y += kRowHeight;
      }
    }

    // Key context menu
    if (ImGui::BeginPopup("key-menu"))
    {
      if (auto* cam = s.SelectedCamera())
      {
        const char* interps[] = {"Hold", "Linear", "Smooth (auto)", "Catmull-Rom", "Bezier"};
        for (int i = 0; i < 5; ++i)
          if (ImGui::MenuItem(interps[i]))
          {
            for (const auto k : s.selectedKeys)
              if (k < cam->path.Size())
                cam->path.MutableKeys()[k].interpolation = static_cast<iris::Interpolation>(i);
            cam->path.InvalidateCache();
            s.Commit("Key interpolation");
          }
        ImGui::Separator();
        if (ImGui::MenuItem("Ease in / out"))
        {
          for (const auto k : s.selectedKeys)
            if (k < cam->path.Size())
              cam->path.MutableKeys()[k].ease = {0.5, 0.5};
          cam->path.InvalidateCache();
          s.Commit("Ease keys");
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C"))
          CopySelectedKeys(s);
        if (ImGui::MenuItem("Delete", "Del"))
          DeleteSelectedKeys(s);
      }
      ImGui::EndPopup();
    }

    // Playhead
    const float px = c.X(s.playhead);
    dl->AddLine(ImVec2(px, c.min.y), ImVec2(px, c.max.y), ImGui::GetColorU32(CurrentPalette().accentStrong), 1.5f);
    dl->AddTriangleFilled(ImVec2(px - 6, c.min.y), ImVec2(px + 6, c.min.y), ImVec2(px, c.min.y + 8),
                          ImGui::GetColorU32(CurrentPalette().accentStrong));
    dl->PopClipRect();
  }
}
