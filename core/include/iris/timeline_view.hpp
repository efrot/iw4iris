// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The arithmetic behind the timeline's navigation, kept out of the UI so it
// can be tested: typed time input, the visible window (zoom, pan, bounds,
// following the playhead), magnetic snapping to edit points and moving the
// in/out range.
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "iris/timeline.hpp"

namespace iris
{
    // Parses a time typed by the user. Accepted, with an optional leading '-':
    //   "12.5" or "12.5s"  seconds
    //   "90f"              frames at `rate`
    //   "1:05.25"          minutes:seconds
    //   "1:02:03"          hours:minutes:seconds
    //   "00:01:02:15"      timecode HH:MM:SS:FF (frames at `rate`)
    // Whitespace around the text is ignored. With `display == Frames` a bare
    // integer is a frame number, matching what the field shows.
    std::optional<double> ParseTime(std::string_view text, const FrameRate& rate,
                                    TimeDisplay display = TimeDisplay::Timecode);

    // The visible part of the timeline (seconds).
    struct TimeWindow
    {
        double start = 0.0;
        double end = 10.0;

        double Span() const { return end - start; }
        bool operator==(const TimeWindow&) const = default;
    };

    // Limits for the visible window: it never gets narrower than `minSpan`
    // (a few frames), never wider than `maxSpan`, and keeps an overlap with
    // [boundsStart, boundsEnd] (the timeline and its content) so the view
    // cannot be lost far away from everything.
    struct TimeWindowLimits
    {
        double boundsStart = 0.0;
        double boundsEnd = 10.0;
        double minSpan = 0.05;
        double maxSpan = 36000.0;
    };
    TimeWindow ClampWindow(TimeWindow w, const TimeWindowLimits& limits);
    // Zoom by `factor` (< 1 zooms in) around `pivot`, which stays where it is.
    TimeWindow ZoomWindow(TimeWindow w, double pivot, double factor, const TimeWindowLimits& limits);
    TimeWindow PanWindow(TimeWindow w, double delta, const TimeWindowLimits& limits);
    // While playing: when t leaves the window (or reaches its last
    // `margin` fraction), the window pages forward so t sits near the start;
    // a t before the window pages back. Otherwise the window is unchanged.
    TimeWindow FollowTime(TimeWindow w, double t, const TimeWindowLimits& limits, double margin = 0.05);
    // A window showing [a, b] with a little room on both sides.
    TimeWindow WindowAround(double a, double b, const TimeWindowLimits& limits, double padding = 0.04);

    // Magnetic snapping: the candidate nearest to t if it is within
    // `toleranceSeconds`, else t.
    double SnapToNearest(double t, const std::vector<double>& candidates, double toleranceSeconds, bool* snapped = nullptr);
    // The nearest candidate strictly before / after t (edit-point stepping).
    std::optional<double> PreviousPoint(double t, const std::vector<double>& candidates, double epsilon = 1e-6);
    std::optional<double> NextPoint(double t, const std::vector<double>& candidates, double epsilon = 1e-6);

    // Moves the [inPoint, outPoint] range by delta without changing its
    // length, kept inside [start, end].
    struct InOutRange
    {
        double in = 0.0;
        double out = 0.0;
        bool operator==(const InOutRange&) const = default;
    };
    InOutRange MoveRange(InOutRange range, double delta, double start, double end);
}  // namespace iris
