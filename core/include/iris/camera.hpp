// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Camera state, lens model and keyframed camera paths (dolly cameras).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/curve.hpp"
#include "iris/math.hpp"

namespace iris
{
    // ---------------------------------------------------------------- lens

    // Physical sensor (film back) size in millimetres.
    struct Sensor
    {
        std::string name = "Super 35";
        double widthMm = 24.89;
        double heightMm = 18.66;
        bool operator==(const Sensor&) const = default;
    };

    const std::vector<Sensor>& SensorPresets();
    std::optional<Sensor> FindSensor(std::string_view name);

    // Common prime focal lengths from ultra-wide to telephoto, in mm.
    const std::vector<double>& FocalLengthPresets();

    // Horizontal angle of view (degrees) of a lens on a sensor width.
    double FocalLengthToHorizontalFov(double focalLengthMm, double sensorWidthMm);
    double HorizontalFovToFocalLength(double horizontalFovDeg, double sensorWidthMm);
    // Converts between horizontal and vertical angle of view for an aspect
    // ratio (width / height).
    double HorizontalToVerticalFov(double horizontalFovDeg, double aspect);
    double VerticalToHorizontalFov(double verticalFovDeg, double aspect);

    // IW4's cg_fov is the horizontal FOV of a 4:3 view; wider screens get a
    // wider horizontal FOV at the same vertical FOV. These convert between
    // cg_fov and the true horizontal FOV of a view with the given aspect.
    double CgFovToHorizontalFov(double cgFovDeg, double aspect);
    double HorizontalFovToCgFov(double horizontalFovDeg, double aspect);

    // Thin-lens depth of field. Distances in game units, focal length in mm,
    // circle of confusion in mm (default: Super 35, 0.025 mm).
    struct DepthOfFieldRange
    {
        double nearLimit = 0.0;   // nearest acceptably sharp distance
        double farLimit = 0.0;    // farthest (infinity when beyond hyperfocal)
        double hyperfocal = 0.0;  // hyperfocal distance
        bool farIsInfinite = false;
    };

    // `unitsPerMetre` converts the scene's game units to metres (1 inch per
    // unit: 39.37 units per metre).
    DepthOfFieldRange ComputeDepthOfField(double focalLengthMm, double fNumber, double focusDistanceUnits,
                                          double circleOfConfusionMm = 0.025, double unitsPerMetre = 39.37007874);

    // Blur disc diameter (mm on the sensor) of a point at `distanceUnits` for a
    // lens focused at `focusDistanceUnits` (thin lens).
    double BlurDiameterMm(double focalLengthMm, double fNumber, double focusDistanceUnits, double distanceUnits,
                          double unitsPerMetre = 39.37007874);

    struct Lens
    {
        double focalLengthMm = 35.0;
        Sensor sensor;
        double fNumber = 2.8;              // aperture (f-stop)
        double focusDistance = 256.0;      // game units
        double anamorphicSqueeze = 1.0;    // 1 = spherical, 1.33 / 1.5 / 2.0 anamorphic
        double breathing = 0.0;            // FOV change per focus pull, 0..1 (simulation, not physical)
        bool operator==(const Lens&) const = default;

        double HorizontalFov() const;
    };

    // ---------------------------------------------------------------- camera

    // The fully resolved camera for one instant.
    struct CameraState
    {
        Vec3 position;
        EulerAngles angles;
        Quat orientation;
        double horizontalFov = 90.0;  // degrees, true horizontal FOV of the output
        double focusDistance = 256.0;
        double fNumber = 2.8;
        bool operator==(const CameraState&) const = default;
    };

    // Rotation interpolation of a path.
    enum class RotationMode
    {
        Quaternion,  // shortest-arc (slerp / squad); no gimbal problems
        Euler,       // per-channel with unwrapping; allows multi-turn spins
    };
    std::string_view ToString(RotationMode mode);
    std::optional<RotationMode> RotationModeFromString(std::string_view text);

    struct CameraKey
    {
        double time = 0.0;  // cinematic time, seconds
        Vec3 position;
        EulerAngles angles;
        double horizontalFov = 90.0;
        double focusDistance = 256.0;
        double fNumber = 2.8;

        Interpolation interpolation = Interpolation::Smooth;  // outgoing segment
        Ease ease;
        // Bézier handles for the position, relative to the key (used when the
        // interpolation is Bezier).
        Vec3 inTangent;
        Vec3 outTangent;
        // Tension of the automatic (Smooth) tangent: 0 = Catmull-Rom like,
        // 1 = no tangent (linear-looking corners).
        double tension = 0.0;

        bool operator==(const CameraKey&) const = default;
    };

    struct PathSample
    {
        double time = 0.0;
        Vec3 position;
    };

    class CameraPath
    {
       public:
        const std::vector<CameraKey>& Keys() const { return keys_; }
        std::vector<CameraKey>& MutableKeys() { return keys_; }
        bool Empty() const { return keys_.empty(); }
        std::size_t Size() const { return keys_.size(); }

        std::size_t AddKey(const CameraKey& key);  // replaces a key at the same time
        bool RemoveKeyAt(std::size_t index);
        std::size_t MoveKey(std::size_t index, double newTime);
        void SetKeys(std::vector<CameraKey> keys);
        void Clear() { keys_.clear(); InvalidateCache(); }

        RotationMode GetRotationMode() const { return rotationMode_; }
        void SetRotationMode(RotationMode mode) { rotationMode_ = mode; InvalidateCache(); }

        // Constant speed: the camera moves along the path at a uniform speed
        // between the first and the last key; the keys' times then only set
        // the start and end (and the order).
        bool IsConstantSpeed() const { return constantSpeed_; }
        void SetConstantSpeed(bool on) { constantSpeed_ = on; InvalidateCache(); }

        double StartTime() const { return keys_.empty() ? 0.0 : keys_.front().time; }
        double EndTime() const { return keys_.empty() ? 0.0 : keys_.back().time; }

        // The camera at cinematic time t; none without keys.
        std::optional<CameraState> Evaluate(double t) const;

        // Arc length of the path in game units.
        double Length() const;

        // Path parameter (a key-time-domain time) the camera is at, at
        // cinematic time t: t itself unless at constant speed.
        double PathTimeAt(double t) const;

        // Dense samples of the path for drawing it (`perSegment` per segment).
        std::vector<PathSample> Sample(int perSegment = 24) const;

        // Editing helpers.
        void Reverse();
        // Offsets every key by dt seconds.
        void Shift(double dt);
        // Rescales the keys' times around `pivot`.
        void Retime(double pivot, double factor);
        // Ensures consecutive Euler angles take the shortest way (adds/removes
        // multiples of 360 degrees); used before switching to Euler mode.
        void UnwrapAngles();
        // Laplacian smoothing of the inner keys' positions (strength 0..1).
        void SmoothPositions(double strength, int iterations = 1);

        void InvalidateCache() const { arcTableValid_ = false; }

       private:
        Vec3 EvaluatePosition(double pathTime) const;
        Quat EvaluateOrientation(double pathTime) const;
        EulerAngles EvaluateEuler(double pathTime) const;
        double EvaluateScalar(double pathTime, double CameraKey::*member) const;
        std::size_t SegmentIndex(double pathTime) const;
        Vec3 AutoTangent(std::size_t i) const;  // units per second
        void BuildArcTable() const;

        std::vector<CameraKey> keys_;
        RotationMode rotationMode_ = RotationMode::Quaternion;
        bool constantSpeed_ = false;

        // pathTime -> cumulative length, for constant speed
        mutable bool arcTableValid_ = false;
        mutable std::vector<double> arcTimes_;
        mutable std::vector<double> arcLengths_;
    };
}  // namespace iris
