// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/preflight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace iris
{
    namespace
    {
        double AngleBetween(const CameraState& a, const CameraState& b)
        {
            // The angle between the two view directions plus the roll change.
            const Vec3 fa = AnglesToForward(a.angles), fb = AnglesToForward(b.angles);
            const double c = std::clamp(Dot(fa, fb), -1.0, 1.0);
            double roll = std::fmod(b.angles.roll - a.angles.roll, 360.0);
            if (roll > 180.0)
                roll -= 360.0;
            if (roll < -180.0)
                roll += 360.0;
            return std::acos(c) * kRadToDeg + std::abs(roll);
        }

        std::string Time(double t)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f s", t);
            return buf;
        }
    }  // namespace

    std::vector<Issue> PreflightProject(const Project& p, double rangeStart, double rangeEnd,
                                        const std::set<std::string>& knownPlayers, const PreflightLimits& limits)
    {
        std::vector<Issue> issues;
        auto error = [&](std::string m) { issues.push_back({Issue::Severity::Error, std::move(m)}); };
        auto warn = [&](std::string m) { issues.push_back({Issue::Severity::Warning, std::move(m)}); };
        if (rangeEnd < rangeStart)
            std::swap(rangeStart, rangeEnd);

        // Cuts to cameras that no longer exist.
        if (p.useCameraCuts)
            for (const auto& cut : p.cuts.Cuts())
                if (!p.FindCamera(cut.cameraId))
                    error("A camera cut at " + Time(cut.time) + " points at a camera that was deleted (\"" + cut.cameraId +
                          "\"). Change or delete the cut (right-click it on the timeline).");

        // Angles (other recordings of the match) the range cuts to.
        for (const auto& seg : AngleSegments(p, rangeStart, rangeEnd))
        {
            if (seg.angle.empty())
                continue;
            const auto* a = p.FindAngle(seg.angle);
            if (!a)
            {
                error("A camera cut at " + Time(seg.start) + " uses an angle that was removed (\"" + seg.angle +
                      "\"). Change the cut's angle or delete the cut.");
                continue;
            }
            if (p.demo)
                if (const auto why = AngleIncompatibility(*p.demo, a->demo))
                    error("Angle '" + a->name + "' (used from " + Time(seg.start) + ") cannot be part of this match: " +
                          *why);
            if (a->sync == SyncState::Unsynced)
                error("Angle '" + a->name + "' (used from " + Time(seg.start) +
                      ") is not synchronised with the main demo: set its offset in the Angles panel.");
            else if (a->sync == SyncState::Suggested)
            {
                char pct[16];
                std::snprintf(pct, sizeof(pct), "%.0f%%", a->confidence * 100.0);
                warn("Angle '" + a->name + "' uses a suggested sync (" + pct +
                     " of the shared kills agree) that was not checked by eye: open it at a kill and confirm.");
            }
        }

        // Cameras shown in the range: sample which one is active.
        const double step = std::max(limits.sampleStep, 1e-3);
        std::map<std::string, std::pair<double, double>> shown;  // camera id -> first / last time shown
        for (double t = rangeStart; t <= rangeEnd + 1e-9; t += step)
            if (const auto* c = p.CameraAt(t))
            {
                auto [it, inserted] = shown.try_emplace(c->id, t, t);
                if (!inserted)
                    it->second.second = t;
            }

        for (const auto& [id, span] : shown)
        {
            const CameraDef* c = p.FindCamera(id);
            if (!c)
                continue;
            const std::string name = "\"" + c->name + "\"";
            if (c->mode == CameraMode::Dolly)
            {
                if (c->path.Size() == 0)
                    error(name + " is shown from " + Time(span.first) + " but has no keys: add keys (K) or use another "
                                                                           "camera there.");
                else if (c->path.Size() == 1)
                    warn(name + " has a single key: it stands still.");
            }
            if ((c->mode == CameraMode::PlayerEyes || c->mode == CameraMode::Chase || c->mode == CameraMode::Follow ||
                 c->mode == CameraMode::Orbit) &&
                c->target.kind == TargetKind::Player && !knownPlayers.empty() && !c->target.name.empty() &&
                !knownPlayers.contains(c->target.name))
                warn(name + " follows the player \"" + c->target.name +
                     "\", who has not appeared in this demo: the camera has no picture (the recorded view is shown).");

            // Motion: only paths the project fully defines (dolly, free,
            // static, orbit around a point) can be checked without the game.
            if (c->mode != CameraMode::Dolly || c->path.Size() < 2)
                continue;
            double worstTurn = 0.0, worstTurnAt = 0.0, worstSpeed = 0.0, worstSpeedAt = 0.0;
            std::optional<CameraState> prev;
            double prevT = 0.0;
            for (double t = span.first; t <= span.second + 1e-9; t += step)
            {
                const auto st = c->path.Evaluate(t);
                if (!st)
                    continue;
                if (prev && t > prevT)
                {
                    const double dt = t - prevT;
                    const double turn = AngleBetween(*prev, *st) / dt;
                    const double speed = Distance(prev->position, st->position) / dt;
                    if (turn > worstTurn)
                    {
                        worstTurn = turn;
                        worstTurnAt = t;
                    }
                    if (speed > worstSpeed)
                    {
                        worstSpeed = speed;
                        worstSpeedAt = t;
                    }
                }
                prev = st;
                prevT = t;
            }
            char buf[200];
            if (worstTurn > limits.maxTurnDegreesPerSecond)
            {
                std::snprintf(buf, sizeof(buf), " turns at %.0f deg/s around %.2f s: check the keys there (too close, or a "
                                                "Bezier handle overshooting).",
                              worstTurn, worstTurnAt);
                warn(name + buf);
            }
            if (worstSpeed > limits.maxSpeedUnitsPerSecond)
            {
                std::snprintf(buf, sizeof(buf), " moves at %.0f units/s around %.2f s: check the keys there.", worstSpeed,
                              worstSpeedAt);
                warn(name + buf);
            }
        }
        if (shown.empty() && !p.cameras.empty())
            warn("No camera is active in the range: the recorded view is shown.");
        return issues;
    }
}  // namespace iris
