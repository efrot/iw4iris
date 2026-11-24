#include <nlohmann/json.hpp>

#include "iris/exporters.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    CameraState State(Vec3 p, EulerAngles a, double fov)
    {
        CameraState s;
        s.position = p;
        s.angles = a;
        s.orientation = AnglesToQuat(a);
        s.horizontalFov = fov;
        s.focusDistance = 300;
        return s;
    }
}  // namespace

TEST_CASE("export: blender transform round-trips")
{
    for (const auto& a : {EulerAngles{0, 0, 0}, EulerAngles{-30, 135, 12}, EulerAngles{60, -45, -20}})
    {
        const auto s = State({120, -340, 56}, a, 72.0);
        const auto b = ToBlender(s, 36.0, 39.37007874);
        const auto back = FromBlender(b, 36.0, 39.37007874);
        CHECK_VEC_NEAR(back.position, s.position, 1e-9);
        CHECK(SameRotation(AnglesToQuat(back.angles), s.orientation, 1e-9));
        CHECK_NEAR(back.horizontalFov, 72.0, 1e-9);
        // The Blender camera looks down its local -Z: that must be the IW4
        // forward vector (world axes are shared).
        CHECK_VEC_NEAR(Rotate(b.rotation, Vec3(0, 0, -1)), AnglesToForward(a), 1e-9);
        CHECK_VEC_NEAR(Rotate(b.rotation, Vec3(0, 1, 0)), AnglesToAxis(a).col[2], 1e-9);
    }
    const auto b = ToBlender(State({39.37007874, 0, 0}, {}, 90.0), 36.0, 39.37007874);
    CHECK_NEAR(b.location.x, 1.0, 1e-9);
    CHECK_NEAR(b.lensMm, 18.0, 1e-9);
}

TEST_CASE("export: after effects orientation, roll and zoom")
{
    // Looking along IW4 +X (AE +X) level: no roll.
    auto ae = ToAfterEffects(State({0, 0, 0}, {0, 0, 0}, 90.0), 1920, 1.0);
    CHECK_NEAR(ae.zRotation, 0.0, 1e-9);
    CHECK_NEAR(ae.zoom, 960.0, 1e-9);
    CHECK_VEC_NEAR(ToAeSpace({0, 0, 100}, 1.0), Vec3(0, -100, 0), 0);  // up is -Y in AE
    const Vec3 dir = ae.pointOfInterest - ae.position;
    CHECK(dir.x > 0);
    CHECK_NEAR(dir.y, 0.0, 1e-9);

    const auto rolled = ToAfterEffects(State({0, 0, 0}, {0, 30, 25}, 60.0), 1920, 1.0);
    CHECK_NEAR(std::abs(rolled.zRotation), 25.0, 1e-6);
    const auto pitched = ToAfterEffects(State({0, 0, 0}, {40, -60, 0}, 60.0), 1920, 1.0);
    CHECK_NEAR(pitched.zRotation, 0.0, 1e-6);
}

TEST_CASE("export: documents")
{
    std::vector<CameraSample> samples;
    for (int f = 0; f < 5; ++f)
        samples.push_back({f, f / 60.0, State({f * 10.0, 0, 0}, {0, f * 5.0, 0}, 80.0)});
    ExportInfo info;
    info.projectName = "test";
    const auto blender = nlohmann::json::parse(ExportBlenderJson(info, samples));
    CHECK(blender["format"] == "iw4iris-camera");
    CHECK(blender["frames"].size() == 5);
    CHECK(blender["frame_end"] == 4);

    const auto jsx = ExportAfterEffectsJsx(info, samples);
    CHECK(jsx.find("setValuesAtTimes") != std::string::npos);
    CHECK(jsx.find("ADBE Camera Zoom") != std::string::npos);
    CHECK(jsx.find("var times = [0,0.016667,0.033333,0.05,0.066667];") != std::string::npos);

    const auto csv = ExportCsv(samples);
    CHECK(std::count(csv.begin(), csv.end(), '\n') == 6);

    const auto sampled = SampleFrames(FrameRate{30, 1}, 10, 3, [](double t) -> std::optional<CameraState> {
        return State({t, 0, 0}, {}, 90);
    });
    CHECK(sampled.size() == 3);
    CHECK_NEAR(sampled[1].time, 11.0 / 30.0, 1e-12);
}
