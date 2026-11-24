// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Minimal vector / quaternion / matrix math for the cinematic core.
//
// Conventions (IW engine, inherited from id Tech 3):
//   * World space is right-handed, +X forward, +Y left, +Z up, units are game
//     units (1 unit is treated as 1 inch for exports).
//   * Euler angles are {pitch, yaw, roll} in degrees. Positive pitch looks
//     down, positive yaw turns left (counter-clockwise seen from above),
//     positive roll rotates about the forward axis following the right-hand
//     rule.
//   * The rotation matrix built from angles is R = Rz(yaw) * Ry(pitch) * Rx(roll)
//     and its columns are the engine's view axis: axis[0] forward, axis[1] left,
//     axis[2] up (AnglesToAxis in the engine).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace iris
{
    inline constexpr double kPi = std::numbers::pi;
    inline constexpr double kDegToRad = kPi / 180.0;
    inline constexpr double kRadToDeg = 180.0 / kPi;

    struct Vec3
    {
        double x = 0.0, y = 0.0, z = 0.0;

        constexpr Vec3() = default;
        constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

        constexpr double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
        constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

        constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator-() const { return {-x, -y, -z}; }
        constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
        constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        constexpr Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
        constexpr bool operator==(const Vec3&) const = default;
    };

    constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }
    constexpr double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    constexpr Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    inline double Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
    inline double Distance(const Vec3& a, const Vec3& b) { return Length(a - b); }
    inline Vec3 Normalize(const Vec3& v)
    {
        const double l = Length(v);
        return l > 1e-12 ? v / l : Vec3{};
    }
    constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }
    constexpr double Lerp(double a, double b, double t) { return a + (b - a) * t; }

    template <typename T>
    constexpr T Clamp(T v, T lo, T hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Wraps an angle in degrees to (-180, 180].
    double WrapDegrees(double degrees);

    // Returns `angle` shifted by a multiple of 360 so that it is the closest
    // equivalent to `reference` (|result - reference| <= 180). Used to unwrap
    // Euler channels before interpolating them component-wise.
    double UnwrapDegreesNear(double angle, double reference);

    // Column-major 3x3 matrix: col[0] = forward, col[1] = left, col[2] = up for
    // view axes.
    struct Mat3
    {
        std::array<Vec3, 3> col{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};

        constexpr Vec3 operator*(const Vec3& v) const { return col[0] * v.x + col[1] * v.y + col[2] * v.z; }
        constexpr Mat3 operator*(const Mat3& o) const
        {
            Mat3 r;
            for (int i = 0; i < 3; ++i)
                r.col[i] = (*this) * o.col[i];
            return r;
        }
        constexpr Mat3 Transposed() const
        {
            Mat3 r;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    r.col[i][j] = col[j][i];
            return r;
        }
        constexpr double Determinant() const { return Dot(col[0], Cross(col[1], col[2])); }
    };

    struct EulerAngles
    {
        double pitch = 0.0;  // degrees, positive looks down
        double yaw = 0.0;    // degrees, positive turns left
        double roll = 0.0;   // degrees
        constexpr bool operator==(const EulerAngles&) const = default;
    };

    struct Quat
    {
        double w = 1.0, x = 0.0, y = 0.0, z = 0.0;

        constexpr Quat() = default;
        constexpr Quat(double w_, double x_, double y_, double z_) : w(w_), x(x_), y(y_), z(z_) {}

        constexpr Quat operator*(const Quat& o) const
        {
            return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                    w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
        }
        constexpr Quat operator*(double s) const { return {w * s, x * s, y * s, z * s}; }
        constexpr Quat operator+(const Quat& o) const { return {w + o.w, x + o.x, y + o.y, z + o.z}; }
        constexpr Quat operator-() const { return {-w, -x, -y, -z}; }
        constexpr Quat Conjugate() const { return {w, -x, -y, -z}; }
        constexpr bool operator==(const Quat&) const = default;
    };

    constexpr double Dot(const Quat& a, const Quat& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
    Quat Normalize(const Quat& q);
    Quat QuatFromAxisAngle(const Vec3& axis, double radians);
    Vec3 Rotate(const Quat& q, const Vec3& v);

    // Unit quaternion logarithm / exponential (pure quaternions for the log).
    Quat Log(const Quat& q);
    Quat Exp(const Quat& q);

    // Shortest-path spherical linear interpolation.
    Quat Slerp(const Quat& a, Quat b, double t);
    // Spherical quadrangle interpolation between q1 and q2 with the inner
    // control points s1 and s2 (see SquadControlPoint).
    Quat Squad(const Quat& q1, const Quat& q2, const Quat& s1, const Quat& s2, double t);
    // Shoemake's inner control point for q given its neighbours.
    Quat SquadControlPoint(const Quat& previous, const Quat& q, const Quat& next);

    Mat3 ToMatrix(const Quat& q);
    Quat FromMatrix(const Mat3& m);

    // Engine-convention conversions.
    Mat3 AnglesToAxis(const EulerAngles& angles);
    EulerAngles AxisToAngles(const Mat3& axis);
    Quat AnglesToQuat(const EulerAngles& angles);
    EulerAngles QuatToAngles(const Quat& q);
    // Forward vector for the angles (axis[0]).
    Vec3 AnglesToForward(const EulerAngles& angles);
    // Angles that look from `from` towards `to` with the given roll.
    EulerAngles LookAt(const Vec3& from, const Vec3& to, double roll = 0.0);

    bool NearlyEqual(double a, double b, double epsilon = 1e-9);
    bool NearlyEqual(const Vec3& a, const Vec3& b, double epsilon = 1e-9);
    // True when both quaternions describe the same rotation (q and -q).
    bool SameRotation(const Quat& a, const Quat& b, double epsilon = 1e-9);
}  // namespace iris
