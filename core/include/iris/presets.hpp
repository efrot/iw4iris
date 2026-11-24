// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Shot presets for trickshot and montage editing. A preset only generates
// ordinary keys (camera path keys, playback-speed keys, markers) around a
// subject; everything it creates stays fully editable.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "iris/camera.hpp"
#include "iris/curve.hpp"
#include "iris/timeline.hpp"

namespace iris
{
    struct PresetSubject
    {
        Vec3 position;            // the subject's eye / point of interest
        double facingYaw = 0.0;   // degrees, the way the subject faces
        double scale = 1.0;       // distances are multiplied by this
    };

    struct PresetResult
    {
        std::vector<CameraKey> cameraKeys;
        std::vector<ScalarKey> speedKeys;  // playback speed (time remap)
        std::vector<Marker> markers;
        bool constantSpeed = false;
        RotationMode rotationMode = RotationMode::Quaternion;
    };

    struct PresetInfo
    {
        std::string id;
        std::string name;
        std::string description;
        double defaultDuration = 4.0;
    };

    const std::vector<PresetInfo>& ShotPresets();

    // Builds a preset starting at cinematic time `start` and lasting
    // `duration` seconds. `moment` is the time of the action (the kill or the
    // trick) for presets that ramp around it. Unknown ids give an empty result.
    PresetResult BuildPreset(std::string_view id, const PresetSubject& subject, double start, double duration,
                             double moment);
}  // namespace iris
