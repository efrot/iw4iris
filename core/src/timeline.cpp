// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "iris/math.hpp"

namespace iris
{
    double FrameRate::FrameToSeconds(std::int64_t frame) const
    {
        if (!Valid())
            return 0.0;
        // Split to keep precision for long timelines.
        const std::int64_t whole = frame / numerator;
        const std::int64_t rest = frame % numerator;
        return static_cast<double>(whole * denominator) +
               static_cast<double>(rest * denominator) / static_cast<double>(numerator);
    }

    std::int64_t FrameRate::SecondsToFrame(double seconds) const
    {
        if (!Valid())
            return 0;
        const double frames = seconds * Fps();
        return static_cast<std::int64_t>(std::floor(frames + 1e-6));
    }

    std::int64_t FrameRate::RoundToFrame(double seconds) const
    {
        if (!Valid())
            return 0;
        return static_cast<std::int64_t>(std::llround(seconds * Fps()));
    }

    FrameRate FrameRate::FromFps(double fps)
    {
        if (fps <= 0.0)
            return {60, 1};
        static const std::int64_t ntscBases[] = {24, 30, 48, 60, 120, 240};
        for (const auto base : ntscBases)
        {
            const double ntsc = static_cast<double>(base) * 1000.0 / 1001.0;
            if (std::abs(fps - ntsc) < 0.005)
                return {base * 1000, 1001};
        }
        const double rounded = std::round(fps);
        if (std::abs(fps - rounded) < 1e-6)
            return {static_cast<std::int64_t>(rounded), 1};
        // Arbitrary rate: keep three decimals exactly.
        std::int64_t num = std::llround(fps * 1000.0), den = 1000;
        const std::int64_t g = std::gcd(num, den);
        return {num / g, den / g};
    }

    std::string FrameRate::ToString() const
    {
        if (denominator == 1)
            return std::to_string(numerator);
        if (denominator == 1001)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f", Fps());
            return buf;
        }
        return std::to_string(numerator) + "/" + std::to_string(denominator);
    }

    const std::vector<FrameRate>& StandardFrameRates()
    {
        static const std::vector<FrameRate> rates{{24000, 1001}, {24, 1},  {25, 1},  {30000, 1001}, {30, 1},
                                                  {50, 1},       {60000, 1001}, {60, 1}, {120, 1},    {240, 1}};
        return rates;
    }

    std::string FormatTime(double seconds, const FrameRate& rate, TimeDisplay display)
    {
        char buf[64];
        const bool negative = seconds < 0.0;
        const double s = std::abs(seconds);
        switch (display)
        {
            case TimeDisplay::Seconds:
                std::snprintf(buf, sizeof(buf), "%s%.3fs", negative ? "-" : "", s);
                return buf;
            case TimeDisplay::Frames:
                std::snprintf(buf, sizeof(buf), "%s%lld", negative ? "-" : "",
                              static_cast<long long>(rate.SecondsToFrame(s)));
                return buf;
            case TimeDisplay::Timecode:
            default:
            {
                const std::int64_t frame = rate.SecondsToFrame(s);
                const std::int64_t fps = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(rate.Fps())));
                const std::int64_t ff = frame % fps;
                const std::int64_t total = frame / fps;
                std::snprintf(buf, sizeof(buf), "%s%02lld:%02lld:%02lld:%0*lld", negative ? "-" : "",
                              static_cast<long long>(total / 3600), static_cast<long long>((total / 60) % 60),
                              static_cast<long long>(total % 60), fps > 99 ? 3 : 2, static_cast<long long>(ff));
                return buf;
            }
        }
    }

    void CameraCutTrack::SetCuts(std::vector<CameraCut> cuts)
    {
        cuts_.clear();
        for (const auto& c : cuts)
            AddCut(c);
    }

    void CameraCutTrack::AddCut(const CameraCut& cut)
    {
        auto it = std::lower_bound(cuts_.begin(), cuts_.end(), cut.time,
                                   [](const CameraCut& c, double t) { return c.time < t; });
        if (it != cuts_.end() && std::abs(it->time - cut.time) < 1e-9)
        {
            *it = cut;
            return;
        }
        cuts_.insert(it, cut);
    }

    bool CameraCutTrack::RemoveCutAt(std::size_t index)
    {
        if (index >= cuts_.size())
            return false;
        cuts_.erase(cuts_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    std::optional<std::string> CameraCutTrack::CameraAt(double t) const
    {
        if (cuts_.empty())
            return std::nullopt;
        auto it = std::upper_bound(cuts_.begin(), cuts_.end(), t,
                                   [](double time, const CameraCut& c) { return time < c.time; });
        if (it == cuts_.begin())
            return cuts_.front().cameraId;
        return (it - 1)->cameraId;
    }

    double TimeRemap::SpeedAt(double cinematicTime) const
    {
        return std::max(0.0, speed_.Evaluate(cinematicTime));
    }

    double TimeRemap::SceneTime(double cinematicTime) const
    {
        if (speed_.Empty())
            return sceneOffset + (cinematicTime - timelineStart) * std::max(0.0, speed_.DefaultValue());
        if (cinematicTime >= timelineStart)
            return sceneOffset + std::max(0.0, speed_.Integrate(timelineStart, cinematicTime));
        return sceneOffset - std::max(0.0, speed_.Integrate(cinematicTime, timelineStart));
    }

    double TimeRemap::CinematicTime(double sceneTime, double searchEnd) const
    {
        // SceneTime is monotonic non-decreasing; bisection finds the earliest
        // cinematic time reaching sceneTime.
        double lo = timelineStart, hi = std::max(searchEnd, timelineStart);
        if (SceneTime(lo) >= sceneTime)
            return lo;
        if (SceneTime(hi) < sceneTime)
            return hi;
        for (int i = 0; i < 80; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            if (SceneTime(mid) < sceneTime)
                lo = mid;
            else
                hi = mid;
        }
        return hi;
    }

    void TimelineSettings::Normalize()
    {
        if (end < start)
            std::swap(start, end);
        inPoint = Clamp(inPoint, start, end);
        outPoint = Clamp(outPoint, start, end);
        if (outPoint < inPoint)
            std::swap(inPoint, outPoint);
        loop.start = Clamp(loop.start, start, end);
        loop.end = Clamp(loop.end, start, end);
        if (loop.end < loop.start)
            std::swap(loop.start, loop.end);
        if (!frameRate.Valid())
            frameRate = {60, 1};
    }

    double TimelineSettings::Advance(double playhead, double dt) const
    {
        double next = playhead + dt;
        if (loop.enabled && loop.end - loop.start > 1e-9 && playhead <= loop.end + 1e-9 && next >= loop.end)
        {
            const double len = loop.end - loop.start;
            next = loop.start + std::fmod(next - loop.start, len);
        }
        return Clamp(next, start, end);
    }

    FrameRange FramesInRange(double inPoint, double outPoint, const FrameRate& rate)
    {
        FrameRange r;
        if (!rate.Valid() || outPoint <= inPoint)
            return r;
        // First frame whose start time is >= inPoint, last frame starting
        // before outPoint.
        std::int64_t first = rate.SecondsToFrame(inPoint);
        if (rate.FrameToSeconds(first) < inPoint - 1e-9)
            ++first;
        std::int64_t endFrame = rate.SecondsToFrame(outPoint);
        if (rate.FrameToSeconds(endFrame) < outPoint - 1e-9)
            ++endFrame;
        r.first = first;
        r.count = std::max<std::int64_t>(0, endFrame - first);
        return r;
    }
}  // namespace iris
