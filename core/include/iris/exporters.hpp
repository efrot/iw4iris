// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Camera exports for compositing: Blender (JSON + the bundled importer
// add-on), Adobe After Effects (ExtendScript .jsx) and CSV.
//
// Coordinate systems
//   IW4 world    right-handed, X forward, Y left, Z up, inches.
//   Blender      right-handed, Z up, metres. World axes map one to one
//                (x, y, z) -> (x, y, z) / unitsPerMetre. A Blender camera
//                looks down its local -Z with +Y up, so its rotation matrix
//                is [ -left | up | -forward ] in IW4 axes.
//   After Effects right-handed with X right, Y down, Z away from the viewer,
//                pixels. IW4 (x, y, z) -> AE (x, -z, y) / unitsPerPixel; the
//                camera uses position + point of interest + Z rotation (roll).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "iris/camera.hpp"
#include "iris/timeline.hpp"

namespace iris
{
    struct CameraSample
    {
        std::int64_t frame = 0;
        double time = 0.0;  // cinematic seconds
        CameraState state;
    };

    // Samples the camera state for every frame in [first, first + count).
    template <typename Evaluate>
    std::vector<CameraSample> SampleFrames(const FrameRate& rate, std::int64_t first, std::int64_t count, Evaluate&& eval)
    {
        std::vector<CameraSample> out;
        out.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
        for (std::int64_t f = first; f < first + count; ++f)
        {
            const double t = rate.FrameToSeconds(f);
            if (auto s = eval(t))
                out.push_back({f, t, *s});
        }
        return out;
    }

    struct ExportInfo
    {
        std::string name = "IW4Iris Camera";
        std::string projectName;
        std::string mapName;
        FrameRate frameRate{60, 1};
        int width = 1920;
        int height = 1080;
        double sensorWidthMm = 36.0;  // the Blender camera's sensor (horizontal fit)
        double unitsPerMetre = 39.37007874;
        double unitsPerPixel = 1.0;   // After Effects
    };

    // Blender
    struct BlenderTransform
    {
        Vec3 location;              // metres
        Quat rotation;              // w, x, y, z (rotation_mode 'QUATERNION')
        double lensMm = 50.0;       // focal length for the export sensor width
        double focusDistanceM = 0.0;
        double fNumber = 2.8;
    };
    BlenderTransform ToBlender(const CameraState& state, double sensorWidthMm, double unitsPerMetre);
    // Inverse, for round-trip tests and importing Blender cameras back.
    CameraState FromBlender(const BlenderTransform& transform, double sensorWidthMm, double unitsPerMetre);
    std::string ExportBlenderJson(const ExportInfo& info, const std::vector<CameraSample>& samples);

    // After Effects
    struct AeTransform
    {
        Vec3 position;
        Vec3 pointOfInterest;
        double zRotation = 0.0;  // degrees
        double zoom = 0.0;       // pixels
        double focusDistance = 0.0;
    };
    Vec3 ToAeSpace(const Vec3& iw, double unitsPerPixel);
    AeTransform ToAfterEffects(const CameraState& state, int compWidth, double unitsPerPixel);
    std::string ExportAfterEffectsJsx(const ExportInfo& info, const std::vector<CameraSample>& samples);

    // frame,time,x,y,z,pitch,yaw,roll,hfov,focus,fstop
    std::string ExportCsv(const std::vector<CameraSample>& samples);
}  // namespace iris
