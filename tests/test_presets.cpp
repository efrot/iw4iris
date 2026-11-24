#include "iris/presets.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("presets: every preset builds an ordered, editable path")
{
    PresetSubject subject;
    subject.position = {100, 200, 60};
    subject.facingYaw = 30;
    for (const auto& info : ShotPresets())
    {
        const auto r = BuildPreset(info.id, subject, 2.0, info.defaultDuration, 2.0 + info.defaultDuration * 0.5);
        CHECK(r.cameraKeys.size() >= 2);
        for (std::size_t i = 1; i < r.cameraKeys.size(); ++i)
            CHECK(r.cameraKeys[i].time > r.cameraKeys[i - 1].time);
        CHECK_NEAR(r.cameraKeys.front().time, 2.0, 1e-12);
        CHECK_NEAR(r.cameraKeys.back().time, 2.0 + info.defaultDuration, 1e-9);
        CameraPath path;
        path.SetKeys(r.cameraKeys);
        CHECK(path.Evaluate(2.0 + info.defaultDuration * 0.3).has_value());
    }
}

TEST_CASE("presets: speed ramps and orbits")
{
    PresetSubject s;
    const auto jump = BuildPreset("slowmo_jump", s, 0.0, 4.0, 2.0);
    CHECK(!jump.speedKeys.empty());
    CHECK(jump.markers.size() == 1);
    double minSpeed = 10;
    for (const auto& k : jump.speedKeys)
        minSpeed = std::min(minSpeed, k.value);
    CHECK(minSpeed < 1.0);
    const auto orbit = BuildPreset("trickshot_orbit", s, 0.0, 6.0, 0.0);
    CHECK(orbit.constantSpeed);
    for (const auto& k : orbit.cameraKeys)
        CHECK_NEAR(Distance(Vec3(k.position.x, k.position.y, 0), Vec3(0, 0, 0)), 180.0, 1e-9);
    CHECK(BuildPreset("does-not-exist", s, 0, 1, 0).cameraKeys.empty());
}
