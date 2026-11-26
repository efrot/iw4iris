// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Cinematic time. Every timeline-driven system evaluates its state from the
// cinematic time t (seconds) — never from wall-clock time — so playback,
// slow motion, seeking and multi-pass rendering are deterministic.
//
// The demo (or live scene) runs on its own clock, "scene time" (seconds since
// the start of the demo). The playback speed curve maps one to the other:
//
//     sceneTime(t) = sceneOffset + integral from timelineStart to t of speed(x) dx
//
// so speed ramps, slow motion and freeze frames (speed 0) are part of the
// project and the same cinematic frame always shows the same scene instant.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "iris/curve.hpp"

namespace iris
{
    // Exact frame rate as a ratio (e.g. 24000/1001 for 23.976).
    struct FrameRate
    {
        std::int64_t numerator = 60;
        std::int64_t denominator = 1;

        double Fps() const { return static_cast<double>(numerator) / static_cast<double>(denominator); }
        bool Valid() const { return numerator > 0 && denominator > 0; }
        bool operator==(const FrameRate&) const = default;

        // Start time of frame n in seconds (exact for integer rates).
        double FrameToSeconds(std::int64_t frame) const;
        // Frame containing time t (floor), with a tolerance so that times
        // computed by FrameToSeconds map back to the same frame.
        std::int64_t SecondsToFrame(double seconds) const;
        // Nearest frame boundary.
        std::int64_t RoundToFrame(double seconds) const;
        double SnapSeconds(double seconds) const { return FrameToSeconds(RoundToFrame(seconds)); }

        static FrameRate FromFps(double fps);  // recognises NTSC rates
        std::string ToString() const;          // "60", "23.976", "30000/1001"
    };

    const std::vector<FrameRate>& StandardFrameRates();

    // Timecode formatting (HH:MM:SS:FF, or seconds, or frames).
    enum class TimeDisplay
    {
        Timecode,
        Seconds,
        Frames,
    };
    std::string FormatTime(double seconds, const FrameRate& rate, TimeDisplay display);

    struct Marker
    {
        std::string id;
        double time = 0.0;
        std::string name;
        std::string color = "#8B7CF6";
        std::string kind = "note";  // note, kill, trickshot, cut, ...
        bool operator==(const Marker&) const = default;
    };

    // One camera cut: from `time` on (until the next cut) the shot uses the
    // camera with the given id.
    struct CameraCut
    {
        double time = 0.0;
        std::string cameraId;
        bool operator==(const CameraCut&) const = default;
    };

    class CameraCutTrack
    {
       public:
        const std::vector<CameraCut>& Cuts() const { return cuts_; }
        void SetCuts(std::vector<CameraCut> cuts);
        void AddCut(const CameraCut& cut);  // replaces a cut at the same time
        bool RemoveCutAt(std::size_t index);
        // The camera active at t: the last cut at or before t, or the first
        // cut's camera before it; none without cuts.
        std::optional<std::string> CameraAt(double t) const;
        void Clear() { cuts_.clear(); }

       private:
        std::vector<CameraCut> cuts_;
    };

    // Maps cinematic time to scene (demo) time through a speed curve.
    class TimeRemap
    {
       public:
        // speed: playback speed multiplier (1 = real time, 0 = frozen). An
        // empty curve means constant speed 1.
        ScalarCurve& Speed() { return speed_; }
        const ScalarCurve& Speed() const { return speed_; }

        double timelineStart = 0.0;  // cinematic time at which sceneOffset applies
        double sceneOffset = 0.0;    // scene time at timelineStart

        double SceneTime(double cinematicTime) const;
        // Inverse mapping (the earliest cinematic time that reaches the scene
        // time); speed is clamped to >= 0 so the mapping is monotonic.
        double CinematicTime(double sceneTime, double searchEnd) const;
        double SpeedAt(double cinematicTime) const;

       private:
        ScalarCurve speed_{1.0};
    };

    // Slows the scene down around the moment at scene time `sceneMoment`
    // (a kill, a trickshot): from shortly before it the speed ramps down to
    // `slow`, holds for `holdSeconds` of cinematic time and ramps back to the
    // speed it had. Speed keys already in that stretch are replaced. Returns
    // the cinematic time the moment is at afterwards (inside the slow part).
    double AddSlowMotion(TimeRemap& remap, double sceneMoment, double slow = 0.3, double holdSeconds = 2.0,
                         double searchEnd = 36000.0);

    struct LoopRegion
    {
        bool enabled = false;
        double start = 0.0;
        double end = 0.0;
        bool operator==(const LoopRegion&) const = default;
    };

    // The editor's timeline view of a project: range, in/out points, loop
    // region, markers. Kept separate from rendering so it serialises simply.
    struct TimelineSettings
    {
        FrameRate frameRate{60, 1};
        double start = 0.0;
        double end = 10.0;
        double inPoint = 0.0;
        double outPoint = 10.0;
        LoopRegion loop;
        TimeDisplay display = TimeDisplay::Timecode;
        bool snapToFrames = true;
        bool operator==(const TimelineSettings&) const = default;

        // Clamps in/out and loop to the range and keeps them ordered.
        void Normalize();
        // Next playhead position when playing, honouring the loop region.
        double Advance(double playhead, double dt) const;
    };

    // Fits the timeline to a source (a demo) of `length` cinematic seconds
    // once it is known: a shorter timeline grows to the source, and an out
    // point that was at the old end follows it. An untouched default timeline
    // (0..10 s, in/out at its ends) that is longer than the source shrinks to
    // it, unless content (keys, clips) lies beyond. Returns true on a change.
    bool FitTimelineToSource(TimelineSettings& timeline, double length, double contentEnd);

    // Frames of a recording range [in, out): the first frame index and the
    // number of frames, at the given rate.
    struct FrameRange
    {
        std::int64_t first = 0;
        std::int64_t count = 0;
    };
    FrameRange FramesInRange(double inPoint, double outPoint, const FrameRate& rate);
}  // namespace iris
