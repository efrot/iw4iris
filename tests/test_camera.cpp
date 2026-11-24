#include "iris/camera.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    CameraKey Key(double t, Vec3 p, EulerAngles a = {}, double fov = 90, Interpolation i = Interpolation::Smooth)
    {
        CameraKey k;
        k.time = t;
        k.position = p;
        k.angles = a;
        k.horizontalFov = fov;
        k.interpolation = i;
        return k;
    }
}  // namespace

TEST_CASE("lens: focal length and field of view")
{
    CHECK_NEAR(FocalLengthToHorizontalFov(50.0, 36.0), 39.5977, 1e-3);
    CHECK_NEAR(FocalLengthToHorizontalFov(18.0, 36.0), 90.0, 1e-9);
    for (double f : FocalLengthPresets())
        CHECK_NEAR(HorizontalFovToFocalLength(FocalLengthToHorizontalFov(f, 24.89), 24.89), f, 1e-9);
    CHECK_NEAR(VerticalToHorizontalFov(HorizontalToVerticalFov(80.0, 16.0 / 9.0), 16.0 / 9.0), 80.0, 1e-9);
}

TEST_CASE("lens: cg_fov is the 4:3 horizontal field of view")
{
    CHECK_NEAR(CgFovToHorizontalFov(65.0, 4.0 / 3.0), 65.0, 1e-9);
    CHECK_NEAR(CgFovToHorizontalFov(65.0, 16.0 / 9.0), 80.6909, 1e-3);
    CHECK_NEAR(HorizontalFovToCgFov(CgFovToHorizontalFov(80.0, 21.0 / 9.0), 21.0 / 9.0), 80.0, 1e-9);
}

TEST_CASE("lens: depth of field")
{
    // 50 mm f/2.8, CoC 0.03 mm: hyperfocal ~ 29.8 m.
    const auto r = ComputeDepthOfField(50.0, 2.8, 5.0 * 39.37007874, 0.03);
    CHECK_NEAR(r.hyperfocal / 39.37007874, 29.81, 0.02);
    CHECK(r.nearLimit < 5.0 * 39.37007874);
    CHECK(r.farLimit > 5.0 * 39.37007874);
    CHECK(!r.farIsInfinite);
    const auto far = ComputeDepthOfField(50.0, 2.8, 40.0 * 39.37007874, 0.03);
    CHECK(far.farIsInfinite);
    CHECK_NEAR(BlurDiameterMm(50.0, 2.8, 200.0, 200.0), 0.0, 1e-12);
    CHECK(BlurDiameterMm(50.0, 1.4, 200.0, 2000.0) > BlurDiameterMm(50.0, 5.6, 200.0, 2000.0));
}

TEST_CASE("path: every interpolation passes through its keys")
{
    for (auto interp : {Interpolation::Linear, Interpolation::Smooth, Interpolation::CatmullRom, Interpolation::Bezier,
                        Interpolation::Hold})
    {
        CameraPath path;
        path.AddKey(Key(0, {0, 0, 0}, {0, 0, 0}, 90, interp));
        path.AddKey(Key(1, {100, 50, 0}, {10, 45, 0}, 70, interp));
        path.AddKey(Key(2.5, {120, 200, 40}, {-5, 120, 10}, 60, interp));
        path.AddKey(Key(4, {0, 300, 0}, {0, 180, 0}, 80, interp));
        for (const auto& k : path.Keys())
        {
            const auto s = path.Evaluate(k.time);
            CHECK(s.has_value());
            CHECK_VEC_NEAR(s->position, k.position, 1e-6);
            CHECK(SameRotation(s->orientation, AnglesToQuat(k.angles), 1e-9));
            CHECK_NEAR(s->horizontalFov, k.horizontalFov, 1e-9);
        }
    }
}

TEST_CASE("path: empty path has no camera, one key is static")
{
    CameraPath path;
    CHECK(!path.Evaluate(0).has_value());
    path.AddKey(Key(1, {5, 6, 7}));
    CHECK_VEC_NEAR(path.Evaluate(-3)->position, Vec3(5, 6, 7), 0);
    CHECK_VEC_NEAR(path.Evaluate(9)->position, Vec3(5, 6, 7), 0);
}

TEST_CASE("path: linear segment midpoint and hold")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}, {0, 0, 0}, 90, Interpolation::Linear));
    path.AddKey(Key(2, {200, 0, 0}, {0, 90, 0}, 50, Interpolation::Hold));
    path.AddKey(Key(3, {300, 0, 0}));
    CHECK_VEC_NEAR(path.Evaluate(1.0)->position, Vec3(100, 0, 0), 1e-9);
    CHECK_NEAR(path.Evaluate(1.0)->angles.yaw, 45.0, 1e-9);
    CHECK_NEAR(path.Evaluate(1.0)->horizontalFov, 70.0, 1e-9);
    CHECK_VEC_NEAR(path.Evaluate(2.9)->position, Vec3(200, 0, 0), 1e-9);
}

TEST_CASE("path: smooth position is C1 at inner keys")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}));
    path.AddKey(Key(1, {100, 40, 0}));
    path.AddKey(Key(3, {160, 200, 50}));
    path.AddKey(Key(4, {0, 260, 0}));
    for (double t : {1.0, 3.0})
    {
        const double h = 1e-6;
        const Vec3 left = (path.Evaluate(t)->position - path.Evaluate(t - h)->position) / h;
        const Vec3 right = (path.Evaluate(t + h)->position - path.Evaluate(t)->position) / h;
        CHECK_VEC_NEAR(left, right, 1e-2);
    }
}

TEST_CASE("path: constant speed traverses equal distances in equal times")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}));
    path.AddKey(Key(0.2, {500, 0, 0}));  // very uneven timing
    path.AddKey(Key(4, {500, 500, 0}));
    path.SetConstantSpeed(true);
    const double length = path.Length();
    CHECK(length > 999.0);
    const double dt = 0.05;
    std::vector<double> steps;
    for (double t = 0.0; t + dt <= 4.0 + 1e-9; t += dt)
        steps.push_back(Distance(path.Evaluate(t)->position, path.Evaluate(t + dt)->position));
    const double expected = length * dt / 4.0;
    for (double s : steps)
        CHECK_NEAR(s, expected, expected * 0.02);
    CHECK_VEC_NEAR(path.Evaluate(0)->position, Vec3(0, 0, 0), 1e-9);
    CHECK_VEC_NEAR(path.Evaluate(4)->position, Vec3(500, 500, 0), 1e-6);
}

TEST_CASE("path: auto tangents do not overshoot with uneven timing")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}));
    path.AddKey(Key(0.2, {500, 0, 0}));
    path.AddKey(Key(4, {500, 500, 0}));
    for (double t = 0.0; t <= 4.0; t += 0.01)
        CHECK(path.Evaluate(t)->position.x <= 500.0 * 1.25);
}

TEST_CASE("path: straight line length")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}, {}, 90, Interpolation::Linear));
    path.AddKey(Key(1, {300, 400, 0}, {}, 90, Interpolation::Linear));
    CHECK_NEAR(path.Length(), 500.0, 1e-6);
}

TEST_CASE("path: euler mode follows multi-key spins, quaternion mode the short arc")
{
    CameraPath path;
    path.AddKey(Key(0, {}, {0, 0, 0}));
    path.AddKey(Key(1, {}, {0, 120, 0}));
    path.AddKey(Key(2, {}, {0, 240, 0}));
    path.AddKey(Key(3, {}, {0, 360, 0}));
    path.SetRotationMode(RotationMode::Euler);
    double previous = -1.0;
    for (double t = 0.0; t <= 3.0; t += 0.1)
    {
        const double yaw = path.Evaluate(t)->angles.yaw;
        const double unwrapped = UnwrapDegreesNear(yaw, previous < 0 ? 0 : previous);
        CHECK(unwrapped >= previous - 1e-6);
        previous = unwrapped;
    }
    CHECK_NEAR(WrapDegrees(path.Evaluate(1.5)->angles.yaw), 180.0, 1.0);
    path.SetRotationMode(RotationMode::Quaternion);
    CHECK_NEAR(std::abs(WrapDegrees(path.Evaluate(1.5)->angles.yaw)), 180.0, 1.0);
}

TEST_CASE("path: reverse, shift and retime")
{
    CameraPath path;
    path.AddKey(Key(1, {0, 0, 0}, {}, 90, Interpolation::Linear));
    path.AddKey(Key(2, {100, 0, 0}, {}, 90, Interpolation::Hold));
    path.AddKey(Key(4, {100, 100, 0}, {}, 90, Interpolation::Smooth));
    const auto before = path.Evaluate(1.5)->position;
    path.Reverse();
    CHECK_NEAR(path.StartTime(), 1.0, 0);
    CHECK_NEAR(path.EndTime(), 4.0, 0);
    CHECK_VEC_NEAR(path.Keys().front().position, Vec3(100, 100, 0), 0);
    CHECK_VEC_NEAR(path.Evaluate(1.0 + 4.0 - 1.5)->position, before, 1e-9);
    path.Shift(2.0);
    CHECK_NEAR(path.StartTime(), 3.0, 1e-12);
    path.Retime(3.0, 2.0);
    CHECK_NEAR(path.EndTime(), 9.0, 1e-12);
}

TEST_CASE("path: smoothing keeps the end points")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}));
    path.AddKey(Key(1, {50, 100, 0}));
    path.AddKey(Key(2, {100, 0, 0}));
    path.SmoothPositions(1.0);
    CHECK_VEC_NEAR(path.Keys().front().position, Vec3(0, 0, 0), 0);
    CHECK_VEC_NEAR(path.Keys().back().position, Vec3(100, 0, 0), 0);
    CHECK_VEC_NEAR(path.Keys()[1].position, Vec3(50, 0, 0), 1e-12);
}

TEST_CASE("path: sampling covers the whole path")
{
    CameraPath path;
    path.AddKey(Key(0, {0, 0, 0}));
    path.AddKey(Key(1, {10, 0, 0}));
    path.AddKey(Key(2, {20, 0, 0}));
    const auto samples = path.Sample(10);
    CHECK(samples.size() == 21);
    CHECK_NEAR(samples.back().time, 2.0, 0);
}
