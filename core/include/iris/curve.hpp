// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Keyframed scalar curves, the model behind the timeline's value tracks and
// the graph editor (FOV, focus distance, aperture, playback speed, light
// intensity, ...). Time is cinematic time in seconds.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iris
{
    // How a key's outgoing segment is interpolated (towards the next key).
    enum class Interpolation
    {
        Hold,        // keep this key's value until the next key (stepped)
        Linear,      // straight line
        Smooth,      // Hermite with automatic tangents (C1 across keys)
        CatmullRom,  // centripetal Catmull-Rom (positions) / Smooth (scalars)
        Bezier,      // cubic Bézier using the keys' handles (graph editor)
    };

    std::string_view ToString(Interpolation interpolation);
    std::optional<Interpolation> InterpolationFromString(std::string_view text);

    // How a key's two Bézier handles relate to each other.
    enum class TangentMode
    {
        Auto,     // computed from the neighbours, handles follow the key
        Aligned,  // free length, colinear in/out
        Free,     // fully independent (broken) handles
        Flat,     // horizontal handles (zero slope)
    };

    std::string_view ToString(TangentMode mode);
    std::optional<TangentMode> TangentModeFromString(std::string_view text);

    // A Bézier handle relative to its key: dt in seconds (in-handle dt <= 0,
    // out-handle dt >= 0) and dv in the curve's unit.
    struct Handle
    {
        double dt = 0.0;
        double dv = 0.0;
        bool operator==(const Handle&) const = default;
    };

    // Temporal easing (After Effects style "influence"): 0 is no easing,
    // 1 is a fully eased stop at the key. Applies to every interpolation but
    // Hold and Bezier (which has explicit handles instead).
    struct Ease
    {
        double in = 0.0;   // easing as the curve arrives at this key
        double out = 0.0;  // easing as the curve leaves this key
        bool operator==(const Ease&) const = default;
    };

    struct ScalarKey
    {
        double time = 0.0;
        double value = 0.0;
        Interpolation interpolation = Interpolation::Smooth;
        TangentMode tangentMode = TangentMode::Auto;
        Ease ease;
        Handle inHandle{-0.25, 0.0};
        Handle outHandle{0.25, 0.0};
        bool operator==(const ScalarKey&) const = default;
    };

    // Cubic-bezier timing function (CSS cubic-bezier(x1, y1, x2, y2)) mapping
    // [0,1] to [0,1]; x1 and x2 are clamped to [0,1] so it stays monotonic.
    double CubicBezierTiming(double x1, double y1, double x2, double y2, double x);

    // Maps a segment's linear time fraction to the eased fraction given the
    // ease leaving the first key and arriving at the second.
    double ApplyEase(double fraction, double easeOut, double easeIn);

    class ScalarCurve
    {
       public:
        ScalarCurve() = default;
        explicit ScalarCurve(double defaultValue) : defaultValue_(defaultValue) {}

        // Keys stay sorted by time; a key added at the time of an existing one
        // (within 1e-9 s) replaces it. Returns its index.
        std::size_t AddKey(const ScalarKey& key);
        bool RemoveKeyAt(std::size_t index);
        void Clear() { keys_.clear(); }
        // Moves a key to a new time, keeping the order. Returns its new index.
        std::size_t MoveKey(std::size_t index, double newTime);
        void SetKeys(std::vector<ScalarKey> keys);

        const std::vector<ScalarKey>& Keys() const { return keys_; }
        std::vector<ScalarKey>& MutableKeys() { return keys_; }
        bool Empty() const { return keys_.empty(); }
        std::size_t Size() const { return keys_.size(); }

        double DefaultValue() const { return defaultValue_; }
        void SetDefaultValue(double v) { defaultValue_ = v; }

        // Value at time t. Before the first key / after the last the curve
        // holds the end values; with no keys it is the default value.
        double Evaluate(double t) const;
        // First derivative (value per second), numerically.
        double Slope(double t) const;

        // Recomputes the handles of Auto and Flat keys from their neighbours
        // (called after edits; Evaluate also works without it).
        void UpdateAutoHandles();

        // Definite integral of the curve between a and b (a <= b), used for
        // time remapping by a playback speed curve.
        double Integrate(double a, double b) const;

        // Index of the segment containing t (key i to i+1), none before the
        // first or after the last key.
        std::optional<std::size_t> SegmentAt(double t) const;

       private:
        double EvaluateSegment(std::size_t i, double t) const;
        double AutoSlope(std::size_t i) const;

        std::vector<ScalarKey> keys_;
        double defaultValue_ = 0.0;
    };
}  // namespace iris
