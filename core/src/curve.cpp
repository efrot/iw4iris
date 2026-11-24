// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/curve.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "iris/math.hpp"

namespace iris
{
    std::string_view ToString(Interpolation interpolation)
    {
        switch (interpolation)
        {
            case Interpolation::Hold: return "hold";
            case Interpolation::Linear: return "linear";
            case Interpolation::Smooth: return "smooth";
            case Interpolation::CatmullRom: return "catmullRom";
            case Interpolation::Bezier: return "bezier";
        }
        return "smooth";
    }

    std::optional<Interpolation> InterpolationFromString(std::string_view text)
    {
        if (text == "hold") return Interpolation::Hold;
        if (text == "linear") return Interpolation::Linear;
        if (text == "smooth") return Interpolation::Smooth;
        if (text == "catmullRom") return Interpolation::CatmullRom;
        if (text == "bezier") return Interpolation::Bezier;
        return std::nullopt;
    }

    std::string_view ToString(TangentMode mode)
    {
        switch (mode)
        {
            case TangentMode::Auto: return "auto";
            case TangentMode::Aligned: return "aligned";
            case TangentMode::Free: return "free";
            case TangentMode::Flat: return "flat";
        }
        return "auto";
    }

    std::optional<TangentMode> TangentModeFromString(std::string_view text)
    {
        if (text == "auto") return TangentMode::Auto;
        if (text == "aligned") return TangentMode::Aligned;
        if (text == "free") return TangentMode::Free;
        if (text == "flat") return TangentMode::Flat;
        return std::nullopt;
    }

    namespace
    {
        double BezierComponent(double p0, double p1, double p2, double p3, double u)
        {
            const double v = 1.0 - u;
            return v * v * v * p0 + 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u * p3;
        }

        double BezierDerivative(double p0, double p1, double p2, double p3, double u)
        {
            const double v = 1.0 - u;
            return 3.0 * v * v * (p1 - p0) + 6.0 * v * u * (p2 - p1) + 3.0 * u * u * (p3 - p2);
        }

        // Solves x(u) = x for a monotonic cubic Bézier in [0,1]: Newton first,
        // bisection as the fallback so it always converges.
        double SolveBezierParameter(double x0, double x1, double x2, double x3, double x)
        {
            if (x <= x0)
                return 0.0;
            if (x >= x3)
                return 1.0;
            double u = (x - x0) / (x3 - x0);
            for (int i = 0; i < 8; ++i)
            {
                const double fx = BezierComponent(x0, x1, x2, x3, u) - x;
                if (std::abs(fx) < 1e-12)
                    return u;
                const double d = BezierDerivative(x0, x1, x2, x3, u);
                if (std::abs(d) < 1e-12)
                    break;
                const double next = u - fx / d;
                if (next < 0.0 || next > 1.0)
                    break;
                u = next;
            }
            double lo = 0.0, hi = 1.0;
            u = 0.5;
            for (int i = 0; i < 64; ++i)
            {
                const double fx = BezierComponent(x0, x1, x2, x3, u);
                if (std::abs(fx - x) < 1e-13)
                    break;
                if (fx < x)
                    lo = u;
                else
                    hi = u;
                u = 0.5 * (lo + hi);
            }
            return u;
        }

        double Hermite(double p0, double m0, double p1, double m1, double u)
        {
            const double u2 = u * u, u3 = u2 * u;
            return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
        }
    }  // namespace

    double CubicBezierTiming(double x1, double y1, double x2, double y2, double x)
    {
        x1 = Clamp(x1, 0.0, 1.0);
        x2 = Clamp(x2, 0.0, 1.0);
        x = Clamp(x, 0.0, 1.0);
        const double u = SolveBezierParameter(0.0, x1, x2, 1.0, x);
        return BezierComponent(0.0, y1, y2, 1.0, u);
    }

    double ApplyEase(double fraction, double easeOut, double easeIn)
    {
        easeOut = Clamp(easeOut, 0.0, 1.0);
        easeIn = Clamp(easeIn, 0.0, 1.0);
        if (easeOut <= 0.0 && easeIn <= 0.0)
            return Clamp(fraction, 0.0, 1.0);
        // An ease of 0 keeps a straight timing tangent (x1 = y1); an ease of 1
        // flattens it completely (y = 0 at the handle).
        const double x1 = easeOut > 0.0 ? easeOut : 1.0 / 3.0;
        const double y1 = easeOut > 0.0 ? 0.0 : 1.0 / 3.0;
        const double x2 = easeIn > 0.0 ? 1.0 - easeIn : 2.0 / 3.0;
        const double y2 = easeIn > 0.0 ? 1.0 : 2.0 / 3.0;
        return CubicBezierTiming(x1, y1, x2, y2, fraction);
    }

    std::size_t ScalarCurve::AddKey(const ScalarKey& key)
    {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key.time,
                                   [](const ScalarKey& k, double t) { return k.time < t; });
        if (it != keys_.end() && std::abs(it->time - key.time) < 1e-9)
        {
            *it = key;
            return static_cast<std::size_t>(it - keys_.begin());
        }
        if (it != keys_.begin() && std::abs((it - 1)->time - key.time) < 1e-9)
        {
            *(it - 1) = key;
            return static_cast<std::size_t>(it - 1 - keys_.begin());
        }
        it = keys_.insert(it, key);
        return static_cast<std::size_t>(it - keys_.begin());
    }

    bool ScalarCurve::RemoveKeyAt(std::size_t index)
    {
        if (index >= keys_.size())
            return false;
        keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    std::size_t ScalarCurve::MoveKey(std::size_t index, double newTime)
    {
        if (index >= keys_.size())
            return index;
        ScalarKey key = keys_[index];
        keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(index));
        key.time = newTime;
        return AddKey(key);
    }

    void ScalarCurve::SetKeys(std::vector<ScalarKey> keys)
    {
        std::stable_sort(keys.begin(), keys.end(), [](const ScalarKey& a, const ScalarKey& b) { return a.time < b.time; });
        keys_.clear();
        for (const auto& k : keys)
            AddKey(k);
    }

    std::optional<std::size_t> ScalarCurve::SegmentAt(double t) const
    {
        if (keys_.size() < 2 || t < keys_.front().time || t >= keys_.back().time)
            return std::nullopt;
        auto it = std::upper_bound(keys_.begin(), keys_.end(), t,
                                   [](double time, const ScalarKey& k) { return time < k.time; });
        return static_cast<std::size_t>(it - keys_.begin()) - 1;
    }

    double ScalarCurve::AutoSlope(std::size_t i) const
    {
        const auto& k = keys_[i];
        if (k.tangentMode == TangentMode::Flat || keys_.size() < 2)
            return 0.0;
        if (i == 0)
        {
            const auto& n = keys_[1];
            return (n.value - k.value) / std::max(n.time - k.time, 1e-9);
        }
        if (i + 1 == keys_.size())
        {
            const auto& p = keys_[i - 1];
            return (k.value - p.value) / std::max(k.time - p.time, 1e-9);
        }
        const auto& p = keys_[i - 1];
        const auto& n = keys_[i + 1];
        const double h0 = std::max(k.time - p.time, 1e-9);
        const double h1 = std::max(n.time - k.time, 1e-9);
        const double d0 = (k.value - p.value) / h0;
        const double d1 = (n.value - k.value) / h1;
        // Hold the value at local extrema (no overshoot), otherwise the
        // Bessel tangent (weighted three-point difference).
        if (d0 * d1 <= 0.0)
            return 0.0;
        return (d0 * h1 + d1 * h0) / (h0 + h1);
    }

    void ScalarCurve::UpdateAutoHandles()
    {
        for (std::size_t i = 0; i < keys_.size(); ++i)
        {
            auto& k = keys_[i];
            if (k.tangentMode != TangentMode::Auto && k.tangentMode != TangentMode::Flat)
                continue;
            const double slope = AutoSlope(i);
            const double before = i > 0 ? (k.time - keys_[i - 1].time) / 3.0 : 0.25;
            const double after = i + 1 < keys_.size() ? (keys_[i + 1].time - k.time) / 3.0 : 0.25;
            k.inHandle = {-before, -slope * before};
            k.outHandle = {after, slope * after};
        }
    }

    double ScalarCurve::EvaluateSegment(std::size_t i, double t) const
    {
        const auto& a = keys_[i];
        const auto& b = keys_[i + 1];
        const double span = b.time - a.time;
        if (span <= 1e-12)
            return b.value;
        const double f = Clamp((t - a.time) / span, 0.0, 1.0);

        switch (a.interpolation)
        {
            case Interpolation::Hold:
                return a.value;
            case Interpolation::Linear:
                return Lerp(a.value, b.value, ApplyEase(f, a.ease.out, b.ease.in));
            case Interpolation::Bezier:
            {
                // Handles are clamped inside the segment so x(u) stays monotonic.
                const double outDt = Clamp(a.outHandle.dt, 0.0, span);
                const double inDt = Clamp(-b.inHandle.dt, 0.0, span);
                const double outDv = a.outHandle.dt > 1e-12 ? a.outHandle.dv * (outDt / a.outHandle.dt) : 0.0;
                const double inDv = b.inHandle.dt < -1e-12 ? b.inHandle.dv * (inDt / -b.inHandle.dt) : 0.0;
                const double x0 = a.time, x1 = a.time + outDt, x2 = b.time - inDt, x3 = b.time;
                const double u = SolveBezierParameter(x0, x1, x2, x3, t);
                return BezierComponent(a.value, a.value + outDv, b.value + inDv, b.value, u);
            }
            case Interpolation::Smooth:
            case Interpolation::CatmullRom:
            default:
            {
                const double u = ApplyEase(f, a.ease.out, b.ease.in);
                const double m0 = (a.tangentMode == TangentMode::Auto || a.tangentMode == TangentMode::Flat)
                                      ? AutoSlope(i)
                                      : (a.outHandle.dt > 1e-12 ? a.outHandle.dv / a.outHandle.dt : 0.0);
                const double m1 = (b.tangentMode == TangentMode::Auto || b.tangentMode == TangentMode::Flat)
                                      ? AutoSlope(i + 1)
                                      : (b.inHandle.dt < -1e-12 ? b.inHandle.dv / b.inHandle.dt : 0.0);
                return Hermite(a.value, m0 * span, b.value, m1 * span, u);
            }
        }
    }

    double ScalarCurve::Evaluate(double t) const
    {
        if (keys_.empty())
            return defaultValue_;
        if (keys_.size() == 1 || t <= keys_.front().time)
            return keys_.front().value;
        if (t >= keys_.back().time)
            return keys_.back().value;
        const auto segment = SegmentAt(t);
        return segment ? EvaluateSegment(*segment, t) : keys_.back().value;
    }

    double ScalarCurve::Slope(double t) const
    {
        constexpr double h = 1e-4;
        return (Evaluate(t + h) - Evaluate(t - h)) / (2.0 * h);
    }

    double ScalarCurve::Integrate(double a, double b) const
    {
        if (b <= a)
            return 0.0;
        if (keys_.empty())
            return defaultValue_ * (b - a);

        // Split at key times (the curve is smooth between keys, possibly
        // discontinuous at Hold keys) and integrate each piece with 8-point
        // Gauss-Legendre on sub-intervals.
        static constexpr std::array<double, 8> nodes{-0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                                                     -0.1834346424956498, 0.1834346424956498,  0.5255324099163290,
                                                     0.7966664774136267,  0.9602898564975363};
        static constexpr std::array<double, 8> weights{0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                                                       0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                                                       0.2223810344533745, 0.1012285362903763};
        std::vector<double> cuts{a};
        for (const auto& k : keys_)
            if (k.time > a && k.time < b)
                cuts.push_back(k.time);
        cuts.push_back(b);

        double total = 0.0;
        for (std::size_t c = 0; c + 1 < cuts.size(); ++c)
        {
            const double lo = cuts[c], hi = cuts[c + 1];
            // Evaluate just inside the piece so a Hold key's step lands on the
            // right side.
            const int pieces = std::max(1, static_cast<int>(std::ceil((hi - lo) / 0.25)));
            const double step = (hi - lo) / pieces;
            for (int p = 0; p < pieces; ++p)
            {
                const double s0 = lo + p * step;
                const double mid = s0 + 0.5 * step, half = 0.5 * step;
                double sum = 0.0;
                for (std::size_t n = 0; n < nodes.size(); ++n)
                    sum += weights[n] * Evaluate(mid + half * nodes[n]);
                total += sum * half;
            }
        }
        return total;
    }
}  // namespace iris
