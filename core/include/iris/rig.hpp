// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Evaluates a project camera at a cinematic time (all modes the core can
// resolve on its own), deterministic camera shake, and the free camera
// controller.
#pragma once

#include <functional>
#include <optional>

#include "iris/camera.hpp"
#include "iris/project.hpp"

namespace iris
{
    // What the engine knows that a rig may need.
    struct RigContext
    {
        // World position of a target (player / entity origin plus its
        // offset), none when the target is not present right now.
        std::function<std::optional<Vec3>(const TargetRef&)> resolveTarget;
        // The game's own view, for first / third person cameras.
        std::optional<CameraState> gameView;
        double aspect = 16.0 / 9.0;
    };

    // Resolves the camera at cinematic time t. Returns none when the mode
    // needs something that is missing (a dolly without keys, an orbit whose
    // target is absent, first person without the game view).
    std::optional<CameraState> EvaluateCamera(const CameraDef& camera, double t, const RigContext& context);

    // Smooth deterministic noise in [-1, 1] (sum of incommensurate sines with
    // seed-derived phases); the same (t, seed, channel) always gives the
    // same value.
    double ShakeNoise(double t, std::uint32_t seed, int channel);
    // Applies the shake of `shake` at time t to a camera state.
    CameraState ApplyShake(const CameraState& state, const ShakeSettings& shake, double t);

    // ------------------------------------------------------------ free camera

    struct FreeCameraInput
    {
        double forward = 0.0;  // -1..1 (W/S, left stick Y)
        double right = 0.0;    // -1..1 (D/A)
        double up = 0.0;       // -1..1 (Space/Ctrl)
        double lookYaw = 0.0;  // degrees this frame (mouse X, already scaled by sensitivity)
        double lookPitch = 0.0;
        double roll = 0.0;     // -1..1 (Q/E)
        double fovDelta = 0.0; // degrees this frame (wheel)
        bool fast = false;     // Shift
        bool slow = false;     // Alt
    };

    struct FreeCameraSettings
    {
        double speed = 220.0;           // units / second
        double fastMultiplier = 4.0;
        double slowMultiplier = 0.2;
        double acceleration = 10.0;     // 1/s, how quickly velocity reaches its target
        double rollSpeed = 45.0;        // degrees / second
        double lookSmoothing = 0.0;     // 0 = raw, up to 0.95 = very smooth
        double minFov = 5.0;
        double maxFov = 150.0;
        bool flyInViewDirection = true; // false: horizontal movement ignores pitch
    };

    class FreeCameraController
    {
       public:
        void SetState(const CameraState& state);
        const CameraState& State() const { return state_; }
        FreeCameraSettings& Settings() { return settings_; }
        const FreeCameraSettings& Settings() const { return settings_; }

        // Advances by dt seconds of real (editor) time.
        void Update(const FreeCameraInput& input, double dt);
        void Stop() { velocity_ = {}; smoothedLook_ = {}; }
        Vec3 Velocity() const { return velocity_; }

       private:
        CameraState state_;
        FreeCameraSettings settings_;
        Vec3 velocity_;
        Vec3 smoothedLook_;  // pitch, yaw, unused
    };
}  // namespace iris
