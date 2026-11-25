// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/timeline_view.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace iris
{
    namespace
    {
        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

        // A non-negative decimal number ("12", "12.5", ".5"); nothing else.
        std::optional<double> Number(std::string_view s)
        {
            if (s.empty())
                return std::nullopt;
            bool digit = false, dot = false;
            for (const char c : s)
            {
                if (c >= '0' && c <= '9')
                    digit = true;
                else if (c == '.' && !dot)
                    dot = true;
                else
                    return std::nullopt;
            }
            if (!digit)
                return std::nullopt;
            // Parsed by hand: strtod follows the C locale's decimal separator.
            double value = 0.0, scale = 1.0;
            bool fraction = false;
            for (const char c : s)
            {
                if (c == '.')
                    fraction = true;
                else if (fraction)
                {
                    scale *= 0.1;
                    value += (c - '0') * scale;
                }
                else
                    value = value * 10.0 + (c - '0');
            }
            return value;
        }

        std::optional<std::int64_t> Integer(std::string_view s)
        {
            if (s.empty())
                return std::nullopt;
            std::int64_t v = 0;
            const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
            if (ec != std::errc{} || p != s.data() + s.size() || v < 0)
                return std::nullopt;
            return v;
        }
    }  // namespace

    std::optional<double> ParseTime(std::string_view text, const FrameRate& rate, TimeDisplay display)
    {
        if (!rate.Valid())
            return std::nullopt;
        auto s = Trim(text);
        bool negative = false;
        if (!s.empty() && s.front() == '-')
        {
            negative = true;
            s = Trim(s.substr(1));
        }
        if (s.empty())
            return std::nullopt;
        const auto sign = [&](double v) { return negative ? -v : v; };

        if (s.back() == 'f' || s.back() == 'F')
        {
            const auto frames = Integer(Trim(s.substr(0, s.size() - 1)));
            if (!frames)
                return std::nullopt;
            return sign(rate.FrameToSeconds(*frames));
        }
        if (s.back() == 's' || s.back() == 'S')
        {
            const auto v = Number(Trim(s.substr(0, s.size() - 1)));
            if (!v)
                return std::nullopt;
            return sign(*v);
        }

        std::vector<std::string_view> parts;
        for (std::size_t from = 0;;)
        {
            const auto colon = s.find(':', from);
            parts.push_back(s.substr(from, colon == std::string_view::npos ? std::string_view::npos : colon - from));
            if (colon == std::string_view::npos)
                break;
            from = colon + 1;
        }
        if (parts.size() > 4)
            return std::nullopt;
        if (parts.size() == 1)
        {
            if (display == TimeDisplay::Frames)
                if (const auto frames = Integer(s))
                    return sign(rate.FrameToSeconds(*frames));
            const auto v = Number(s);
            if (!v)
                return std::nullopt;
            return sign(*v);
        }
        if (parts.size() == 4)
        {
            // HH:MM:SS:FF
            std::int64_t v[4];
            for (int i = 0; i < 4; ++i)
            {
                const auto n = Integer(parts[static_cast<std::size_t>(i)]);
                if (!n)
                    return std::nullopt;
                v[i] = *n;
            }
            const auto fps = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(rate.Fps())));
            if (v[1] >= 60 || v[2] >= 60 || v[3] >= fps)
                return std::nullopt;
            const std::int64_t frame = ((v[0] * 60 + v[1]) * 60 + v[2]) * fps + v[3];
            return sign(rate.FrameToSeconds(frame));
        }
        // M:S(.s) or H:M:S(.s): whole leading fields, the last may have decimals.
        double total = 0.0;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            const bool last = i + 1 == parts.size();
            double value;
            if (last)
            {
                const auto v = Number(parts[i]);
                if (!v || *v >= 60.0)
                    return std::nullopt;
                value = *v;
            }
            else
            {
                const auto n = Integer(parts[i]);
                if (!n || (i > 0 && *n >= 60))
                    return std::nullopt;
                value = static_cast<double>(*n);
            }
            total = total * 60.0 + value;
        }
        return sign(total);
    }

    TimeWindow ClampWindow(TimeWindow w, const TimeWindowLimits& limits)
    {
        if (!std::isfinite(w.start) || !std::isfinite(w.end))
            w = {limits.boundsStart, limits.boundsEnd};
        if (w.end < w.start)
            std::swap(w.start, w.end);
        const double minSpan = std::max(1e-4, limits.minSpan);
        const double maxSpan = std::max(minSpan, limits.maxSpan);
        double span = std::clamp(w.Span(), minSpan, maxSpan);
        const double center = 0.5 * (w.start + w.end);
        w = {center - 0.5 * span, center + 0.5 * span};
        // Keep at least a quarter of the window over the bounds.
        const double keep = 0.25 * span;
        const double lo = std::min(limits.boundsStart, limits.boundsEnd);
        const double hi = std::max(limits.boundsStart, limits.boundsEnd);
        if (w.end < lo + keep)
        {
            const double d = (lo + keep) - w.end;
            w.start += d;
            w.end += d;
        }
        if (w.start > hi - keep)
        {
            const double d = w.start - (hi - keep);
            w.start -= d;
            w.end -= d;
        }
        return w;
    }

    TimeWindow ZoomWindow(TimeWindow w, double pivot, double factor, const TimeWindowLimits& limits)
    {
        if (!(factor > 0.0) || !std::isfinite(factor))
            return ClampWindow(w, limits);
        const double minSpan = std::max(1e-4, limits.minSpan);
        const double maxSpan = std::max(minSpan, limits.maxSpan);
        const double span = w.Span();
        const double target = std::clamp(span * factor, minSpan, maxSpan);
        if (span > 0.0)
            factor = target / span;
        pivot = std::clamp(pivot, w.start, w.end);
        TimeWindow z{pivot - (pivot - w.start) * factor, pivot + (w.end - pivot) * factor};
        return ClampWindow(z, limits);
    }

    TimeWindow PanWindow(TimeWindow w, double delta, const TimeWindowLimits& limits)
    {
        if (std::isfinite(delta))
        {
            w.start += delta;
            w.end += delta;
        }
        return ClampWindow(w, limits);
    }

    TimeWindow FollowTime(TimeWindow w, double t, const TimeWindowLimits& limits, double margin)
    {
        const double span = w.Span();
        if (span <= 0.0 || !std::isfinite(t))
            return w;
        margin = std::clamp(margin, 0.0, 0.45);
        if (t >= w.start && t <= w.end - margin * span)
            return w;
        // Page so t sits a margin after the start.
        const double start = t - margin * span;
        return ClampWindow({start, start + span}, limits);
    }

    TimeWindow WindowAround(double a, double b, const TimeWindowLimits& limits, double padding)
    {
        if (b < a)
            std::swap(a, b);
        double span = b - a;
        if (span < limits.minSpan)
        {
            const double c = 0.5 * (a + b);
            a = c - 0.5 * limits.minSpan;
            b = c + 0.5 * limits.minSpan;
            span = limits.minSpan;
        }
        const double pad = std::max(0.0, padding) * span;
        return ClampWindow({a - pad, b + pad}, limits);
    }

    double SnapToNearest(double t, const std::vector<double>& candidates, double toleranceSeconds, bool* snapped)
    {
        double best = t, bestDistance = toleranceSeconds;
        bool found = false;
        for (const double c : candidates)
        {
            const double d = std::abs(c - t);
            if (d <= bestDistance)
            {
                best = c;
                bestDistance = d;
                found = true;
            }
        }
        if (snapped)
            *snapped = found;
        return best;
    }

    std::optional<double> PreviousPoint(double t, const std::vector<double>& candidates, double epsilon)
    {
        std::optional<double> best;
        for (const double c : candidates)
            if (c < t - epsilon && (!best || c > *best))
                best = c;
        return best;
    }

    std::optional<double> NextPoint(double t, const std::vector<double>& candidates, double epsilon)
    {
        std::optional<double> best;
        for (const double c : candidates)
            if (c > t + epsilon && (!best || c < *best))
                best = c;
        return best;
    }

    InOutRange MoveRange(InOutRange range, double delta, double start, double end)
    {
        if (range.out < range.in)
            std::swap(range.in, range.out);
        if (end < start)
            std::swap(start, end);
        const double length = std::min(range.out - range.in, end - start);
        double in = range.in + (std::isfinite(delta) ? delta : 0.0);
        in = std::clamp(in, start, end - length);
        return {in, in + length};
    }
}  // namespace iris
