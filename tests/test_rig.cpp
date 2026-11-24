#include "iris/rig.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("rig: orbit circles its target and looks at it")
{
    CameraDef cam;
    cam.mode = CameraMode::Orbit;
    cam.target = {TargetKind::Point, -1, {100, 200, 0}, {0, 0, 0}};
    cam.orbitDistance = 50;
    cam.orbitHeight = 10;
    cam.orbitSpeed = 90;
    RigContext ctx;
    const auto s0 = EvaluateCamera(cam, 0.0, ctx);
    const auto s1 = EvaluateCamera(cam, 1.0, ctx);
    CHECK(s0 && s1);
    CHECK_VEC_NEAR(s0->position, Vec3(150, 200, 10), 1e-9);
    CHECK_VEC_NEAR(s1->position, Vec3(100, 250, 10), 1e-9);
    CHECK_VEC_NEAR(AnglesToForward(s1->angles), Normalize(Vec3(100, 200, 0) - s1->position), 1e-9);
}

TEST_CASE("rig: player targets resolve through the context")
{
    CameraDef cam;
    cam.mode = CameraMode::Follow;
    cam.target = {TargetKind::Player, 4, {}, {0, 0, 60}};
    RigContext ctx;
    CHECK(!EvaluateCamera(cam, 0, ctx).has_value());
    ctx.resolveTarget = [](const TargetRef& t) -> std::optional<Vec3> {
        if (t.index == 4)
            return Vec3{0, 0, 60};
        return std::nullopt;
    };
    const auto s = EvaluateCamera(cam, 0, ctx);
    CHECK(s.has_value());
    CHECK_VEC_NEAR(AnglesToForward(s->angles), Normalize(Vec3(0, 0, 60) - s->position), 1e-9);
}

TEST_CASE("rig: static, free, dolly and first person")
{
    RigContext ctx;
    CameraDef cam;
    cam.mode = CameraMode::Static;
    cam.position = {1, 2, 3};
    cam.angles = {10, 20, 0};
    cam.horizontalFov = 75;
    auto s = EvaluateCamera(cam, 5.0, ctx);
    CHECK_VEC_NEAR(s->position, Vec3(1, 2, 3), 0);
    CHECK_NEAR(s->horizontalFov, 75, 0);
    cam.useLensFov = true;
    cam.lens.focalLengthMm = 18.0;
    cam.lens.sensor = {"FF", 36.0, 24.0};
    CHECK_NEAR(EvaluateCamera(cam, 0, ctx)->horizontalFov, 90.0, 1e-9);

    cam.mode = CameraMode::Dolly;
    CHECK(!EvaluateCamera(cam, 0, ctx).has_value());
    CameraKey k;
    k.position = {9, 9, 9};
    cam.path.AddKey(k);
    CHECK_VEC_NEAR(EvaluateCamera(cam, 0, ctx)->position, Vec3(9, 9, 9), 0);

    cam.mode = CameraMode::FirstPerson;
    CHECK(!EvaluateCamera(cam, 0, ctx).has_value());
    CameraState game;
    game.position = {7, 7, 7};
    ctx.gameView = game;
    CHECK_VEC_NEAR(EvaluateCamera(cam, 0, ctx)->position, Vec3(7, 7, 7), 0);
}

TEST_CASE("rig: shake is deterministic and bounded")
{
    ShakeSettings shake;
    shake.enabled = true;
    shake.amplitudePosition = 2.0;
    shake.amplitudeRotation = 1.0;
    CameraState base;
    for (double t = 0; t < 5; t += 0.137)
    {
        const auto a = ApplyShake(base, shake, t);
        const auto b = ApplyShake(base, shake, t);
        CHECK(a.position == b.position);
        CHECK(std::abs(a.position.x) <= 2.0 + 1e-12);
        CHECK(std::abs(a.angles.pitch) <= 1.0 + 1e-12);
    }
    shake.seed = 2;
    CHECK(ApplyShake(base, shake, 1.0).position != ApplyShake(base, ShakeSettings{true, 2.0, 1.0, 1.4, 1}, 1.0).position);
    shake.enabled = false;
    CHECK(ApplyShake(base, shake, 1.0).position == base.position);
}

TEST_CASE("rig: free camera accelerates along the view and clamps pitch")
{
    FreeCameraController fc;
    CameraState start;
    start.angles = {0, 90, 0};  // facing +Y
    fc.SetState(start);
    FreeCameraInput in;
    in.forward = 1.0;
    for (int i = 0; i < 120; ++i)
        fc.Update(in, 1.0 / 60.0);
    CHECK(fc.State().position.y > 150.0);
    CHECK_NEAR(fc.State().position.x, 0.0, 1e-6);
    CHECK_NEAR(Length(fc.Velocity()), fc.Settings().speed, fc.Settings().speed * 0.01);

    FreeCameraInput look;
    look.lookPitch = 500.0;
    fc.Update(look, 1.0 / 60.0);
    CHECK(fc.State().angles.pitch <= 89.9 + 1e-12);

    FreeCameraInput strafe;
    strafe.right = 1.0;
    fc.SetState(start);
    for (int i = 0; i < 60; ++i)
        fc.Update(strafe, 1.0 / 60.0);
    CHECK(fc.State().position.x > 50.0);  // facing +Y, right is +X

    FreeCameraInput zoom;
    zoom.fovDelta = -1000.0;
    fc.Update(zoom, 0.016);
    CHECK_NEAR(fc.State().horizontalFov, fc.Settings().minFov, 0);
}
