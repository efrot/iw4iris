// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The camera director: editable shots framed on a subject (an actor, a
// prop, a graphic) from its position, facing and size. Framing uses the
// lens model: the distance makes the chosen part of the subject fill the
// frame height for the shot's focal length.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/project.hpp"

namespace iris
{
    enum class ShotType
    {
        ExtremeCloseUp,
        CloseUp,
        Medium,
        FullBody,
        Wide,
        OverShoulder,     // needs a second subject (looks past it)
        LowAngleHero,
        HighAngle,
        Tracking,         // moves with the subject (sampled from its motion)
        Orbit,
        DollyIn,
        DollyOut,
        Crane,
        Handheld,
        WeaponShowcase,
        ThirdPersonFollow,
        DramaticReveal,
    };
    std::string_view ToString(ShotType type);
    std::optional<ShotType> ShotTypeFromString(std::string_view text);
    std::string ShotLabel(ShotType type);
    const std::vector<ShotType>& AllShotTypes();

    struct ShotSubject
    {
        Vec3 position;          // base (feet / bottom centre)
        double yaw = 0.0;       // the direction the subject faces
        double height = 72.0;   // subject height in units
        double width = 32.0;
        // Position at time t for moving subjects (tracking / follow shots).
        std::function<Vec3(double)> positionAt;
    };

    struct ShotSettings
    {
        double start = 0.0;
        double duration = 5.0;
        double aspect = 16.0 / 9.0;
        double side = 1.0;      // +1 camera on the subject's left, -1 on its right
        std::optional<double> focalLengthMm;  // overrides the shot's default lens
    };

    struct ShotResult
    {
        CameraDef camera;       // a dolly camera with keys (and shake for handheld)
        std::vector<std::string> warnings;
    };

    // `secondary` is the subject an over-the-shoulder shot looks at.
    ShotResult MakeShot(ShotType type, const ShotSubject& subject, const ShotSettings& settings,
                        const std::optional<ShotSubject>& secondary = std::nullopt);

    // Distance at which `frameHeight` units fill the frame height.
    double FramingDistance(double frameHeight, double horizontalFovDeg, double aspect);
}  // namespace iris
