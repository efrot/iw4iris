// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iris
{
    // ------------------------------------------------------------------ lens

    const std::vector<Sensor>& SensorPresets()
    {
        static const std::vector<Sensor> presets{
            {"Full Frame", 36.0, 24.0},
            {"Super 35", 24.89, 18.66},
            {"Super 35 (16:9)", 24.89, 14.0},
            {"APS-C", 23.6, 15.7},
            {"Micro Four Thirds", 17.3, 13.0},
            {"Super 16", 12.52, 7.41},
            {"Large Format (65 mm)", 54.12, 25.58},
        };
        return presets;
    }

    std::optional<Sensor> FindSensor(std::string_view name)
    {
        for (const auto& s : SensorPresets())
            if (s.name == name)
                return s;
        return std::nullopt;
    }

    const std::vector<double>& FocalLengthPresets()
    {
        static const std::vector<double> presets{10, 14, 18, 21, 24, 28, 35, 40, 50, 65, 75, 85, 100, 135, 200, 300};
        return presets;
    }

    double FocalLengthToHorizontalFov(double focalLengthMm, double sensorWidthMm)
    {
        focalLengthMm = std::max(focalLengthMm, 1e-6);
        return 2.0 * std::atan(sensorWidthMm / (2.0 * focalLengthMm)) * kRadToDeg;
    }

    double HorizontalFovToFocalLength(double horizontalFovDeg, double sensorWidthMm)
    {
        const double half = Clamp(horizontalFovDeg, 1e-6, 179.999) * 0.5 * kDegToRad;
        return sensorWidthMm / (2.0 * std::tan(half));
    }

    double HorizontalToVerticalFov(double horizontalFovDeg, double aspect)
    {
        const double half = Clamp(horizontalFovDeg, 1e-6, 179.999) * 0.5 * kDegToRad;
        return 2.0 * std::atan(std::tan(half) / std::max(aspect, 1e-9)) * kRadToDeg;
    }

    double VerticalToHorizontalFov(double verticalFovDeg, double aspect)
    {
        const double half = Clamp(verticalFovDeg, 1e-6, 179.999) * 0.5 * kDegToRad;
        return 2.0 * std::atan(std::tan(half) * aspect) * kRadToDeg;
    }

    double CgFovToHorizontalFov(double cgFovDeg, double aspect)
    {
        // cg_fov is the horizontal FOV at 4:3; the vertical FOV is kept.
        const double vertical = HorizontalToVerticalFov(cgFovDeg, 4.0 / 3.0);
        return VerticalToHorizontalFov(vertical, aspect);
    }

    double HorizontalFovToCgFov(double horizontalFovDeg, double aspect)
    {
        const double vertical = HorizontalToVerticalFov(horizontalFovDeg, aspect);
        return VerticalToHorizontalFov(vertical, 4.0 / 3.0);
    }

    DepthOfFieldRange ComputeDepthOfField(double focalLengthMm, double fNumber, double focusDistanceUnits,
                                          double circleOfConfusionMm, double unitsPerMetre)
    {
        DepthOfFieldRange r;
        const double f = std::max(focalLengthMm, 1e-6);
        const double n = std::max(fNumber, 1e-6);
        const double c = std::max(circleOfConfusionMm, 1e-9);
        const double s = std::max(focusDistanceUnits / unitsPerMetre * 1000.0, f * 1.0001);  // mm
        const double h = f * f / (n * c) + f;  // hyperfocal, mm
        r.hyperfocal = h / 1000.0 * unitsPerMetre;
        r.nearLimit = (s * (h - f) / (h + s - 2.0 * f)) / 1000.0 * unitsPerMetre;
        if (s >= h)
        {
            r.farIsInfinite = true;
            r.farLimit = std::numeric_limits<double>::infinity();
        }
        else
        {
            r.farLimit = (s * (h - f) / (h - s)) / 1000.0 * unitsPerMetre;
        }
        return r;
    }

    double BlurDiameterMm(double focalLengthMm, double fNumber, double focusDistanceUnits, double distanceUnits,
                          double unitsPerMetre)
    {
        const double f = std::max(focalLengthMm, 1e-6);
        const double n = std::max(fNumber, 1e-6);
        const double s = std::max(focusDistanceUnits / unitsPerMetre * 1000.0, f * 1.0001);
        const double d = std::max(distanceUnits / unitsPerMetre * 1000.0, f * 1.0001);
        // c = (f^2 / N) * |d - s| / (d * (s - f))
        return (f * f / n) * std::abs(d - s) / (d * (s - f));
    }

    double Lens::HorizontalFov() const
    {
        // An anamorphic lens squeezes a wider horizontal field onto the sensor.
        return FocalLengthToHorizontalFov(focalLengthMm, sensor.widthMm * std::max(anamorphicSqueeze, 1.0));
    }

    // ------------------------------------------------------------------ path

    std::string_view ToString(RotationMode mode)
    {
        return mode == RotationMode::Euler ? "euler" : "quaternion";
    }

    std::optional<RotationMode> RotationModeFromString(std::string_view text)
    {
        if (text == "euler") return RotationMode::Euler;
        if (text == "quaternion") return RotationMode::Quaternion;
        return std::nullopt;
    }

    std::size_t CameraPath::AddKey(const CameraKey& key)
    {
        InvalidateCache();
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key.time,
                                   [](const CameraKey& k, double t) { return k.time < t; });
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

    bool CameraPath::RemoveKeyAt(std::size_t index)
    {
        if (index >= keys_.size())
            return false;
        keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(index));
        InvalidateCache();
        return true;
    }

    std::size_t CameraPath::MoveKey(std::size_t index, double newTime)
    {
        if (index >= keys_.size())
            return index;
        CameraKey key = keys_[index];
        keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(index));
        key.time = newTime;
        return AddKey(key);
    }

    void CameraPath::SetKeys(std::vector<CameraKey> keys)
    {
        std::stable_sort(keys.begin(), keys.end(), [](const CameraKey& a, const CameraKey& b) { return a.time < b.time; });
        keys_.clear();
        for (const auto& k : keys)
            AddKey(k);
        InvalidateCache();
    }

    std::size_t CameraPath::SegmentIndex(double pathTime) const
    {
        auto it = std::upper_bound(keys_.begin(), keys_.end(), pathTime,
                                   [](double time, const CameraKey& k) { return time < k.time; });
        const auto idx = static_cast<std::size_t>(it - keys_.begin());
        return idx == 0 ? 0 : std::min(idx - 1, keys_.size() - 2);
    }

    Vec3 CameraPath::AutoTangent(std::size_t i) const
    {
        // Velocity (units / second) at key i: Bessel (weighted) tangent,
        // scaled by (1 - tension). Ends use one-sided differences.
        const auto& k = keys_[i];
        Vec3 v;
        if (keys_.size() < 2)
            return v;
        if (i == 0)
        {
            v = (keys_[1].position - k.position) / std::max(keys_[1].time - k.time, 1e-9);
        }
        else if (i + 1 == keys_.size())
        {
            v = (k.position - keys_[i - 1].position) / std::max(k.time - keys_[i - 1].time, 1e-9);
        }
        else
        {
            const double h0 = std::max(k.time - keys_[i - 1].time, 1e-9);
            const double h1 = std::max(keys_[i + 1].time - k.time, 1e-9);
            const Vec3 d0 = (k.position - keys_[i - 1].position) / h0;
            const Vec3 d1 = (keys_[i + 1].position - k.position) / h1;
            v = (d0 * h1 + d1 * h0) / (h0 + h1);
        }
        return v * (1.0 - Clamp(k.tension, 0.0, 1.0));
    }

    Vec3 CameraPath::EvaluatePosition(double pathTime) const
    {
        if (keys_.size() == 1 || pathTime <= keys_.front().time)
            return keys_.front().position;
        if (pathTime >= keys_.back().time)
            return keys_.back().position;

        const std::size_t i = SegmentIndex(pathTime);
        const auto& a = keys_[i];
        const auto& b = keys_[i + 1];
        const double span = std::max(b.time - a.time, 1e-12);
        const double f = Clamp((pathTime - a.time) / span, 0.0, 1.0);

        switch (a.interpolation)
        {
            case Interpolation::Hold:
                return a.position;
            case Interpolation::Linear:
                return Lerp(a.position, b.position, ApplyEase(f, a.ease.out, b.ease.in));
            case Interpolation::Bezier:
            {
                const double u = ApplyEase(f, a.ease.out, b.ease.in);
                const Vec3 p1 = a.position + a.outTangent;
                const Vec3 p2 = b.position + b.inTangent;
                const double v = 1.0 - u;
                return a.position * (v * v * v) + p1 * (3 * v * v * u) + p2 * (3 * v * u * u) + b.position * (u * u * u);
            }
            case Interpolation::CatmullRom:
            {
                // Centripetal Catmull-Rom (alpha = 0.5) through p1..p2, with
                // mirrored phantom points at the ends.
                const double u = ApplyEase(f, a.ease.out, b.ease.in);
                const Vec3 p1 = a.position, p2 = b.position;
                const Vec3 p0 = i > 0 ? keys_[i - 1].position : p1 * 2.0 - p2;
                const Vec3 p3 = i + 2 < keys_.size() ? keys_[i + 2].position : p2 * 2.0 - p1;
                auto knot = [](double t, const Vec3& x, const Vec3& y) {
                    return t + std::max(std::pow(Distance(x, y), 0.5), 1e-6);
                };
                const double t0 = 0.0, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
                const double t = Lerp(t1, t2, u);
                const Vec3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
                const Vec3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
                const Vec3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
                const Vec3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
                const Vec3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
                return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
            }
            case Interpolation::Smooth:
            default:
            {
                const double u = ApplyEase(f, a.ease.out, b.ease.in);
                // Auto-clamped tangents: the Bézier handle a tangent implies
                // (|m| * span / 3) is limited to half the segment's chord, so
                // very uneven key timing cannot make the path loop or
                // overshoot. Directions are kept (the path stays G1).
                const double chord = Distance(a.position, b.position);
                auto clamped = [&](Vec3 m) {
                    const double handle = iris::Length(m) / 3.0;
                    const double maxHandle = 0.5 * chord;
                    return handle > maxHandle && handle > 1e-12 ? m * (maxHandle / handle) : m;
                };
                const Vec3 m0 = clamped(AutoTangent(i) * span);
                const Vec3 m1 = clamped(AutoTangent(i + 1) * span);
                const double u2 = u * u, u3 = u2 * u;
                return a.position * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + b.position * (-2 * u3 + 3 * u2) +
                       m1 * (u3 - u2);
            }
        }
    }

    Quat CameraPath::EvaluateOrientation(double pathTime) const
    {
        auto quatAt = [&](std::size_t i) { return AnglesToQuat(keys_[i].angles); };
        if (keys_.size() == 1 || pathTime <= keys_.front().time)
            return quatAt(0);
        if (pathTime >= keys_.back().time)
            return quatAt(keys_.size() - 1);

        const std::size_t i = SegmentIndex(pathTime);
        const auto& a = keys_[i];
        const auto& b = keys_[i + 1];
        const double span = std::max(b.time - a.time, 1e-12);
        const double f = Clamp((pathTime - a.time) / span, 0.0, 1.0);
        const double u = ApplyEase(f, a.ease.out, b.ease.in);

        Quat q1 = quatAt(i);
        Quat q2 = quatAt(i + 1);
        if (Dot(q1, q2) < 0.0)
            q2 = -q2;

        switch (a.interpolation)
        {
            case Interpolation::Hold:
                return q1;
            case Interpolation::Linear:
                return Slerp(q1, q2, u);
            default:
            {
                Quat q0 = i > 0 ? quatAt(i - 1) : q1;
                Quat q3 = i + 2 < keys_.size() ? quatAt(i + 2) : q2;
                if (Dot(q0, q1) < 0.0)
                    q0 = -q0;
                if (Dot(q3, q2) < 0.0)
                    q3 = -q3;
                const Quat s1 = SquadControlPoint(q0, q1, q2);
                const Quat s2 = SquadControlPoint(q1, q2, q3);
                return Squad(q1, q2, s1, s2, u);
            }
        }
    }

    EulerAngles CameraPath::EvaluateEuler(double pathTime) const
    {
        // Each channel is a scalar curve over the unwrapped key angles.
        auto channel = [&](double EulerAngles::*member) {
            ScalarCurve curve;
            double previous = 0.0;
            for (std::size_t i = 0; i < keys_.size(); ++i)
            {
                const auto& k = keys_[i];
                double value = k.angles.*member;
                if (i > 0)
                    value = UnwrapDegreesNear(value, previous);
                previous = value;
                ScalarKey sk;
                sk.time = k.time;
                sk.value = value;
                sk.interpolation = k.interpolation == Interpolation::Bezier ? Interpolation::Smooth : k.interpolation;
                sk.ease = k.ease;
                curve.AddKey(sk);
            }
            return curve.Evaluate(pathTime);
        };
        return {channel(&EulerAngles::pitch), channel(&EulerAngles::yaw), channel(&EulerAngles::roll)};
    }

    double CameraPath::EvaluateScalar(double pathTime, double CameraKey::*member) const
    {
        ScalarCurve curve;
        for (const auto& k : keys_)
        {
            ScalarKey sk;
            sk.time = k.time;
            sk.value = k.*member;
            sk.interpolation = k.interpolation == Interpolation::Bezier || k.interpolation == Interpolation::CatmullRom
                                   ? Interpolation::Smooth
                                   : k.interpolation;
            sk.ease = k.ease;
            curve.AddKey(sk);
        }
        return curve.Evaluate(pathTime);
    }

    void CameraPath::BuildArcTable() const
    {
        arcTimes_.clear();
        arcLengths_.clear();
        if (keys_.size() < 2)
        {
            arcTableValid_ = true;
            return;
        }
        constexpr int samplesPerSegment = 256;
        double total = 0.0;
        Vec3 previous = EvaluatePosition(keys_.front().time);
        arcTimes_.push_back(keys_.front().time);
        arcLengths_.push_back(0.0);
        for (std::size_t i = 0; i + 1 < keys_.size(); ++i)
        {
            const double t0 = keys_[i].time, t1 = keys_[i + 1].time;
            for (int s = 1; s <= samplesPerSegment; ++s)
            {
                const double t = Lerp(t0, t1, static_cast<double>(s) / samplesPerSegment);
                const Vec3 p = EvaluatePosition(t);
                total += Distance(p, previous);
                previous = p;
                arcTimes_.push_back(t);
                arcLengths_.push_back(total);
            }
        }
        arcTableValid_ = true;
    }

    double CameraPath::Length() const
    {
        if (!arcTableValid_)
            BuildArcTable();
        return arcLengths_.empty() ? 0.0 : arcLengths_.back();
    }

    double CameraPath::PathTimeAt(double t) const
    {
        if (!constantSpeed_ || keys_.size() < 2)
            return t;
        if (!arcTableValid_)
            BuildArcTable();
        const double t0 = keys_.front().time, t1 = keys_.back().time;
        if (t <= t0)
            return t0;
        if (t >= t1)
            return t1;
        const double total = arcLengths_.back();
        if (total <= 1e-9)
            return t;
        // Ease the whole traversal with the first key's ease-out and the last
        // key's ease-in so a constant-speed move can still start and stop
        // softly.
        const double fraction =
            ApplyEase((t - t0) / (t1 - t0), keys_.front().ease.out, keys_.back().ease.in);
        const double target = fraction * total;
        auto it = std::lower_bound(arcLengths_.begin(), arcLengths_.end(), target);
        if (it == arcLengths_.begin())
            return arcTimes_.front();
        if (it == arcLengths_.end())
            return arcTimes_.back();
        const auto idx = static_cast<std::size_t>(it - arcLengths_.begin());
        const double l0 = arcLengths_[idx - 1], l1 = arcLengths_[idx];
        const double w = l1 - l0 > 1e-12 ? (target - l0) / (l1 - l0) : 0.0;
        return Lerp(arcTimes_[idx - 1], arcTimes_[idx], w);
    }

    std::optional<CameraState> CameraPath::Evaluate(double t) const
    {
        if (keys_.empty())
            return std::nullopt;
        const double pt = PathTimeAt(t);
        CameraState s;
        s.position = EvaluatePosition(pt);
        if (rotationMode_ == RotationMode::Euler)
        {
            s.angles = EvaluateEuler(pt);
            s.orientation = AnglesToQuat(s.angles);
        }
        else
        {
            s.orientation = EvaluateOrientation(pt);
            s.angles = QuatToAngles(s.orientation);
        }
        s.horizontalFov = Clamp(EvaluateScalar(pt, &CameraKey::horizontalFov), 1.0, 179.0);
        s.focusDistance = std::max(EvaluateScalar(pt, &CameraKey::focusDistance), 0.0);
        s.fNumber = std::max(EvaluateScalar(pt, &CameraKey::fNumber), 0.5);
        return s;
    }

    std::vector<PathSample> CameraPath::Sample(int perSegment) const
    {
        std::vector<PathSample> out;
        if (keys_.empty())
            return out;
        if (keys_.size() == 1)
        {
            out.push_back({keys_[0].time, keys_[0].position});
            return out;
        }
        perSegment = std::max(perSegment, 1);
        for (std::size_t i = 0; i + 1 < keys_.size(); ++i)
        {
            const double t0 = keys_[i].time, t1 = keys_[i + 1].time;
            for (int s = 0; s < perSegment; ++s)
            {
                const double t = Lerp(t0, t1, static_cast<double>(s) / perSegment);
                out.push_back({t, EvaluatePosition(t)});
            }
        }
        out.push_back({keys_.back().time, keys_.back().position});
        return out;
    }

    void CameraPath::Reverse()
    {
        if (keys_.size() < 2)
            return;
        const double t0 = keys_.front().time, t1 = keys_.back().time;
        std::vector<CameraKey> reversed(keys_.rbegin(), keys_.rend());
        for (auto& k : reversed)
        {
            k.time = t0 + (t1 - k.time);
            std::swap(k.inTangent, k.outTangent);
            std::swap(k.ease.in, k.ease.out);
        }
        // The outgoing interpolation of a segment belonged to its first key,
        // which is now its second: shift them along.
        for (std::size_t i = 0; i + 1 < reversed.size(); ++i)
            reversed[i].interpolation = keys_[keys_.size() - 2 - i].interpolation;
        keys_ = std::move(reversed);
        InvalidateCache();
    }

    void CameraPath::Shift(double dt)
    {
        for (auto& k : keys_)
            k.time += dt;
        InvalidateCache();
    }

    void CameraPath::Retime(double pivot, double factor)
    {
        factor = std::max(factor, 1e-6);
        for (auto& k : keys_)
            k.time = pivot + (k.time - pivot) * factor;
        InvalidateCache();
    }

    void CameraPath::UnwrapAngles()
    {
        for (std::size_t i = 1; i < keys_.size(); ++i)
        {
            auto& a = keys_[i].angles;
            const auto& p = keys_[i - 1].angles;
            a.pitch = UnwrapDegreesNear(a.pitch, p.pitch);
            a.yaw = UnwrapDegreesNear(a.yaw, p.yaw);
            a.roll = UnwrapDegreesNear(a.roll, p.roll);
        }
        InvalidateCache();
    }

    void CameraPath::SmoothPositions(double strength, int iterations)
    {
        strength = Clamp(strength, 0.0, 1.0);
        for (int it = 0; it < iterations && keys_.size() > 2; ++it)
        {
            std::vector<Vec3> next(keys_.size());
            for (std::size_t i = 0; i < keys_.size(); ++i)
            {
                if (i == 0 || i + 1 == keys_.size())
                {
                    next[i] = keys_[i].position;
                    continue;
                }
                const Vec3 average = (keys_[i - 1].position + keys_[i + 1].position) * 0.5;
                next[i] = Lerp(keys_[i].position, average, strength);
            }
            for (std::size_t i = 0; i < keys_.size(); ++i)
                keys_[i].position = next[i];
        }
        InvalidateCache();
    }
}  // namespace iris
