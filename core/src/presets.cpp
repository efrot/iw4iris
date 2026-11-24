// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/presets.hpp"

#include <cmath>

namespace iris
{
    const std::vector<PresetInfo>& ShotPresets()
    {
        static const std::vector<PresetInfo> presets{
            {"cinematic_intro", "Smooth cinematic introduction", "A slow rising push towards the subject.", 5.0},
            {"scope_closeup", "Sniper scope close-up", "Tight telephoto framing beside the weapon, slight drift.", 3.0},
            {"weapon_showcase", "Intervention weapon showcase", "A low arc past the weapon side with a roll.", 4.0},
            {"trickshot_orbit", "Third-person trickshot orbit", "A full constant-speed orbit around the player.", 6.0},
            {"rooftop_establishing", "Rooftop establishing shot", "High wide shot descending towards the subject.", 5.0},
            {"slowmo_jump", "Slow-motion jump sequence", "Tracking shot with a speed ramp into slow motion at the moment.", 4.0},
            {"killcam_transition", "Killcam transition", "Whip from the shooter's view to the victim side.", 2.0},
            {"player_to_player", "Player-to-player move", "Travels from behind the subject forward along its view.", 4.0},
            {"weapon_reveal", "Dramatic weapon reveal", "Rack-focus push-in from very close.", 3.0},
            {"over_shoulder", "Over-the-shoulder", "Fixed over-the-shoulder framing with a gentle drift.", 3.0},
            {"character_closeup", "Character close-up", "Slow push on the face with a long lens.", 3.0},
            {"map_flythrough", "Map flythrough", "High-speed fly-over passing the subject.", 6.0},
        };
        return presets;
    }

    namespace
    {
        Vec3 Around(const PresetSubject& s, double distance, double angleDeg, double height)
        {
            const double a = (s.facingYaw + angleDeg) * kDegToRad;
            return s.position + Vec3{std::cos(a) * distance, std::sin(a) * distance, height} * s.scale;
        }

        CameraKey Key(double t, const Vec3& pos, const Vec3& lookAt, double fov, double roll = 0.0,
                      Interpolation interp = Interpolation::Smooth)
        {
            CameraKey k;
            k.time = t;
            k.position = pos;
            k.angles = LookAt(pos, lookAt, roll);
            k.horizontalFov = fov;
            k.focusDistance = Distance(pos, lookAt);
            k.interpolation = interp;
            return k;
        }

        ScalarKey Speed(double t, double v, Interpolation interp = Interpolation::Smooth)
        {
            ScalarKey k;
            k.time = t;
            k.value = v;
            k.interpolation = interp;
            return k;
        }
    }  // namespace

    PresetResult BuildPreset(std::string_view id, const PresetSubject& s, double start, double duration, double moment)
    {
        PresetResult r;
        duration = std::max(duration, 0.1);
        const double end = start + duration;
        const double mid = start + duration * 0.5;
        const Vec3 target = s.position;
        auto ease = [](CameraKey k, double in, double out) {
            k.ease = {in, out};
            return k;
        };

        if (id == "cinematic_intro")
        {
            r.cameraKeys.push_back(ease(Key(start, Around(s, 420, 160, 90), target, 60), 0.0, 0.5));
            r.cameraKeys.push_back(Key(mid, Around(s, 260, 175, 50), target, 55));
            r.cameraKeys.push_back(ease(Key(end, Around(s, 140, 185, 20), target, 50), 0.6, 0.0));
        }
        else if (id == "scope_closeup")
        {
            const Vec3 a = Around(s, 34, 70, -6), b = Around(s, 30, 60, -4);
            r.cameraKeys.push_back(ease(Key(start, a, target + Vec3{0, 0, -4}, 24), 0.0, 0.3));
            r.cameraKeys.push_back(ease(Key(end, b, target + Vec3{0, 0, -4}, 22), 0.3, 0.0));
        }
        else if (id == "weapon_showcase")
        {
            r.cameraKeys.push_back(Key(start, Around(s, 60, -60, -18), target, 45, -8));
            r.cameraKeys.push_back(Key(mid, Around(s, 44, 0, -10), target, 40, 0));
            r.cameraKeys.push_back(Key(end, Around(s, 60, 60, -18), target, 45, 8));
        }
        else if (id == "trickshot_orbit")
        {
            constexpr int steps = 8;
            for (int i = 0; i <= steps; ++i)
            {
                const double t = start + duration * i / steps;
                r.cameraKeys.push_back(Key(t, Around(s, 180, 180 + 360.0 * i / steps, 30), target, 70));
            }
            r.constantSpeed = true;
            r.rotationMode = RotationMode::Euler;  // keep the full turn
        }
        else if (id == "rooftop_establishing")
        {
            r.cameraKeys.push_back(ease(Key(start, Around(s, 1400, 200, 700), target, 75), 0.0, 0.4));
            r.cameraKeys.push_back(ease(Key(end, Around(s, 500, 190, 180), target, 60), 0.7, 0.0));
        }
        else if (id == "slowmo_jump")
        {
            r.cameraKeys.push_back(Key(start, Around(s, 220, 120, 20), target, 70));
            r.cameraKeys.push_back(Key(moment, Around(s, 150, 100, 40), target, 60));
            r.cameraKeys.push_back(Key(end, Around(s, 130, 70, 60), target, 55));
            const double rampIn = std::max(start, moment - duration * 0.15);
            const double rampOut = std::min(end, moment + duration * 0.25);
            r.speedKeys.push_back(Speed(start, 1.0));
            r.speedKeys.push_back(Speed(rampIn, 1.0));
            r.speedKeys.push_back(Speed(moment, 0.25));
            r.speedKeys.push_back(Speed(rampOut, 0.25));
            r.speedKeys.push_back(Speed(end, 1.0));
            r.markers.push_back({"", moment, "Jump", "#F7B955", "trickshot"});
        }
        else if (id == "killcam_transition")
        {
            r.cameraKeys.push_back(ease(Key(start, Around(s, 20, 180, 0), target + AnglesToForward({0, s.facingYaw, 0}) * 400.0, 65), 0.0, 0.0));
            r.cameraKeys.push_back(ease(Key(end, Around(s, 160, 40, 40), target, 60), 0.8, 0.0));
            r.speedKeys.push_back(Speed(start, 1.0));
            r.speedKeys.push_back(Speed(mid, 0.4));
            r.speedKeys.push_back(Speed(end, 1.0));
            r.markers.push_back({"", moment, "Kill", "#F75E5E", "kill"});
        }
        else if (id == "player_to_player")
        {
            const Vec3 forward = AnglesToForward({0, s.facingYaw, 0});
            const Vec3 from = target - forward * (120 * s.scale) + Vec3{0, 0, 10};
            const Vec3 to = target + forward * (900 * s.scale) + Vec3{0, 0, 30};
            r.cameraKeys.push_back(ease(Key(start, from, target + forward * 600.0, 65), 0.0, 0.3));
            r.cameraKeys.push_back(ease(Key(end, to, to + forward * 400.0, 65), 0.5, 0.0));
            r.constantSpeed = true;
        }
        else if (id == "weapon_reveal")
        {
            CameraKey a = Key(start, Around(s, 26, 30, -8), target + Vec3{0, 0, -8}, 40);
            a.focusDistance = 8.0;
            CameraKey b = Key(end, Around(s, 14, 20, -8), target + Vec3{0, 0, -8}, 34);
            r.cameraKeys.push_back(ease(a, 0.0, 0.4));
            r.cameraKeys.push_back(ease(b, 0.7, 0.0));
        }
        else if (id == "over_shoulder")
        {
            const Vec3 forward = AnglesToForward({0, s.facingYaw, 0});
            const Vec3 look = target + forward * 600.0;
            r.cameraKeys.push_back(Key(start, Around(s, 48, 150, 8), look, 55));
            r.cameraKeys.push_back(Key(end, Around(s, 44, 145, 10), look, 52));
        }
        else if (id == "character_closeup")
        {
            r.cameraKeys.push_back(ease(Key(start, Around(s, 90, 15, 0), target, 22), 0.0, 0.3));
            r.cameraKeys.push_back(ease(Key(end, Around(s, 70, 10, 0), target, 20), 0.5, 0.0));
        }
        else if (id == "map_flythrough")
        {
            r.cameraKeys.push_back(Key(start, Around(s, 2400, 220, 900), target, 85));
            r.cameraKeys.push_back(Key(mid, Around(s, 400, 270, 300), target, 80));
            r.cameraKeys.push_back(Key(end, Around(s, 2400, 320, 900), target, 85));
            r.constantSpeed = true;
        }
        return r;
    }
}  // namespace iris
