// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/transform.hpp"

#include <algorithm>
#include <cmath>

namespace iris
{
    namespace
    {
        bool SameCurve(const ScalarCurve& a, const ScalarCurve& b)
        {
            return a.Keys() == b.Keys() && a.DefaultValue() == b.DefaultValue();
        }

        void Key(ScalarCurve& c, double t, double v, Interpolation interp)
        {
            ScalarKey k;
            k.time = t;
            k.value = v;
            k.interpolation = interp;
            c.AddKey(k);
            c.UpdateAutoHandles();
        }

        double Unwrapped(const ScalarCurve& c, double t, double degrees)
        {
            return c.Empty() ? degrees : UnwrapDegreesNear(degrees, c.Evaluate(t));
        }
    }  // namespace

    bool TransformTrack::Empty() const
    {
        bool empty = true;
        ForEachChannel([&](const ScalarCurve& c) { empty = empty && c.Empty(); });
        return empty;
    }

    void TransformTrack::SetKey(double t, const Transform& tr, Interpolation interp, bool withScale)
    {
        Key(x, t, tr.position.x, interp);
        Key(y, t, tr.position.y, interp);
        Key(z, t, tr.position.z, interp);
        Key(pitch, t, Unwrapped(pitch, t, tr.angles.pitch), interp);
        Key(yaw, t, Unwrapped(yaw, t, tr.angles.yaw), interp);
        Key(roll, t, Unwrapped(roll, t, tr.angles.roll), interp);
        if (withScale || !scaleX.Empty())
        {
            Key(scaleX, t, tr.scale.x, interp);
            Key(scaleY, t, tr.scale.y, interp);
            Key(scaleZ, t, tr.scale.z, interp);
        }
    }

    bool TransformTrack::RemoveKeysAt(double t, double epsilon)
    {
        bool removed = false;
        ForEachChannel([&](ScalarCurve& c) {
            for (std::size_t i = c.Size(); i-- > 0;)
                if (std::abs(c.Keys()[i].time - t) <= epsilon)
                {
                    c.RemoveKeyAt(i);
                    removed = true;
                }
            c.UpdateAutoHandles();
        });
        return removed;
    }

    Transform TransformTrack::Evaluate(double t, const Transform& rest) const
    {
        auto eval = [t](const ScalarCurve& c, double fallback) { return c.Empty() ? fallback : c.Evaluate(t); };
        Transform out;
        out.position = {eval(x, rest.position.x), eval(y, rest.position.y), eval(z, rest.position.z)};
        out.angles = {eval(pitch, rest.angles.pitch), eval(yaw, rest.angles.yaw), eval(roll, rest.angles.roll)};
        out.scale = {eval(scaleX, rest.scale.x), eval(scaleY, rest.scale.y), eval(scaleZ, rest.scale.z)};
        return out;
    }

    std::vector<double> TransformTrack::KeyTimes() const
    {
        std::vector<double> times;
        ForEachChannel([&](const ScalarCurve& c) {
            for (const auto& k : c.Keys())
                times.push_back(k.time);
        });
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }),
                    times.end());
        return times;
    }

    void TransformTrack::Shift(double dt)
    {
        ForEachChannel([&](ScalarCurve& c) {
            auto keys = c.Keys();
            for (auto& k : keys)
                k.time += dt;
            c.SetKeys(std::move(keys));
            c.UpdateAutoHandles();
        });
    }

    void TransformTrack::Clear()
    {
        ForEachChannel([](ScalarCurve& c) { c.Clear(); });
    }

    void TransformTrack::SetInterpolation(Interpolation interp)
    {
        ForEachChannel([&](ScalarCurve& c) {
            for (auto& k : c.MutableKeys())
                k.interpolation = interp;
            c.UpdateAutoHandles();
        });
    }

    bool TransformTrack::operator==(const TransformTrack& o) const
    {
        return SameCurve(x, o.x) && SameCurve(y, o.y) && SameCurve(z, o.z) && SameCurve(pitch, o.pitch) &&
               SameCurve(yaw, o.yaw) && SameCurve(roll, o.roll) && SameCurve(scaleX, o.scaleX) &&
               SameCurve(scaleY, o.scaleY) && SameCurve(scaleZ, o.scaleZ);
    }
}  // namespace iris
