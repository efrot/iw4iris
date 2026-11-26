// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "DemoControl.hpp"
#include "EditorInternal.hpp"
#include "Runtime.hpp"

#include <iris/timeline_view.hpp>

#include <cmath>
#include <cstdio>

namespace IW4Iris::Editor
{
  namespace
  {
    constexpr float kLabelWidth = 178.0f;
    constexpr float kRulerHeight = 24.0f;
    constexpr float kRowHeight = 22.0f;
    constexpr float kRangeHeight = 13.0f;     // the in/out bar under the ruler
    constexpr float kHeaderHeight = kRulerHeight + kRangeHeight;
    constexpr float kOverviewHeight = 14.0f;  // the scroll bar showing the whole timeline
    constexpr float kVScrollWidth = 7.0f;
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

    iris::TimeWindowLimits Limits(const Session& s)
    {
      const auto& tl = s.project.timeline;
      const double fps = std::max(1.0, tl.frameRate.Fps());
      iris::TimeWindowLimits l;
      l.boundsStart = tl.start;
      l.boundsEnd = std::max({tl.end, s.project.ContentEnd(), tl.start + 1.0});
      l.minSpan = std::max(0.05, 6.0 / fps);
      l.maxSpan = std::max(60.0, (l.boundsEnd - l.boundsStart) * 3.0);
      return l;
    }

    void SetWindow(Session& s, const iris::TimeWindow& w)
    {
      auto& v = Ui().timeline;
      const auto c = iris::ClampWindow(w, Limits(s));
      v.start = c.start;
      v.end = c.end;
    }

    void FitView(Session& s)
    {
      const auto& tl = s.project.timeline;
      SetWindow(s, iris::WindowAround(tl.start, std::max(tl.end, s.project.ContentEnd()), Limits(s), 0.02));
    }

    // Content dragged past the end of the timeline extends it (otherwise
    // the playhead could not reach it).
    void GrowTimelineToContent(Session& s)
    {
      auto& tl = s.project.timeline;
      const double end = s.project.ContentEnd();
      if (end > tl.end)
        iris::FitTimelineToSource(tl, end - tl.start, end);
    }

    // Where a drag lands: magnetic edit points within 8 px (hold Alt to
    // move freely), else the nearest frame.
    double DragTime(const Session& s, const Canvas& c, float x, const std::vector<double>& points)
    {
      const double t = c.T(x);
      if (Ui().timeline.magnet && !ImGui::GetIO().KeyAlt)
      {
        const double tolerance = 8.0 * (c.end - c.start) / std::max(1.0f, c.max.x - c.min.x);
        bool snapped = false;
        const double m = iris::SnapToNearest(t, points, tolerance, &snapped);
        if (snapped)
          return m;
      }
      return Snap(s, t);
    }

    // An editable time: shows the formatted time, takes typed input
    // ("12.5", "1:05", "90f", "00:00:12:30") and returns true when a valid
    // time was entered. Right-click: how times are shown.
    bool TimeField(Session& s, const char* id, double& value, char (&buffer)[32], bool& editing, float width,
                   const char* tooltip)
    {
      auto& tl = s.project.timeline;
      if (!editing)
        std::snprintf(buffer, sizeof(buffer), "%s", iris::FormatTime(value, tl.frameRate, tl.display).c_str());
      ImGui::SetNextItemWidth(width);
      const bool enter = ImGui::InputText(id, buffer, sizeof(buffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
      editing = ImGui::IsItemActive();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !editing)
        ImGui::SetTooltip("%s\nClick and type a time: 12.5 (seconds), 1:05 (min:sec), 90f (frames) or "
                          "00:00:12:30 (timecode). Right-click: display format.", tooltip);
      if (ImGui::BeginPopupContextItem())
      {
        if (ImGui::MenuItem("Timecode (HH:MM:SS:FF)", nullptr, tl.display == iris::TimeDisplay::Timecode))
          tl.display = iris::TimeDisplay::Timecode;
        if (ImGui::MenuItem("Seconds", nullptr, tl.display == iris::TimeDisplay::Seconds))
          tl.display = iris::TimeDisplay::Seconds;
        if (ImGui::MenuItem("Frames", nullptr, tl.display == iris::TimeDisplay::Frames))
          tl.display = iris::TimeDisplay::Frames;
        ImGui::EndPopup();
      }
      if (!enter)
        return false;
      editing = false;
      const auto parsed = iris::ParseTime(buffer, tl.frameRate, tl.display);
      if (!parsed)
      {
        Notify(std::string("Not a time: \"") + buffer + "\" (try 12.5, 1:05, 90f or 00:00:12:30)");
        return false;
      }
      value = *parsed;
      return true;
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
          const ImVec2 inPt(c.X(k.time + k.inHandle.dt), Y(k.value + k.inHandle.dv));
          const ImVec2 outPt(c.X(k.time + k.outHandle.dt), Y(k.value + k.outHandle.dv));
          dl->AddLine(inPt, p, IM_COL32(200, 200, 210, 120));
          dl->AddLine(p, outPt, IM_COL32(200, 200, 210, 120));
          dl->AddCircleFilled(inPt, 4.0f, IM_COL32(220, 220, 230, 255));
          dl->AddCircleFilled(outPt, 4.0f, IM_COL32(220, 220, 230, 255));
          if (hovered && ImLengthSqr(ImVec2(mouse.x - inPt.x, mouse.y - inPt.y)) < 36.0f)
          {
            hoverKey = static_cast<int>(i);
            hoverHandle = -1;
          }
          if (hovered && ImLengthSqr(ImVec2(mouse.x - outPt.x, mouse.y - outPt.y)) < 36.0f)
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

    if (s.timelineFitRequested)
    {
      FitView(s);
      s.timelineFitRequested = false;
    }

    // Header: where we are, the in/out range, view and editing tools.
    {
      double t = s.playhead;
      if (TimeField(s, "##time", t, v.timeText, v.timeEdit, 104, "Playhead (current time)"))
        RequestSeek(s, t);
      ImGui::SameLine();
      if (IconButton(Icons::STEP_BACKWARD, "Previous edit point: key, marker, cut, clip, in/out (Up arrow)"))
        if (const auto prev = iris::PreviousPoint(s.playhead, EditPoints(s)))
          RequestSeek(s, *prev);
      ImGui::SameLine();
      if (IconButton(Icons::STEP_FORWARD, "Next edit point (Down arrow)"))
        if (const auto next = iris::NextPoint(s.playhead, EditPoints(s)))
          RequestSeek(s, *next);
      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("In");
      ImGui::SameLine();
      double in = tl.inPoint;
      if (TimeField(s, "##in", in, v.inText, v.inEdit, 96, "In point: the first frame that is rendered (I)"))
        SetInPoint(s, in);
      ImGui::SameLine();
      ImGui::TextUnformatted("Out");
      ImGui::SameLine();
      double out = tl.outPoint;
      if (TimeField(s, "##out", out, v.outText, v.outEdit, 96, "Out point: rendering stops here (O)"))
        SetOutPoint(s, out);
      ImGui::SameLine();
      ImGui::TextDisabled("%s  |  %s FPS", iris::FormatTime(tl.outPoint - tl.inPoint, tl.frameRate, tl.display).c_str(),
                          tl.frameRate.ToString().c_str());
      ImGui::SameLine();
      ImGui::SetNextItemWidth(110);
      double length = tl.end - tl.start;
      if (DragDouble("##length", &length, 0.1f, 1.0, 36000.0, "Length %.1f s"))
      {
        const bool outAtEnd = std::abs(tl.outPoint - tl.end) < 1e-6;
        tl.end = tl.start + length;
        if (outAtEnd)
          tl.outPoint = tl.end;
        tl.Normalize();
        s.Commit("Timeline length", true);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("How long the timeline is. Drag, or double-click to type.");
    }
    if (IconButton(Icons::EXPAND, "Show the whole timeline (\\)"))
      FitView(s);
    ImGui::SameLine();
    if (IconButton(Icons::SEARCH, "Zoom to the in/out range"))
      SetWindow(s, iris::WindowAround(tl.inPoint, tl.outPoint, Limits(s)));
    ImGui::SameLine();
    if (ImGui::SmallButton(" - "))
      ZoomTimeline(s, 1.0 / 0.7);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Zoom out (-, or the mouse wheel)");
    ImGui::SameLine();
    if (ImGui::SmallButton(" + "))
      ZoomTimeline(s, 0.7);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Zoom in (+, or the mouse wheel)");
    ImGui::SameLine();
    if (IconButton(Icons::CHART, "Graph editor (G)", v.graph))
      v.graph = !v.graph;
    ImGui::SameLine();
    if (IconButton(Icons::FLAG, "Add marker at the playhead (M)"))
      AddMarker(s);
    ImGui::SameLine();
    if (IconButton(Icons::SCISSORS, "Add a cut to the active camera at the playhead"))
    {
      p.cuts.AddCut({s.playhead, p.activeCameraId, CutAngle(s)});
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
    ImGui::Checkbox("Frames", &tl.snapToFrames);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Snap times to whole frames");
    ImGui::SameLine();
    ImGui::Checkbox("Magnet", &v.magnet);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Drags stick to markers, keys, cuts, clip edges and in/out. Hold Alt to move freely.");
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &v.follow);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Scroll the timeline with the playhead while playing");
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Play the loop region over and over (right-click the in/out bar: \"Loop this range\")");

    // Body: labels | canvas, with the ruler and the in/out bar on top and
    // the overview scroll bar at the bottom.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < kLabelWidth + 80 || avail.y < kHeaderHeight + kOverviewHeight + 20)
    {
      ImGui::TextDisabled("Make this panel larger to see the timeline.");
      return;
    }
    auto* dl = ImGui::GetWindowDrawList();
    Canvas c{ImVec2(origin.x + kLabelWidth, origin.y), ImVec2(origin.x + avail.x, origin.y + avail.y), v.start, v.end};
    const float rowsTop = c.min.y + kHeaderHeight;
    const float rowsBottom = c.max.y - kOverviewHeight;
    const float rangeTop = c.min.y + kRulerHeight;
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(14, 15, 18, 255));
    dl->AddRectFilled(origin, ImVec2(origin.x + kLabelWidth, origin.y + avail.y), IM_COL32(19, 20, 24, 255));

    // The whole canvas is one item: hit-testing below is manual.
    ImGui::SetCursorScreenPos(c.min);
    ImGui::InvisibleButton("##canvas", ImVec2(c.max.x - c.min.x, c.max.y - c.min.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool overLabels = ImGui::IsWindowHovered() && mouse.x >= origin.x && mouse.x < c.min.x && mouse.y >= rowsTop &&
                            mouse.y < rowsBottom;
    const auto limits = Limits(s);
    iris::TimeWindow w{v.start, v.end};
    const float width = std::max(1.0f, c.max.x - c.min.x);
    const auto pixelsToSeconds = [&](float px) { return static_cast<double>(px) / width * w.Span(); };

    // Rows scroll vertically when they do not fit.
    const float visibleRows = std::max(1.0f, rowsBottom - rowsTop);
    const float maxScroll = std::max(0.0f, v.contentHeight - visibleRows);

    // Wheel: zoom around the mouse; Shift+wheel (or a sideways wheel) pans;
    // Ctrl+wheel, or the wheel over the track names, scrolls the rows.
    if ((hovered || overLabels) && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
    {
      if (overLabels || (io.KeyCtrl && io.MouseWheel != 0.0f))
        v.scrollY -= io.MouseWheel * kRowHeight * 3.0f;
      else if (io.KeyShift || io.MouseWheelH != 0.0f)
      {
        const float amount = io.MouseWheelH != 0.0f ? -io.MouseWheelH : -io.MouseWheel;
        w = iris::PanWindow(w, amount * 0.1 * w.Span(), limits);
      }
      else
        w = iris::ZoomWindow(w, c.T(mouse.x), io.MouseWheel > 0 ? 0.8 : 1.0 / 0.8, limits);
    }
    // Middle drag pans (and scrolls the rows).
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
      w = iris::PanWindow(w, -pixelsToSeconds(io.MouseDelta.x), limits);
      v.scrollY -= io.MouseDelta.y;
    }
    // Dragging something past the left or right edge scrolls the view.
    auto& uiState = Ui();
    const bool dragging = v.scrubbing || v.rangeDrag != 0 || v.draggingKeys || !uiState.objDragId.empty() ||
                          v.draggingMarker >= 0 || v.boxSelecting;
    if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      float over = 0.0f;
      if (mouse.x < c.min.x)
        over = mouse.x - c.min.x;
      else if (mouse.x > c.max.x)
        over = mouse.x - c.max.x;
      if (over != 0.0f)
        w = iris::PanWindow(w, pixelsToSeconds(std::clamp(over, -200.0f, 200.0f)) * io.DeltaTime * 6.0, limits);
    }
    // Following the playhead while it plays.
    if (v.follow && s.playing && !v.scrubbing && !dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
        v.overviewDrag == 0)
      w = iris::FollowTime(w, s.playhead, limits);
    w = iris::ClampWindow(w, limits);
    v.start = w.start;
    v.end = w.end;
    c.start = v.start;
    c.end = v.end;
    v.scrollY = std::clamp(v.scrollY, 0.0f, maxScroll);

    dl->PushClipRect(c.min, c.max, true);

    // In/out and loop shading
    dl->AddRectFilled(ImVec2(c.X(tl.inPoint), c.min.y), ImVec2(c.X(tl.outPoint), rowsBottom), IM_COL32(255, 255, 255, 8));
    if (tl.loop.enabled)
      dl->AddRectFilled(ImVec2(c.X(tl.loop.start), c.min.y), ImVec2(c.X(tl.loop.end), c.min.y + kRulerHeight),
                        ImGui::GetColorU32(CurrentPalette().accentSoft));
    dl->AddRectFilled(ImVec2(c.min.x, c.min.y), ImVec2(c.X(tl.start), rowsBottom), IM_COL32(0, 0, 0, 90));
    dl->AddRectFilled(ImVec2(c.X(tl.end), c.min.y), ImVec2(c.max.x, rowsBottom), IM_COL32(0, 0, 0, 90));

    // Ruler
    dl->AddRectFilled(c.min, ImVec2(c.max.x, rangeTop), IM_COL32(24, 25, 30, 255));
    const double step = NiceStep(v.end - v.start, c.max.x - c.min.x);
    for (double t = std::floor(v.start / step) * step; t <= v.end; t += step)
    {
      const float x = c.X(t);
      dl->AddLine(ImVec2(x, rowsTop), ImVec2(x, rowsBottom), IM_COL32(255, 255, 255, 14));
      dl->AddLine(ImVec2(x, c.min.y + kRulerHeight * 0.55f), ImVec2(x, c.min.y + kRulerHeight), IM_COL32(170, 170, 180, 160));
      dl->AddText(ImVec2(x + 3, c.min.y + 2), IM_COL32(150, 150, 160, 255),
                  iris::FormatTime(t, tl.frameRate, tl.display).c_str());
    }
    dl->AddLine(ImVec2(c.min.x, rowsTop), ImVec2(c.max.x, rowsTop), IM_COL32(60, 62, 72, 255));
    dl->AddLine(ImVec2(c.X(tl.inPoint), rangeTop), ImVec2(c.X(tl.inPoint), rowsBottom), ImGui::GetColorU32(CurrentPalette().accentSoft));
    dl->AddLine(ImVec2(c.X(tl.outPoint), rangeTop), ImVec2(c.X(tl.outPoint), rowsBottom), ImGui::GetColorU32(CurrentPalette().accentSoft));

    const bool onRuler = mouse.y < rangeTop;
    const bool onRange = !onRuler && mouse.y < rowsTop;
    const bool onOverview = mouse.y >= rowsBottom;
    const bool vScrollShown = maxScroll > 0.0f;
    const bool onVScroll = vScrollShown && !onRuler && !onRange && !onOverview && mouse.x >= c.max.x - kVScrollWidth - 2;
    const bool inRows = hovered && !onRuler && !onRange && !onOverview && !onVScroll;
    const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // In/out bar: drag the ends to set in and out, the middle to move the
    // range, empty space to draw a new one; double-click for everything.
    {
      const float xi = c.X(tl.inPoint), xo = c.X(tl.outPoint);
      const ImU32 accent = ImGui::GetColorU32(CurrentPalette().accent);
      const ImU32 soft = ImGui::GetColorU32(CurrentPalette().accentSoft);
      dl->AddRectFilled(ImVec2(c.min.x, rangeTop), ImVec2(c.max.x, rowsTop), IM_COL32(18, 19, 23, 255));
      dl->AddRectFilled(ImVec2(xi, rangeTop + 2), ImVec2(xo, rowsTop - 2), soft, 3.0f);
      dl->AddRectFilled(ImVec2(xi - 1, rangeTop + 1), ImVec2(xi + 3, rowsTop - 1), accent, 1.0f);
      dl->AddRectFilled(ImVec2(xo - 3, rangeTop + 1), ImVec2(xo + 1, rowsTop - 1), accent, 1.0f);
      int hit = 0;
      if (hovered && onRange)
      {
        const float di = std::abs(mouse.x - xi), dout = std::abs(mouse.x - xo);
        if (std::min(di, dout) < 7.0f)
          hit = di <= dout ? 1 : 2;
        else if (mouse.x > xi && mouse.x < xo)
          hit = 3;
        else
          hit = 4;
        ImGui::SetMouseCursor(hit == 3 ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeEW);
        if (v.rangeDrag == 0)
          ImGui::SetTooltip(hit == 1   ? "In point %s: drag to change (I sets it at the playhead)"
                            : hit == 2 ? "Out point %s: drag to change (O sets it at the playhead)"
                            : hit == 3 ? "Rendered range (%s): drag to move it, double-click for the whole timeline"
                                       : "Drag here to draw a new in/out range (%s)",
                            iris::FormatTime(hit == 1 ? tl.inPoint : hit == 2 ? tl.outPoint : tl.outPoint - tl.inPoint,
                                             tl.frameRate, tl.display).c_str());
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
          tl.inPoint = tl.start;
          tl.outPoint = tl.end;
          s.Commit("In/out: whole timeline");
          hit = 0;
        }
        else if (leftClicked)
        {
          v.rangeDrag = hit;
          v.rangeOrigIn = tl.inPoint;
          v.rangeOrigOut = tl.outPoint;
          v.rangeAnchor = hit == 4 ? Snap(s, c.T(mouse.x)) : c.T(mouse.x);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
          ImGui::OpenPopup("range-menu");
      }
      if (v.rangeDrag != 0)
      {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          auto points = EditPoints(s);
          points.push_back(s.playhead);
          std::erase_if(points, [&](double x) { return x == v.rangeOrigIn || x == v.rangeOrigOut; });
          const double frame = 1.0 / std::max(1.0, tl.frameRate.Fps());
          const double t = DragTime(s, c, mouse.x, points);
          switch (v.rangeDrag)
          {
            case 1: tl.inPoint = std::clamp(t, tl.start, tl.outPoint - frame); break;
            case 2: tl.outPoint = std::clamp(t, tl.inPoint + frame, tl.end); break;
            case 3:
            {
              // The in edge snaps; the range keeps its length.
              const double target = DragTime(s, c, c.X(v.rangeOrigIn + (c.T(mouse.x) - v.rangeAnchor)), points);
              const auto moved = iris::MoveRange({v.rangeOrigIn, v.rangeOrigOut}, target - v.rangeOrigIn, tl.start, tl.end);
              tl.inPoint = moved.in;
              tl.outPoint = moved.out;
              break;
            }
            case 4:
            {
              const double a = std::clamp(v.rangeAnchor, tl.start, tl.end), b = std::clamp(t, tl.start, tl.end);
              if (std::abs(b - a) >= frame)
              {
                tl.inPoint = std::min(a, b);
                tl.outPoint = std::max(a, b);
              }
              break;
            }
            default: break;
          }
          ImGui::SetMouseCursor(v.rangeDrag == 3 ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeEW);
          ImGui::SetTooltip("In %s   Out %s   (%s)", iris::FormatTime(tl.inPoint, tl.frameRate, tl.display).c_str(),
                            iris::FormatTime(tl.outPoint, tl.frameRate, tl.display).c_str(),
                            iris::FormatTime(tl.outPoint - tl.inPoint, tl.frameRate, tl.display).c_str());
        }
        else
        {
          v.rangeDrag = 0;
          if (tl.inPoint != v.rangeOrigIn || tl.outPoint != v.rangeOrigOut)
            s.Commit("Change in/out");
        }
      }
    }
    if (ImGui::BeginPopup("range-menu"))
    {
      if (ImGui::MenuItem("Set in point at the playhead", "I"))
        SetInPoint(s, s.playhead);
      if (ImGui::MenuItem("Set out point at the playhead", "O"))
        SetOutPoint(s, s.playhead);
      if (ImGui::MenuItem("Whole timeline", "double-click"))
      {
        tl.inPoint = tl.start;
        tl.outPoint = tl.end;
        s.Commit("In/out: whole timeline");
      }
      if (ImGui::MenuItem("Loop this range"))
      {
        tl.loop.enabled = true;
        tl.loop.start = tl.inPoint;
        tl.loop.end = tl.outPoint;
        s.Commit("Loop region");
      }
      if (ImGui::MenuItem("Zoom to this range"))
        SetWindow(s, iris::WindowAround(tl.inPoint, tl.outPoint, Limits(s)));
      ImGui::EndPopup();
    }

    // Ruler: press and drag anywhere on it to move the playhead.
    if (hovered && onRuler)
    {
      if (!v.scrubbing)
      {
        // Where a click would put the playhead.
        const float x = mouse.x;
        dl->AddLine(ImVec2(x, c.min.y), ImVec2(x, rowsBottom), IM_COL32(255, 255, 255, 40));
        ImGui::SetTooltip("%s  -  click or drag to move the playhead\nRight-click: in / out / marker here",
                          iris::FormatTime(Snap(s, c.T(x)), tl.frameRate, tl.display).c_str());
      }
      if (leftClicked)
        v.scrubbing = true;
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        v.rangeAnchor = Snap(s, c.T(mouse.x));
        ImGui::OpenPopup("ruler-menu");
      }
    }
    if (ImGui::BeginPopup("ruler-menu"))
    {
      const double at = std::clamp(v.rangeAnchor, tl.start, tl.end);
      ImGui::TextDisabled("%s", iris::FormatTime(at, tl.frameRate, tl.display).c_str());
      if (ImGui::MenuItem("Move the playhead here"))
        RequestSeek(s, at);
      if (ImGui::MenuItem("Set in point here"))
        SetInPoint(s, at);
      if (ImGui::MenuItem("Set out point here"))
        SetOutPoint(s, at);
      if (ImGui::MenuItem("Add marker here"))
      {
        RequestSeek(s, at);
        AddMarker(s);
      }
      if (ImGui::MenuItem("End the timeline here") && at > tl.start)
      {
        tl.end = at;
        tl.Normalize();
        s.Commit("Timeline length");
      }
      ImGui::EndPopup();
    }

    // Rows (scrolled by v.scrollY, clipped between the in/out bar and the
    // overview).
    dl->PopClipRect();
    const ImVec2 rowsMin(c.min.x, rowsTop), rowsMax(c.max.x, rowsBottom);
    dl->PushClipRect(rowsMin, rowsMax, true);
    const float rowsStart = rowsTop - v.scrollY;
    float y = rowsStart;
    bool consumed = false;  // a row element took this click
    auto label = [&](const char* icon, const std::string& text, ImU32 color, bool selected) {
      dl->PopClipRect();
      dl->PushClipRect(ImVec2(origin.x, rowsTop), ImVec2(c.min.x, rowsBottom), true);
      if (selected)
        dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + kLabelWidth, y + kRowHeight), IM_COL32(255, 255, 255, 14));
      dl->AddText(ImVec2(origin.x + 8, y + 3), color, icon);
      dl->AddText(ImVec2(origin.x + 28, y + 3), IM_COL32(210, 212, 220, 255), text.c_str());
      dl->PopClipRect();
      dl->PushClipRect(rowsMin, rowsMax, true);
      dl->AddLine(ImVec2(c.min.x, y + kRowHeight), ImVec2(c.max.x, y + kRowHeight), IM_COL32(255, 255, 255, 10));
    };
    auto labelClicked = [&]() {
      return ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overLabels && mouse.y >= y && mouse.y < y + kRowHeight;
    };

    if (v.graph)
    {
      Canvas g = c;
      g.min.y = rowsTop + 4;
      g.max.y = rowsBottom;
      DrawGraph(s, g, dl);
      y = rowsStart + visibleRows;
      consumed = true;  // the graph editor handles its own clicks
    }
    else
    {
      // Markers row (with the demo's kills and trickshot candidates as
      // small ticks along the bottom: click one to go there)
      label(Icons::FLAG, "Markers", IM_COL32(247, 185, 85, 255), false);
      if (s.status.demoPlaying && !s.moments.Moments().empty())
      {
        std::vector<std::pair<double, std::string>> ticks;
        for (const auto& m : s.moments.Moments())
          if (m.kind == iris::MomentKind::Kill)
            if (const auto at = MomentTimelineTime(s, m.serverTime))
              ticks.emplace_back(*at, m.detail);
        std::vector<double> stars;
        for (const auto& m : s.moments.Trickshots())
          if (const auto at = MomentTimelineTime(s, m.serverTime))
            stars.push_back(*at);
        for (const auto& [at, text] : ticks)
        {
          const float x = c.X(at);
          if (x < c.min.x - 4 || x > c.max.x + 4)
            continue;
          const bool star = std::any_of(stars.begin(), stars.end(), [&](double t) { return std::abs(t - at) < 1e-3; });
          const ImU32 col = star ? IM_COL32(242, 193, 78, 255) : IM_COL32(229, 72, 77, 220);
          dl->AddTriangleFilled(ImVec2(x - 3, y + kRowHeight - 1), ImVec2(x + 3, y + kRowHeight - 1),
                                ImVec2(x, y + kRowHeight - 7), col);
          if (inRows && std::abs(mouse.x - x) < 4 && mouse.y >= y + kRowHeight - 9 && mouse.y < y + kRowHeight)
          {
            ImGui::SetTooltip("%s%s\nClick: go there (2 s before)", star ? "Trickshot candidate: " : "", text.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
              RequestSeek(s, std::max(tl.start, at - 2.0));
              consumed = true;
            }
          }
        }
      }
      for (std::size_t i = 0; i < p.markers.size(); ++i)
      {
        auto& m = p.markers[i];
        const float x = c.X(m.time);
        const ImU32 col = ParseColor(m.color);
        dl->AddTriangleFilled(ImVec2(x - 5, y + 4), ImVec2(x + 5, y + 4), ImVec2(x, y + 14), col);
        dl->AddLine(ImVec2(x, y + 14), ImVec2(x, rowsBottom), (col & 0x00FFFFFF) | 0x30000000);
        if (!m.name.empty())
          dl->AddText(ImVec2(x + 7, y + 3), IM_COL32(200, 200, 205, 200), m.name.c_str());
        const bool hit = inRows && std::abs(mouse.x - x) < 6 && mouse.y >= y && mouse.y < y + kRowHeight;
        if (hit)
        {
          consumed = consumed || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
          ImGui::SetTooltip("%s  %s\nDrag to move, double-click to jump there", m.name.empty() ? "Marker" : m.name.c_str(),
                            iris::FormatTime(m.time, tl.frameRate, tl.display).c_str());
        }
        if (hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
          v.draggingMarker = static_cast<int>(i);
          consumed = true;
        }
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
          GrowTimelineToContent(s);
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
          {
            // The recording it is in, when the project has angles.
            std::string text = cam->name;
            if (!cuts[i].angle.empty())
            {
              const auto* a = p.FindAngle(cuts[i].angle);
              text += "  @ " + (a ? a->name : std::string("removed angle"));
            }
            else if (!p.angles.empty())
              text += "  @ main";
            dl->PushClipRect(ImVec2(c.X(t0) + 1, y), ImVec2(c.X(t1) - 1, y + kRowHeight), true);
            dl->AddText(ImVec2(c.X(t0) + 5, y + 3), IM_COL32(240, 240, 245, 255), text.c_str());
            dl->PopClipRect();
          }
          if (inRows && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouse.x >= c.X(t0) && mouse.x < c.X(t1) &&
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
          if (!p.angles.empty())
          {
            ImGui::SeparatorText("Recording");
            auto& cut = cuts[static_cast<std::size_t>(index)];
            if (ImGui::MenuItem("Main demo", nullptr, cut.angle.empty()))
            {
              cut.angle.clear();
              p.cuts.SetCuts(cuts);
              s.Commit("Change cut angle");
            }
            for (const auto& a : p.angles)
              if (ImGui::MenuItem(a.name.c_str(), nullptr, cut.angle == a.id))
              {
                cut.angle = a.id;
                p.cuts.SetCuts(cuts);
                s.Commit("Change cut angle");
              }
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
          if (inRows && std::abs(mouse.x - kp.x) < 7 && std::abs(mouse.y - kp.y) < 8)
            hitKey = static_cast<int>(i);
        }
        const bool rowHovered = inRows && mouse.y >= y && mouse.y < y + kRowHeight;
        if (rowHovered && hitKey >= 0 && !v.draggingKeys)
        {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          ImGui::SetTooltip("Key %d  %s\nDrag to retime, double-click to jump there, right-click for options", hitKey + 1,
                            iris::FormatTime(keys[static_cast<std::size_t>(hitKey)].time, tl.frameRate, tl.display).c_str());
        }
        if (rowHovered && !state.locked && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          consumed = true;
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
            if (b.x - a.x < 4.0f)
            {
              // A click on empty space, not a box: move the playhead there.
              RequestSeek(s, Snap(s, c.T(v.boxStart.x)));
            }
            else
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
            {
              GrowTimelineToContent(s);
              s.Commit("Move keys");
            }
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

      // Scene objects: actor clips (drag to move, drag the right edge to
      // trim, right-click to delete), transform keys (click to jump there),
      // graphic animations and effect plays.
      auto& ui = Ui();
      auto& sc = p.scene;
      auto rowHovered = [&]() { return inRows && mouse.y >= y && mouse.y < y + kRowHeight; };
      bool removeBar = false;
      auto bar = [&](const std::string& objId, int index, double& start, double& duration, const std::string& text, ImU32 col) {
        const float x0 = c.X(start), x1 = std::max(c.X(start + duration), x0 + 3.0f);
        dl->AddRectFilled(ImVec2(x0, y + 3), ImVec2(x1, y + kRowHeight - 3), col, 3.0f);
        dl->PushClipRect(ImVec2(x0, y), ImVec2(x1, y + kRowHeight), true);
        dl->AddText(ImVec2(x0 + 4, y + 4), IM_COL32(12, 12, 16, 255), text.c_str());
        dl->PopClipRect();
        const bool over = rowHovered() && mouse.x >= x0 - 2 && mouse.x <= x1 + 2;
        if (over)
        {
          const bool edge = mouse.x > x1 - 6;
          ImGui::SetMouseCursor(edge ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
          if (ui.objDragId.empty())
            ImGui::SetTooltip("%s\n%s .. %s\nDrag to move, drag the right edge to trim, right-click to delete", text.c_str(),
                              iris::FormatTime(start, tl.frameRate, tl.display).c_str(),
                              iris::FormatTime(start + duration, tl.frameRate, tl.display).c_str());
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ui.objDragId.empty())
          {
            consumed = true;
            ui.objDragId = objId;
            ui.objDragIndex = index;
            ui.objDragMode = mouse.x > x1 - 6 ? 1 : 0;
            ui.objDragOffset = c.T(mouse.x) - start;
            SelectObject(s, objId);
          }
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            removeBar = true;
        }
        if (ui.objDragId == objId && ui.objDragIndex == index && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          if (ui.objDragMode == 0)
            start = Snap(s, c.T(mouse.x) - ui.objDragOffset);
          else
            duration = std::max(0.05, Snap(s, c.T(mouse.x)) - start);
        }
      };
      auto keys = [&](const iris::TransformTrack& track, ImU32 col) {
        for (const double t : track.KeyTimes())
        {
          const ImVec2 kp(c.X(t), y + kRowHeight * 0.5f);
          Diamond(dl, kp, 4.0f, col, IM_COL32(10, 10, 12, 255));
          if (rowHovered() && std::abs(mouse.x - kp.x) < 5 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ui.objDragId.empty())
          {
            RequestSeek(s, t);
            consumed = true;
          }
        }
      };
      for (auto& a : sc.actors)
      {
        label(Icons::USER, a.name, IM_COL32(140, 200, 255, 255), s.selectedObject == a.id);
        if (labelClicked())
          SelectObject(s, a.id);
        for (int i = 0; i < static_cast<int>(a.clips.size()); ++i)
        {
          removeBar = false;
          auto& clip = a.clips[static_cast<std::size_t>(i)];
          bar(a.id, i, clip.start, clip.duration, iris::ClassifyAnimation(clip.anim).label, IM_COL32(120, 180, 240, 220));
          if (removeBar)
          {
            a.clips.erase(a.clips.begin() + i);
            s.Commit("Delete clip");
            break;
          }
        }
        keys(a.track, IM_COL32(250, 250, 255, 255));
        y += kRowHeight;
      }
      for (auto& pr : sc.props)
      {
        if (pr.track.Empty())
          continue;
        label(Icons::CUBE, pr.name, IM_COL32(200, 200, 170, 255), s.selectedObject == pr.id);
        if (labelClicked())
          SelectObject(s, pr.id);
        keys(pr.track, IM_COL32(230, 220, 160, 255));
        y += kRowHeight;
      }
      for (auto& e : sc.effects)
      {
        label(Icons::MAGIC, e.name, IM_COL32(255, 160, 90, 255), s.selectedObject == e.id);
        if (labelClicked())
          SelectObject(s, e.id);
        const int plays = e.repeat > 0 ? (e.count > 0 ? std::min(e.count, 200) : 200) : 1;
        for (int k = 0; k < plays; ++k)
        {
          const double t = e.start + k * e.repeat;
          if (t > c.end)
            break;
          const float x = c.X(t);
          dl->AddTriangleFilled(ImVec2(x - 5, y + kRowHeight - 4), ImVec2(x + 5, y + kRowHeight - 4), ImVec2(x, y + 4),
                                e.enabled ? IM_COL32(255, 160, 90, 255) : IM_COL32(120, 110, 100, 255));
        }
        y += kRowHeight;
      }
      for (auto& g : sc.graphics)
      {
        label(Icons::LAYER, g.name, IM_COL32(200, 150, 255, 255), s.selectedObject == g.id);
        if (labelClicked())
          SelectObject(s, g.id);
        for (int i = 0; i < static_cast<int>(g.animations.size()); ++i)
        {
          removeBar = false;
          auto& an = g.animations[static_cast<std::size_t>(i)];
          std::string text = an.preset;
          for (const auto& info : iris::AnimationPresets())
            if (info.id == an.preset)
              text = info.label;
          bar(g.id, i, an.start, an.duration, text, an.enabled ? IM_COL32(190, 150, 250, 220) : IM_COL32(110, 100, 130, 220));
          if (removeBar)
          {
            g.animations.erase(g.animations.begin() + i);
            s.Commit("Delete animation");
            break;
          }
        }
        keys(g.track, IM_COL32(230, 210, 255, 255));
        y += kRowHeight;
      }
      if (!ui.objDragId.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        ui.objDragId.clear();
        ui.objDragIndex = -1;
        GrowTimelineToContent(s);
        s.Commit("Retime");
      }
    }
    v.contentHeight = y - rowsStart;
    dl->PopClipRect();
    dl->PushClipRect(c.min, c.max, true);

    // Off-screen playhead: an arrow at the edge; clicking it brings it back.
    const float playheadX = c.X(s.playhead);
    const bool offscreenLeft = playheadX < c.min.x, offscreenRight = playheadX > c.max.x;
    const float arrowX = offscreenLeft ? c.min.x + 2 : c.max.x - 2 - (vScrollShown ? kVScrollWidth + 2 : 0.0f);
    const float arrowY = rowsTop + 10;
    if ((offscreenLeft || offscreenRight) && inRows && std::abs(mouse.x - arrowX) < 12 && std::abs(mouse.y - arrowY) < 10)
    {
      ImGui::SetTooltip("The playhead is %s. Click to show it.", offscreenLeft ? "to the left" : "to the right");
      if (leftClicked)
      {
        consumed = true;
        const double span = v.end - v.start;
        SetWindow(s, {s.playhead - 0.3 * span, s.playhead + 0.7 * span});
      }
    }

    // A press on empty row space moves the playhead (and keeps scrubbing
    // while the button is held), like the ruler.
    if (inRows && leftClicked && !consumed && !v.scrubbing && v.rangeDrag == 0)
      v.scrubbing = true;

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

    // Scrubbing: the playhead follows the mouse, sticking to edit points.
    if (v.scrubbing)
    {
      s.scrubbing = true;
      const double t = std::clamp(DragTime(s, c, mouse.x, EditPoints(s)), tl.start, tl.end);
      s.playhead = t;
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      if (s.status.demoPlaying)
      {
        // Live preview in a demo. Seeking forward is cheap (the demo runs
        // ahead); seeking back restarts the demo, so that only happens once
        // the mouse rests, or on release.
        const double now = ImGui::GetTime();
        if (std::abs(t - v.lastScrubTime) > 1e-9)
        {
          v.lastScrubTime = t;
          v.scrubRestTime = now;
        }
        const double scene = p.timeRemap.SceneTime(t);
        const double frame = 1.0 / std::max(1.0, tl.frameRate.Fps());
        const bool forward = scene > s.status.sceneTime + frame;
        const bool backward = scene < s.status.sceneTime - frame;
        const bool rested = now - v.scrubRestTime > 0.35;
        if (!s.status.seeking && now - v.lastLiveSeek > 0.1 && (forward || (backward && rested)))
        {
          v.lastLiveSeek = now;
          s.Post([scene] { DemoControl::Seek(scene); });
        }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        v.scrubbing = false;
        s.scrubbing = false;
        v.lastScrubTime = -1.0;
        RequestSeek(s, s.playhead);
      }
    }

    // Playhead: a line through the rows and a handle with the time on the ruler.
    const ImU32 strong = ImGui::GetColorU32(CurrentPalette().accentStrong);
    const float px = c.X(s.playhead);
    dl->AddLine(ImVec2(px, c.min.y), ImVec2(px, rowsBottom), strong, 1.5f);
    dl->AddTriangleFilled(ImVec2(px - 6, rangeTop - 8), ImVec2(px + 6, rangeTop - 8), ImVec2(px, rangeTop), strong);
    if (v.scrubbing || (hovered && onRuler))
    {
      const auto timeLabel = iris::FormatTime(s.playhead, tl.frameRate, tl.display);
      const ImVec2 size = ImGui::CalcTextSize(timeLabel.c_str());
      const float lx = std::clamp(px - size.x * 0.5f - 4, c.min.x, c.max.x - size.x - 8);
      dl->AddRectFilled(ImVec2(lx, c.min.y + 1), ImVec2(lx + size.x + 8, c.min.y + size.y + 3), strong, 3.0f);
      dl->AddText(ImVec2(lx + 4, c.min.y + 2), IM_COL32(12, 12, 16, 255), timeLabel.c_str());
    }
    if (offscreenLeft || offscreenRight)
      dl->AddTriangleFilled(ImVec2(arrowX, arrowY), ImVec2(arrowX + (offscreenLeft ? 9.0f : -9.0f), arrowY - 6),
                            ImVec2(arrowX + (offscreenLeft ? 9.0f : -9.0f), arrowY + 6), strong);

    // Vertical scroll bar for the rows.
    if (vScrollShown)
    {
      const float x0 = c.max.x - kVScrollWidth - 1, x1 = c.max.x - 1;
      const float thumbH = std::max(20.0f, visibleRows * visibleRows / std::max(visibleRows, v.contentHeight));
      const float thumbY = rowsTop + (visibleRows - thumbH) * (maxScroll > 0 ? v.scrollY / maxScroll : 0.0f);
      dl->AddRectFilled(ImVec2(x0, rowsTop), ImVec2(x1, rowsBottom), IM_COL32(0, 0, 0, 80), 3.0f);
      const bool overThumb = hovered && onVScroll;
      dl->AddRectFilled(ImVec2(x0 + 1, thumbY), ImVec2(x1 - 1, thumbY + thumbH),
                        overThumb || v.vScrollDrag ? IM_COL32(200, 200, 215, 200) : IM_COL32(140, 140, 155, 150), 3.0f);
      if (overThumb && leftClicked)
      {
        v.vScrollDrag = true;
        if (mouse.y < thumbY || mouse.y > thumbY + thumbH)  // click on the track jumps there
          v.scrollY = std::clamp((mouse.y - rowsTop - thumbH * 0.5f) / std::max(1.0f, visibleRows - thumbH) * maxScroll, 0.0f, maxScroll);
      }
      if (v.vScrollDrag)
      {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
          v.scrollY = std::clamp(v.scrollY + io.MouseDelta.y * maxScroll / std::max(1.0f, visibleRows - thumbH), 0.0f, maxScroll);
        else
          v.vScrollDrag = false;
      }
    }
    dl->PopClipRect();

    // Overview: the whole timeline; the bright box is what is visible. Drag
    // the box to scroll, its ends to zoom, click elsewhere to jump there.
    {
      const float oy0 = rowsBottom + 1, oy1 = c.max.y - 1;
      double a = std::min(limits.boundsStart, v.start), b = std::max(limits.boundsEnd, v.end);
      if (v.overviewDrag != 0)
      {
        a = v.overviewA;
        b = v.overviewB;
      }
      const double span = std::max(1e-6, b - a);
      const auto ox = [&](double t) { return c.min.x + static_cast<float>((t - a) / span) * width; };
      const auto ot = [&](float x) { return a + (x - c.min.x) / width * span; };
      dl->AddRectFilled(ImVec2(c.min.x, oy0), ImVec2(c.max.x, oy1), IM_COL32(10, 10, 13, 255));
      dl->AddRectFilled(ImVec2(ox(tl.inPoint), oy0 + 3), ImVec2(ox(tl.outPoint), oy1 - 3), ImGui::GetColorU32(CurrentPalette().accentSoft));
      for (const auto& m : p.markers)
        dl->AddLine(ImVec2(ox(m.time), oy0), ImVec2(ox(m.time), oy1), (ParseColor(m.color) & 0x00FFFFFF) | 0xA0000000);
      dl->AddLine(ImVec2(ox(s.playhead), oy0), ImVec2(ox(s.playhead), oy1), strong, 2.0f);
      const float vx0 = ox(v.start), vx1 = std::max(ox(v.end), vx0 + 6.0f);
      const bool overOverview = hovered && onOverview;
      int hit = 0;
      if (overOverview)
      {
        if (std::abs(mouse.x - vx0) < 5)
          hit = 2;
        else if (std::abs(mouse.x - vx1) < 5)
          hit = 3;
        else if (mouse.x > vx0 && mouse.x < vx1)
          hit = 1;
        ImGui::SetMouseCursor(hit >= 2 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
        if (v.overviewDrag == 0)
          ImGui::SetTooltip("Whole timeline. Drag the box to scroll, its ends to zoom, click to jump there.\n"
                            "Double-click: show everything.");
      }
      dl->AddRectFilled(ImVec2(vx0, oy0 + 1), ImVec2(vx1, oy1 - 1),
                        overOverview || v.overviewDrag ? IM_COL32(255, 255, 255, 46) : IM_COL32(255, 255, 255, 28), 2.0f);
      dl->AddRect(ImVec2(vx0, oy0 + 1), ImVec2(vx1, oy1 - 1), IM_COL32(220, 220, 235, 150), 2.0f);
      if (overOverview && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
        FitView(s);
        v.overviewDrag = 0;
      }
      else if (overOverview && leftClicked)
      {
        v.overviewA = a;
        v.overviewB = b;
        if (hit == 0)
        {
          // Jump: centre the view on the click, then keep dragging it.
          const double half = 0.5 * (v.end - v.start);
          SetWindow(s, {ot(mouse.x) - half, ot(mouse.x) + half});
          hit = 1;
        }
        v.overviewDrag = hit;
        v.overviewGrab = ot(mouse.x) - v.start;
      }
      if (v.overviewDrag != 0)
      {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          const double t = ot(mouse.x);
          iris::TimeWindow nw{v.start, v.end};
          if (v.overviewDrag == 1)
          {
            const double len = nw.Span();
            nw.start = t - v.overviewGrab;
            nw.end = nw.start + len;
          }
          else if (v.overviewDrag == 2)
            nw.start = std::min(t, nw.end - limits.minSpan);
          else
            nw.end = std::max(t, nw.start + limits.minSpan);
          SetWindow(s, nw);
        }
        else
          v.overviewDrag = 0;
      }
    }

    // The label column's corner: how to navigate.
    if (ImGui::IsWindowHovered() && mouse.x >= origin.x && mouse.x < c.min.x && mouse.y >= rowsBottom)
      ImGui::SetTooltip("Timeline navigation\n"
                        "  Click or drag the ruler / empty rows: move the playhead\n"
                        "  Mouse wheel: zoom    Shift+wheel or middle drag: scroll\n"
                        "  Wheel over the names / Ctrl+wheel: scroll the rows\n"
                        "  Bottom bar: drag to scroll, drag its ends to zoom\n"
                        "  In/out bar: drag the ends, the middle, or draw a new range\n"
                        "  Left/Right: one frame (Shift: one second)   Up/Down: previous/next edit point\n"
                        "  I / O: in / out at the playhead   Home / End: go to in / out\n"
                        "  + / -: zoom   \\: show everything   Alt while dragging: no magnet");
    dl->AddText(ImVec2(origin.x + 8, rowsBottom + 1), IM_COL32(120, 122, 135, 255), "Navigate (hover for help)");
    dl->AddText(ImVec2(origin.x + 8, c.min.y + 4), IM_COL32(120, 122, 135, 255), "Time");
    dl->AddText(ImVec2(origin.x + 8, rangeTop), IM_COL32(120, 122, 135, 255), "In / Out");
  }

  // ------------------------------------------------------------ navigation

  std::vector<double> EditPoints(const Session& s)
  {
    const auto& p = s.project;
    const auto& tl = p.timeline;
    std::vector<double> pts{tl.start, tl.end, tl.inPoint, tl.outPoint};
    if (tl.loop.enabled)
    {
      pts.push_back(tl.loop.start);
      pts.push_back(tl.loop.end);
    }
    for (const auto& m : p.markers)
      pts.push_back(m.time);
    for (const auto& c : p.cuts.Cuts())
      pts.push_back(c.time);
    // The demo's kills: Up / Down jump from kill to kill.
    if (s.status.demoPlaying)
      for (const auto& m : s.moments.Moments())
        if (m.kind == iris::MomentKind::Kill)
          if (const auto at = MomentTimelineTime(s, m.serverTime))
            pts.push_back(*at);
    for (const auto& cam : p.cameras)
      for (const auto& k : cam.path.Keys())
        pts.push_back(k.time);
    for (const auto& k : p.timeRemap.Speed().Keys())
      pts.push_back(k.time);
    for (const auto& t : p.tracks)
      for (const auto& k : t.curve.Keys())
        pts.push_back(k.time);
    const auto addTrack = [&](const iris::TransformTrack& track) {
      for (const double t : track.KeyTimes())
        pts.push_back(t);
    };
    for (const auto& a : p.scene.actors)
    {
      for (const auto& clip : a.clips)
      {
        pts.push_back(clip.start);
        pts.push_back(clip.start + clip.duration);
      }
      addTrack(a.track);
    }
    for (const auto& pr : p.scene.props)
      addTrack(pr.track);
    for (const auto& e : p.scene.effects)
      pts.push_back(e.start);
    for (const auto& g : p.scene.graphics)
    {
      for (const auto& an : g.animations)
      {
        pts.push_back(an.start);
        pts.push_back(an.start + an.duration);
      }
      addTrack(g.track);
    }
    return pts;
  }

  void ZoomTimeline(Session& s, double factor)
  {
    auto& v = Ui().timeline;
    iris::TimeWindow w{v.start, v.end};
    // Around the playhead when it is visible, else around the centre.
    const double pivot = s.playhead >= v.start && s.playhead <= v.end ? s.playhead : 0.5 * (v.start + v.end);
    SetWindow(s, iris::ZoomWindow(w, pivot, factor, Limits(s)));
  }

  void FitTimeline(Session& s)
  {
    FitView(s);
  }

  void SetInPoint(Session& s, double t)
  {
    auto& tl = s.project.timeline;
    const double frame = 1.0 / std::max(1.0, tl.frameRate.Fps());
    t = Snap(s, std::clamp(t, tl.start, tl.end));
    if (t >= tl.outPoint)
      tl.outPoint = std::min(tl.end, t + frame);  // keep a valid range
    tl.inPoint = std::min(t, tl.outPoint - frame);
    tl.Normalize();
    s.Commit("Set in point");
  }

  void SetOutPoint(Session& s, double t)
  {
    auto& tl = s.project.timeline;
    const double frame = 1.0 / std::max(1.0, tl.frameRate.Fps());
    t = Snap(s, std::clamp(t, tl.start, tl.end));
    if (t <= tl.inPoint)
      tl.inPoint = std::max(tl.start, t - frame);
    tl.outPoint = std::max(t, tl.inPoint + frame);
    tl.Normalize();
    s.Commit("Set out point");
  }
}
