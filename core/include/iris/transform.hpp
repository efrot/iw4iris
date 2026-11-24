// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Keyframed transforms of scene objects (actors, props, graphics). Each
// channel is an ordinary ScalarCurve, so the graph editor, eases and the
// timeline work on them like on any other track. Evaluation is a pure
// function of cinematic time.
#pragma once

#include <vector>

#include "iris/curve.hpp"
#include "iris/math.hpp"

namespace iris
{
    struct Transform
    {
        Vec3 position;
        EulerAngles angles;
        Vec3 scale{1, 1, 1};
        bool operator==(const Transform&) const = default;
    };

    class TransformTrack
    {
       public:
        ScalarCurve x, y, z;
        ScalarCurve pitch, yaw, roll;
        ScalarCurve scaleX, scaleY, scaleZ;

        bool Empty() const;
        // Keys every channel at t (scale only when `withScale`). Angles are
        // unwrapped against the curve so the object turns the short way.
        void SetKey(double t, const Transform& transform, Interpolation interpolation = Interpolation::Smooth,
                    bool withScale = false);
        // Removes the keys of every channel at t (within epsilon).
        bool RemoveKeysAt(double t, double epsilon = 1e-6);
        // Channels without keys take the rest transform's value.
        Transform Evaluate(double t, const Transform& rest) const;
        // Sorted, de-duplicated key times of all channels.
        std::vector<double> KeyTimes() const;
        void Shift(double dt);
        void Clear();
        void SetInterpolation(Interpolation interpolation);

        bool operator==(const TransformTrack& o) const;

        template <typename F>
        void ForEachChannel(F&& f)
        {
            for (ScalarCurve* c : {&x, &y, &z, &pitch, &yaw, &roll, &scaleX, &scaleY, &scaleZ})
                f(*c);
        }
        template <typename F>
        void ForEachChannel(F&& f) const
        {
            for (const ScalarCurve* c : {&x, &y, &z, &pitch, &yaw, &roll, &scaleX, &scaleY, &scaleZ})
                f(*c);
        }
    };
}  // namespace iris
