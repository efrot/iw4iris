#include "iris/math.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    // id Tech 3 AngleVectors, the engine's reference implementation.
    void Q3AngleVectors(const EulerAngles& a, Vec3& forward, Vec3& right, Vec3& up)
    {
        const double sy = std::sin(a.yaw * kDegToRad), cy = std::cos(a.yaw * kDegToRad);
        const double sp = std::sin(a.pitch * kDegToRad), cp = std::cos(a.pitch * kDegToRad);
        const double sr = std::sin(a.roll * kDegToRad), cr = std::cos(a.roll * kDegToRad);
        forward = {cp * cy, cp * sy, -sp};
        right = {(-1 * sr * sp * cy + -1 * cr * -sy), (-1 * sr * sp * sy + -1 * cr * cy), -1 * sr * cp};
        up = {(cr * sp * cy + -sr * -sy), (cr * sp * sy + -sr * cy), cr * cp};
    }

    std::vector<EulerAngles> SampleAngles()
    {
        std::vector<EulerAngles> out;
        for (double p = -80; p <= 80; p += 20)
            for (double y = -170; y <= 180; y += 35)
                for (double r = -60; r <= 60; r += 30)
                    out.push_back({p, y, r});
        return out;
    }
}  // namespace

TEST_CASE("math: AnglesToAxis matches the engine's AngleVectors")
{
    for (const auto& a : SampleAngles())
    {
        Vec3 f, r, u;
        Q3AngleVectors(a, f, r, u);
        const Mat3 m = AnglesToAxis(a);
        CHECK_VEC_NEAR(m.col[0], f, 1e-12);
        CHECK_VEC_NEAR(m.col[1], -r, 1e-12);  // axis[1] is left
        CHECK_VEC_NEAR(m.col[2], u, 1e-12);
        CHECK_NEAR(m.Determinant(), 1.0, 1e-12);
    }
}

TEST_CASE("math: AxisToAngles inverts AnglesToAxis")
{
    for (const auto& a : SampleAngles())
    {
        const EulerAngles b = AxisToAngles(AnglesToAxis(a));
        CHECK_NEAR(b.pitch, a.pitch, 1e-9);
        CHECK_NEAR(WrapDegrees(b.yaw - a.yaw), 0.0, 1e-9);
        CHECK_NEAR(WrapDegrees(b.roll - a.roll), 0.0, 1e-9);
    }
}

TEST_CASE("math: gimbal lock keeps the view direction")
{
    const EulerAngles a{90.0, 37.0, 12.0};
    const EulerAngles b = AxisToAngles(AnglesToAxis(a));
    const Mat3 ma = AnglesToAxis(a), mb = AnglesToAxis(b);
    for (int i = 0; i < 3; ++i)
        CHECK_VEC_NEAR(ma.col[i], mb.col[i], 1e-9);
}

TEST_CASE("math: quaternions agree with the angle matrices")
{
    for (const auto& a : SampleAngles())
    {
        const Quat q = AnglesToQuat(a);
        const Mat3 mq = ToMatrix(q), ma = AnglesToAxis(a);
        for (int i = 0; i < 3; ++i)
            CHECK_VEC_NEAR(mq.col[i], ma.col[i], 1e-12);
        CHECK(SameRotation(FromMatrix(ma), q, 1e-12));
        CHECK_VEC_NEAR(Rotate(q, Vec3(1, 0, 0)), ma.col[0], 1e-12);
    }
}

TEST_CASE("math: slerp takes the shortest arc")
{
    const Quat a = AnglesToQuat({0, 0, 0});
    const Quat b = AnglesToQuat({0, 90, 0});
    CHECK(SameRotation(Slerp(a, b, 0.0), a));
    CHECK(SameRotation(Slerp(a, b, 1.0), b));
    CHECK_NEAR(QuatToAngles(Slerp(a, b, 0.5)).yaw, 45.0, 1e-9);
    // b and -b are the same rotation; slerp must not take the long way.
    CHECK_NEAR(QuatToAngles(Slerp(a, -b, 0.5)).yaw, 45.0, 1e-9);
    const Quat c = AnglesToQuat({0, 170, 0});
    const Quat d = AnglesToQuat({0, -170, 0});
    CHECK_NEAR(std::abs(QuatToAngles(Slerp(c, d, 0.5)).yaw), 180.0, 1e-9);
}

TEST_CASE("math: squad interpolates through its end points")
{
    const Quat q0 = AnglesToQuat({0, -30, 0}), q1 = AnglesToQuat({10, 0, 0}), q2 = AnglesToQuat({-5, 45, 10}),
               q3 = AnglesToQuat({0, 90, 0});
    const Quat s1 = SquadControlPoint(q0, q1, q2), s2 = SquadControlPoint(q1, q2, q3);
    CHECK(SameRotation(Squad(q1, q2, s1, s2, 0.0), q1, 1e-9));
    CHECK(SameRotation(Squad(q1, q2, s1, s2, 1.0), q2, 1e-9));
    for (double t = 0.0; t <= 1.0; t += 0.1)
        CHECK_NEAR(std::sqrt(Dot(Squad(q1, q2, s1, s2, t), Squad(q1, q2, s1, s2, t))), 1.0, 1e-9);
}

TEST_CASE("math: log and exp invert each other")
{
    const Quat q = AnglesToQuat({20, -70, 15});
    CHECK(SameRotation(Exp(Log(q)), q, 1e-12));
}

TEST_CASE("math: angle wrapping")
{
    CHECK_NEAR(WrapDegrees(190.0), -170.0, 1e-12);
    CHECK_NEAR(WrapDegrees(-190.0), 170.0, 1e-12);
    CHECK_NEAR(WrapDegrees(180.0), 180.0, 1e-12);
    CHECK_NEAR(WrapDegrees(-180.0), 180.0, 1e-12);
    CHECK_NEAR(WrapDegrees(720.0 + 15.0), 15.0, 1e-12);
    CHECK_NEAR(UnwrapDegreesNear(-170.0, 170.0), 190.0, 1e-12);
    CHECK_NEAR(UnwrapDegreesNear(10.0, 350.0), 370.0, 1e-12);
}

TEST_CASE("math: LookAt points the forward axis at the target")
{
    const Vec3 from{100, -40, 30}, to{-250, 300, -90};
    const EulerAngles a = LookAt(from, to, 0.0);
    CHECK_VEC_NEAR(AnglesToForward(a), Normalize(to - from), 1e-12);
    CHECK_NEAR(a.roll, 0.0, 1e-12);
}
