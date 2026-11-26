// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/rig.hpp"

#include <cmath>

namespace iris
{
    namespace
    {
        double FovOf(const CameraDef& camera)
        {
            return camera.useLensFov ? camera.lens.HorizontalFov() : camera.horizontalFov;
        }

        CameraState FromTransform(const Vec3& position, const EulerAngles& angles, double fov, const CameraDef& camera)
        {
            CameraState s;
            s.position = position;
            s.angles = angles;
            s.orientation = AnglesToQuat(angles);
            s.horizontalFov = Clamp(fov, 1.0, 179.0);
            s.focusDistance = camera.lens.focusDistance;
            s.fNumber = camera.lens.fNumber;
            return s;
        }

        std::uint32_t Hash(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }
    }  // namespace

    double ShakeNoise(double t, std::uint32_t seed, int channel)
    {
        // Three octaves with irrational frequency ratios: smooth, non-repeating
        // in practice, cheap, and exactly reproducible.
        static constexpr double ratios[3] = {1.0, 2.1180339887, 4.3608058};
        static constexpr double weights[3] = {0.6, 0.3, 0.1};
        double v = 0.0;
        for (int o = 0; o < 3; ++o)
        {
            const std::uint32_t h = Hash(seed * 7919u + static_cast<std::uint32_t>(channel) * 104729u +
                                         static_cast<std::uint32_t>(o) * 1299709u);
            const double phase = (h & 0xFFFFFF) / static_cast<double>(0x1000000) * 2.0 * kPi;
            v += weights[o] * std::sin(2.0 * kPi * ratios[o] * t + phase);
        }
        return Clamp(v, -1.0, 1.0);
    }

    CameraState ApplyShake(const CameraState& state, const ShakeSettings& shake, double t)
    {
        if (!shake.enabled)
            return state;
        CameraState s = state;
        const double ft = t * std::max(shake.frequency, 0.0);
        s.position += Vec3{ShakeNoise(ft, shake.seed, 0), ShakeNoise(ft, shake.seed, 1), ShakeNoise(ft, shake.seed, 2)} *
                      shake.amplitudePosition;
        s.angles.pitch += ShakeNoise(ft, shake.seed, 3) * shake.amplitudeRotation;
        s.angles.yaw += ShakeNoise(ft, shake.seed, 4) * shake.amplitudeRotation;
        s.angles.roll += ShakeNoise(ft, shake.seed, 5) * shake.amplitudeRotation * 0.5;
        s.orientation = AnglesToQuat(s.angles);
        return s;
    }

    std::optional<CameraState> EvaluateCamera(const CameraDef& camera, double t, const RigContext& context)
    {
        std::optional<CameraState> out;
        auto target = [&]() -> std::optional<Vec3> {
            if (camera.target.kind == TargetKind::None)
                return std::nullopt;
            if (camera.target.kind == TargetKind::Point)
                return camera.target.point + camera.target.offset;
            if (context.resolveTarget)
                return context.resolveTarget(camera.target);
            return std::nullopt;
        };

        switch (camera.mode)
        {
            case CameraMode::FirstPerson:
            case CameraMode::ThirdPerson:
                out = context.gameView;
                break;
            case CameraMode::Free:
                out = FromTransform(camera.position, camera.angles, FovOf(camera), camera);
                break;
            case CameraMode::Static:
            {
                EulerAngles angles = camera.angles;
                if (const auto tp = target())
                    angles = LookAt(camera.position, *tp, camera.angles.roll);
                out = FromTransform(camera.position, angles, FovOf(camera), camera);
                break;
            }
            case CameraMode::Dolly:
            {
                out = camera.path.Evaluate(t);
                if (out && camera.useLensFov)
                    out->horizontalFov = camera.lens.HorizontalFov();
                break;
            }
            case CameraMode::Orbit:
            {
                const auto tp = target();
                if (!tp)
                    break;
                const double a = (camera.orbitStartAngle + camera.orbitSpeed * t) * kDegToRad;
                const Vec3 pos = *tp + Vec3{std::cos(a) * camera.orbitDistance, std::sin(a) * camera.orbitDistance,
                                            camera.orbitHeight};
                out = FromTransform(pos, LookAt(pos, *tp, camera.angles.roll), FovOf(camera), camera);
                break;
            }
            case CameraMode::PlayerEyes:
            {
                // The recording player's eyes are the game's own view (exact,
                // with its recorded field of view); any other player's are
                // reconstructed from their entity: origin, stance, view angles.
                const auto pv = context.resolvePlayer ? context.resolvePlayer(camera.target) : std::nullopt;
                if (!pv)
                    break;
                if (pv->exact && context.gameView)
                    out = context.gameView;
                else
                {
                    // A few units in front of the eyes, out of the player's
                    // own head model (the engine still draws it).
                    const EulerAngles view{pv->angles.pitch, pv->angles.yaw, 0.0};
                    out = FromTransform(pv->eye + AnglesToForward(view) * 6.0, view, FovOf(camera), camera);
                }
                break;
            }
            case CameraMode::Chase:
            {
                // Behind the player (orbitStartAngle turns around them),
                // turning with their yaw; looks just ahead of their eyes.
                const auto pv = context.resolvePlayer ? context.resolvePlayer(camera.target) : std::nullopt;
                if (!pv)
                    break;
                const double yaw = (pv->angles.yaw + camera.orbitStartAngle) * kDegToRad;
                const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0};
                const Vec3 right{std::sin(yaw), -std::cos(yaw), 0.0};
                const Vec3 pos = pv->eye - forward * camera.orbitDistance + right * camera.chaseShoulder +
                                 Vec3{0.0, 0.0, camera.orbitHeight};
                const double facing = pv->angles.yaw * kDegToRad;
                const Vec3 look = pv->eye + Vec3{std::cos(facing), std::sin(facing), 0.0} * 48.0;
                out = FromTransform(pos, LookAt(pos, look, camera.angles.roll), FovOf(camera), camera);
                break;
            }
            case CameraMode::Follow:
            {
                const auto tp = target();
                if (!tp)
                    break;
                const double a = camera.orbitStartAngle * kDegToRad;
                const Vec3 pos = *tp + Vec3{std::cos(a) * camera.orbitDistance, std::sin(a) * camera.orbitDistance,
                                            camera.orbitHeight};
                out = FromTransform(pos, LookAt(pos, *tp, camera.angles.roll), FovOf(camera), camera);
                break;
            }
        }
        if (out && camera.mode != CameraMode::FirstPerson && camera.mode != CameraMode::ThirdPerson)
            out = ApplyShake(*out, camera.shake, t);
        return out;
    }

    // ------------------------------------------------------------ free camera

    void FreeCameraController::SetState(const CameraState& state)
    {
        state_ = state;
        state_.orientation = AnglesToQuat(state_.angles);
        velocity_ = {};
        smoothedLook_ = {};
    }

    void FreeCameraController::Update(const FreeCameraInput& in, double dt)
    {
        dt = Clamp(dt, 0.0, 0.25);
        const auto& s = settings_;

        // Look, with optional exponential smoothing of the per-frame deltas.
        const double smoothing = Clamp(s.lookSmoothing, 0.0, 0.95);
        const double keep = dt > 0.0 ? std::pow(smoothing, dt * 60.0) : 1.0;
        smoothedLook_.x = smoothedLook_.x * keep + in.lookPitch * (1.0 - keep);
        smoothedLook_.y = smoothedLook_.y * keep + in.lookYaw * (1.0 - keep);
        const double pitchDelta = smoothing > 0.0 ? smoothedLook_.x : in.lookPitch;
        const double yawDelta = smoothing > 0.0 ? smoothedLook_.y : in.lookYaw;

        state_.angles.pitch = Clamp(state_.angles.pitch + pitchDelta, -89.9, 89.9);
        state_.angles.yaw = WrapDegrees(state_.angles.yaw + yawDelta);
        state_.angles.roll = WrapDegrees(state_.angles.roll + in.roll * s.rollSpeed * dt);
        state_.horizontalFov = Clamp(state_.horizontalFov + in.fovDelta, s.minFov, s.maxFov);

        // Move, accelerating towards the wanted velocity.
        double speed = s.speed;
        if (in.fast)
            speed *= s.fastMultiplier;
        if (in.slow)
            speed *= s.slowMultiplier;
        const Mat3 axis = AnglesToAxis(state_.angles);
        Vec3 forward = axis.col[0];
        if (!s.flyInViewDirection)
            forward = Normalize(Vec3{forward.x, forward.y, 0.0});
        const Vec3 left = Normalize(Vec3{axis.col[1].x, axis.col[1].y, s.flyInViewDirection ? axis.col[1].z : 0.0});
        Vec3 wish = forward * Clamp(in.forward, -1.0, 1.0) - left * Clamp(in.right, -1.0, 1.0) +
                    Vec3{0, 0, 1} * Clamp(in.up, -1.0, 1.0);
        if (Length(wish) > 1.0)
            wish = Normalize(wish);
        const Vec3 target = wish * speed;
        const double blend = 1.0 - std::exp(-std::max(s.acceleration, 0.1) * dt);
        velocity_ = Lerp(velocity_, target, blend);
        state_.position += velocity_ * dt;
        state_.orientation = AnglesToQuat(state_.angles);
    }
}  // namespace iris
