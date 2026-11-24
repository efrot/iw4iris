// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/shots.hpp"

#include <cmath>

namespace iris
{
    namespace
    {
        struct ShotInfo
        {
            ShotType type;
            const char* id;
            const char* label;
            double focal;
        };

        const std::vector<ShotInfo>& Infos()
        {
            static const std::vector<ShotInfo> infos{
                {ShotType::ExtremeCloseUp, "extremeCloseUp", "Extreme close-up", 135},
                {ShotType::CloseUp, "closeUp", "Close-up", 85},
                {ShotType::Medium, "medium", "Medium shot", 50},
                {ShotType::FullBody, "fullBody", "Full-body shot", 35},
                {ShotType::Wide, "wide", "Wide establishing shot", 24},
                {ShotType::OverShoulder, "overShoulder", "Over-the-shoulder", 50},
                {ShotType::LowAngleHero, "lowAngleHero", "Low-angle hero shot", 28},
                {ShotType::HighAngle, "highAngle", "High-angle shot", 35},
                {ShotType::Tracking, "tracking", "Tracking shot", 35},
                {ShotType::Orbit, "orbit", "Orbit", 35},
                {ShotType::DollyIn, "dollyIn", "Dolly in", 35},
                {ShotType::DollyOut, "dollyOut", "Dolly out", 35},
                {ShotType::Crane, "crane", "Crane shot", 24},
                {ShotType::Handheld, "handheld", "Handheld", 35},
                {ShotType::WeaponShowcase, "weaponShowcase", "Weapon showcase", 50},
                {ShotType::ThirdPersonFollow, "thirdPersonFollow", "Third-person follow", 28},
                {ShotType::DramaticReveal, "dramaticReveal", "Dramatic reveal", 35},
            };
            return infos;
        }

        const ShotInfo& InfoFor(ShotType t)
        {
            for (const auto& i : Infos())
                if (i.type == t)
                    return i;
            return Infos().front();
        }
    }  // namespace

    std::string_view ToString(ShotType type) { return InfoFor(type).id; }

    std::optional<ShotType> ShotTypeFromString(std::string_view text)
    {
        for (const auto& i : Infos())
            if (text == i.id)
                return i.type;
        return std::nullopt;
    }

    std::string ShotLabel(ShotType type) { return InfoFor(type).label; }

    const std::vector<ShotType>& AllShotTypes()
    {
        static const std::vector<ShotType> all = [] {
            std::vector<ShotType> v;
            for (const auto& i : Infos())
                v.push_back(i.type);
            return v;
        }();
        return all;
    }

    double FramingDistance(double frameHeight, double horizontalFovDeg, double aspect)
    {
        const double tanHalfV = std::tan(Clamp(horizontalFovDeg, 1.0, 179.0) * 0.5 * kDegToRad) / std::max(aspect, 1e-6);
        return frameHeight * 0.5 / std::max(tanHalfV, 1e-9);
    }

    ShotResult MakeShot(ShotType type, const ShotSubject& subject, const ShotSettings& st,
                        const std::optional<ShotSubject>& secondary)
    {
        ShotResult r;
        CameraDef& cam = r.camera;
        cam.name = ShotLabel(type);
        cam.mode = CameraMode::Dolly;
        Lens lens;
        lens.focalLengthMm = st.focalLengthMm.value_or(InfoFor(type).focal);
        cam.lens = lens;
        cam.useLensFov = false;
        const double hfov = lens.HorizontalFov();
        cam.horizontalFov = hfov;

        const double H = std::max(subject.height, 1.0);
        const Vec3 up{0, 0, 1};
        const double yawRad = subject.yaw * kDegToRad;
        const Vec3 fwd{std::cos(yawRad), std::sin(yawRad), 0};
        const Vec3 left{-std::sin(yawRad), std::cos(yawRad), 0};
        const double side = st.side >= 0 ? 1.0 : -1.0;
        const double t0 = st.start, t1 = st.start + std::max(st.duration, 0.1);
        auto pos = [&](double t) { return subject.positionAt ? subject.positionAt(t) : subject.position; };
        auto at = [&](double t, double heightFraction) { return pos(t) + up * (H * heightFraction); };
        auto dist = [&](double frame) { return FramingDistance(frame, hfov, st.aspect); };
        // Direction from the subject to the camera, `angle` degrees off its facing.
        auto around = [&](double angleDeg) {
            const double a = angleDeg * kDegToRad;
            return fwd * std::cos(a) + left * (std::sin(a) * side);
        };
        auto key = [&](double t, const Vec3& p, const Vec3& aim) {
            CameraKey k;
            k.time = t;
            k.position = p;
            k.angles = LookAt(p, aim);
            k.horizontalFov = hfov;
            k.focusDistance = std::max(Distance(p, aim), 1.0);
            k.fNumber = lens.fNumber;
            k.interpolation = Interpolation::Smooth;
            cam.path.AddKey(k);
        };
        // A static framing with a gentle push so the shot is not frozen.
        auto framed = [&](double frame, double aimFraction, double angleDeg, double camHeightFraction, double push) {
            const Vec3 aim = at(t0, aimFraction);
            const double d = dist(frame);
            Vec3 p0 = aim + around(angleDeg) * d;
            p0.z = pos(t0).z + H * camHeightFraction;
            const Vec3 p1 = Lerp(p0, aim, push);
            key(t0, p0, aim);
            key(t1, p1, at(t1, aimFraction));
        };

        switch (type)
        {
            case ShotType::ExtremeCloseUp: framed(H * 0.12, 0.92, 15, 0.92, 0.02); break;
            case ShotType::CloseUp: framed(H * 0.3, 0.88, 25, 0.88, 0.03); break;
            case ShotType::Medium: framed(H * 0.6, 0.72, 25, 0.78, 0.04); break;
            case ShotType::FullBody: framed(H * 1.25, 0.5, 25, 0.6, 0.04); break;
            case ShotType::Wide: framed(H * 4.0, 0.45, 35, 1.2, 0.03); break;
            case ShotType::Handheld:
                framed(H * 0.6, 0.72, 25, 0.8, 0.02);
                cam.shake.enabled = true;
                cam.shake.amplitudePosition = 0.8;
                cam.shake.amplitudeRotation = 0.5;
                cam.shake.frequency = 1.2;
                break;
            case ShotType::LowAngleHero:
            {
                const Vec3 aim = at(t0, 0.7);
                const double d = dist(H * 1.1);
                Vec3 p = pos(t0) + around(20) * d;
                p.z = pos(t0).z + H * 0.12;
                key(t0, p, aim);
                key(t1, Lerp(p, aim, 0.08), at(t1, 0.7));
                break;
            }
            case ShotType::HighAngle:
            {
                const Vec3 aim = at(t0, 0.45);
                const double d = dist(H * 1.6);
                Vec3 p = pos(t0) + around(30) * (d * 0.7);
                p.z = pos(t0).z + H * 2.2;
                key(t0, p, aim);
                key(t1, Lerp(p, aim, 0.05), at(t1, 0.45));
                break;
            }
            case ShotType::OverShoulder:
            {
                Vec3 look = subject.position + fwd * 240.0 + up * (H * 0.7);
                if (secondary)
                    look = secondary->position + up * (secondary->height * 0.75);
                else
                    r.warnings.push_back("Over-the-shoulder needs a second subject; the camera looks where the subject faces.");
                Vec3 dir = look - at(t0, 0.85);
                dir.z = 0;
                dir = Normalize(dir);
                const Vec3 dirLeft{-dir.y, dir.x, 0};
                const Vec3 p = at(t0, 0.93) - dir * 34.0 + dirLeft * (14.0 * side);
                key(t0, p, look);
                key(t1, p + dir * 4.0, look);
                break;
            }
            case ShotType::Tracking:
            {
                const double d = dist(H * 0.9);
                const int steps = std::max(2, static_cast<int>(std::ceil((t1 - t0) / 0.5)));
                for (int i = 0; i <= steps; ++i)
                {
                    const double t = t0 + (t1 - t0) * i / steps;
                    Vec3 p = at(t, 0.0) + left * (d * side);
                    if (!subject.positionAt)
                        p += fwd * (40.0 * (static_cast<double>(i) / steps - 0.5));
                    p.z = pos(t).z + H * 0.7;
                    key(t, p, at(t, 0.65));
                }
                if (!subject.positionAt)
                    r.warnings.push_back("The subject does not move; the tracking shot slides along it.");
                break;
            }
            case ShotType::Orbit:
            {
                const double d = dist(H * 1.4);
                const int n = 8;
                for (int i = 0; i <= n; ++i)
                {
                    const double t = t0 + (t1 - t0) * i / n;
                    const double angle = -80.0 + 160.0 * i / n;
                    Vec3 p = at(t, 0.0) + around(angle) * d;
                    p.z = pos(t).z + H * 0.65;
                    key(t, p, at(t, 0.55));
                }
                cam.path.SetConstantSpeed(true);
                break;
            }
            case ShotType::DollyIn:
            case ShotType::DollyOut:
            {
                const double near = dist(H * 0.9), far = dist(H * 2.2);
                const bool in = type == ShotType::DollyIn;
                const Vec3 aim0 = at(t0, 0.7), aim1 = at(t1, 0.75);
                Vec3 p0 = aim0 + around(15) * (in ? far : near);
                Vec3 p1 = aim1 + around(15) * (in ? near : far);
                p0.z = pos(t0).z + H * 0.75;
                p1.z = pos(t1).z + H * 0.8;
                key(t0, p0, aim0);
                key(t1, p1, aim1);
                break;
            }
            case ShotType::Crane:
            {
                Vec3 p0 = at(t0, 0.0) + around(25) * dist(H * 1.2);
                p0.z = pos(t0).z + H * 0.3;
                Vec3 p1 = at(t1, 0.0) + around(35) * dist(H * 3.0);
                p1.z = pos(t1).z + H * 3.0;
                key(t0, p0, at(t0, 0.5));
                key(t1, p1, at(t1, 0.6));
                break;
            }
            case ShotType::WeaponShowcase:
            {
                const double d = dist(H * 0.35);
                const int n = 4;
                for (int i = 0; i <= n; ++i)
                {
                    const double t = t0 + (t1 - t0) * i / n;
                    Vec3 p = at(t, 0.6) + around(-50.0 + 40.0 * i / n) * d;
                    p.z = pos(t).z + H * 0.62;
                    key(t, p, at(t, 0.6) + fwd * 10.0);
                }
                break;
            }
            case ShotType::ThirdPersonFollow:
            {
                const int steps = subject.positionAt ? std::max(2, static_cast<int>(std::ceil((t1 - t0) / 0.5))) : 1;
                for (int i = 0; i <= steps; ++i)
                {
                    const double t = t0 + (t1 - t0) * i / steps;
                    const Vec3 p = at(t, 0.85) - fwd * 110.0 + left * (22.0 * side);
                    key(t, p, at(t, 0.7) + fwd * 220.0);
                }
                break;
            }
            case ShotType::DramaticReveal:
            {
                const double d = dist(H * 0.8);
                const int n = 4;
                for (int i = 0; i <= n; ++i)
                {
                    const double t = t0 + (t1 - t0) * i / n;
                    const double f = static_cast<double>(i) / n;
                    Vec3 p = at(t, 0.0) + around(170.0 - 150.0 * f) * d;
                    p.z = pos(t).z + H * (0.25 + 0.6 * f);
                    key(t, p, at(t, 0.55 + 0.3 * f));
                }
                break;
            }
        }
        return r;
    }
}  // namespace iris
