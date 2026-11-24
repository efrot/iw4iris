#include "iris/curve.hpp"
#include "iris/math.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    ScalarKey K(double t, double v, Interpolation i = Interpolation::Smooth)
    {
        ScalarKey k;
        k.time = t;
        k.value = v;
        k.interpolation = i;
        return k;
    }
}  // namespace

TEST_CASE("curve: empty and single-key curves")
{
    ScalarCurve c(3.5);
    CHECK_NEAR(c.Evaluate(-10), 3.5, 0);
    c.AddKey(K(1.0, 7.0));
    CHECK_NEAR(c.Evaluate(0.0), 7.0, 0);
    CHECK_NEAR(c.Evaluate(5.0), 7.0, 0);
}

TEST_CASE("curve: keys stay sorted and same-time keys replace")
{
    ScalarCurve c;
    c.AddKey(K(2, 2));
    c.AddKey(K(0, 0));
    c.AddKey(K(1, 1));
    c.AddKey(K(1, 5));
    CHECK(c.Size() == 3);
    CHECK_NEAR(c.Keys()[1].value, 5, 0);
    const auto idx = c.MoveKey(0, 3.0);
    CHECK(idx == 2);
    CHECK_NEAR(c.Keys().front().time, 1.0, 0);
}

TEST_CASE("curve: linear, hold and end behaviour")
{
    ScalarCurve c;
    c.AddKey(K(0, 0, Interpolation::Linear));
    c.AddKey(K(2, 10, Interpolation::Hold));
    c.AddKey(K(4, 20));
    CHECK_NEAR(c.Evaluate(1.0), 5.0, 1e-12);
    CHECK_NEAR(c.Evaluate(2.0), 10.0, 1e-12);
    CHECK_NEAR(c.Evaluate(3.999), 10.0, 1e-12);
    CHECK_NEAR(c.Evaluate(4.0), 20.0, 1e-12);
    CHECK_NEAR(c.Evaluate(100.0), 20.0, 1e-12);
    CHECK_NEAR(c.Evaluate(-1.0), 0.0, 1e-12);
}

TEST_CASE("curve: smooth passes through keys and is C1 at inner keys")
{
    ScalarCurve c;
    c.AddKey(K(0, 0));
    c.AddKey(K(1, 3));
    c.AddKey(K(3, 4));
    c.AddKey(K(4, 10));
    for (const auto& k : c.Keys())
        CHECK_NEAR(c.Evaluate(k.time), k.value, 1e-12);
    for (double t : {1.0, 3.0})
    {
        const double h = 1e-6;
        const double left = (c.Evaluate(t) - c.Evaluate(t - h)) / h;
        const double right = (c.Evaluate(t + h) - c.Evaluate(t)) / h;
        CHECK_NEAR(left, right, 1e-3);
    }
}

TEST_CASE("curve: smooth does not overshoot at a local extremum")
{
    ScalarCurve c;
    c.AddKey(K(0, 0));
    c.AddKey(K(1, 10));
    c.AddKey(K(2, 0));
    for (double t = 0; t <= 2.0; t += 0.01)
        CHECK(c.Evaluate(t) <= 10.0 + 1e-9);
}

TEST_CASE("curve: bezier with one-third handles equals a straight line")
{
    ScalarCurve c;
    ScalarKey a = K(0, 0, Interpolation::Bezier);
    a.tangentMode = TangentMode::Free;
    a.outHandle = {1.0 / 3.0, 10.0 / 3.0};
    ScalarKey b = K(1, 10, Interpolation::Bezier);
    b.tangentMode = TangentMode::Free;
    b.inHandle = {-1.0 / 3.0, -10.0 / 3.0};
    c.AddKey(a);
    c.AddKey(b);
    for (double t = 0; t <= 1.0; t += 0.05)
        CHECK_NEAR(c.Evaluate(t), 10.0 * t, 1e-9);
}

TEST_CASE("curve: bezier flat handles ease in and out")
{
    ScalarCurve c;
    ScalarKey a = K(0, 0, Interpolation::Bezier);
    a.tangentMode = TangentMode::Free;
    a.outHandle = {0.5, 0.0};
    ScalarKey b = K(1, 1, Interpolation::Bezier);
    b.tangentMode = TangentMode::Free;
    b.inHandle = {-0.5, 0.0};
    c.AddKey(a);
    c.AddKey(b);
    CHECK_NEAR(c.Evaluate(0.5), 0.5, 1e-9);
    CHECK(c.Evaluate(0.1) < 0.1);
    CHECK(c.Evaluate(0.9) > 0.9);
    double previous = -1;
    for (double t = 0; t <= 1.0; t += 0.01)
    {
        const double v = c.Evaluate(t);
        CHECK(v >= previous - 1e-12);
        previous = v;
    }
}

TEST_CASE("curve: ease mapping")
{
    CHECK_NEAR(ApplyEase(0.0, 0.5, 0.5), 0.0, 1e-12);
    CHECK_NEAR(ApplyEase(1.0, 0.5, 0.5), 1.0, 1e-12);
    CHECK_NEAR(ApplyEase(0.3, 0.0, 0.0), 0.3, 1e-12);
    CHECK(ApplyEase(0.1, 0.8, 0.0) < 0.1);
    double previous = 0.0;
    for (double x = 0.0; x <= 1.0; x += 0.01)
    {
        const double y = ApplyEase(x, 0.7, 0.4);
        CHECK(y >= previous - 1e-12);
        previous = y;
    }
}

TEST_CASE("curve: integration")
{
    ScalarCurve constant(2.0);
    CHECK_NEAR(constant.Integrate(0, 3), 6.0, 1e-12);

    ScalarCurve ramp;
    ramp.AddKey(K(0, 0, Interpolation::Linear));
    ramp.AddKey(K(1, 1, Interpolation::Linear));
    CHECK_NEAR(ramp.Integrate(0, 1), 0.5, 1e-9);
    CHECK_NEAR(ramp.Integrate(0, 2), 1.5, 1e-9);  // holds 1 after the last key

    ScalarCurve hold;
    hold.AddKey(K(0, 1, Interpolation::Hold));
    hold.AddKey(K(1, 3, Interpolation::Hold));
    CHECK_NEAR(hold.Integrate(0, 2), 1.0 + 3.0, 1e-9);
    CHECK_NEAR(hold.Integrate(0.5, 0.5), 0.0, 0);
}

TEST_CASE("curve: auto handles follow the neighbours")
{
    ScalarCurve c;
    c.AddKey(K(0, 0));
    c.AddKey(K(1, 1));
    c.AddKey(K(2, 2));
    c.UpdateAutoHandles();
    const auto& mid = c.Keys()[1];
    CHECK_NEAR(mid.outHandle.dv / mid.outHandle.dt, 1.0, 1e-12);
    CHECK_NEAR(mid.inHandle.dv / mid.inHandle.dt, 1.0, 1e-12);
}

TEST_CASE("curve: string conversions round-trip")
{
    for (auto i : {Interpolation::Hold, Interpolation::Linear, Interpolation::Smooth, Interpolation::CatmullRom,
                   Interpolation::Bezier})
        CHECK(InterpolationFromString(ToString(i)) == i);
    for (auto m : {TangentMode::Auto, TangentMode::Aligned, TangentMode::Free, TangentMode::Flat})
        CHECK(TangentModeFromString(ToString(m)) == m);
    CHECK(!InterpolationFromString("nope").has_value());
}
