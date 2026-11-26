// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Render preflight: what is wrong with the shot itself, found before a
// recording instead of in the finished video. It complements the
// recording-settings checks (ValidateRecording) and the engine checks
// (film set, occlusion) with project-level ones:
//
//   * camera cuts that point at deleted cameras;
//   * dolly cameras that are shown in the range but cannot move (fewer
//     than two keys) or have no keys at all;
//   * cameras on players that never appeared in this demo (a name typo, a
//     project made for another demo);
//   * camera moves that turn or travel unusually fast (a key set too close
//     to another one, a tangent overshoot), with the time and the speed.
//
// Everything is evaluated from the project alone, deterministically.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "iris/project.hpp"
#include "iris/recording.hpp"

namespace iris
{
    struct PreflightLimits
    {
        double maxTurnDegreesPerSecond = 540.0;   // faster looks like a glitch, not a whip pan
        double maxSpeedUnitsPerSecond = 3000.0;   // about 75 m/s
        double sampleStep = 1.0 / 60.0;           // seconds between samples of a path
    };

    // `knownPlayers`: names of the players seen in the loaded demo (empty:
    // no demo, the player check is skipped).
    std::vector<Issue> PreflightProject(const Project& project, double rangeStart, double rangeEnd,
                                        const std::set<std::string>& knownPlayers,
                                        const PreflightLimits& limits = {});
}  // namespace iris
