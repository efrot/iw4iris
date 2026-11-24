// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/math.hpp"

namespace iris
{
    double WrapDegrees(double degrees)
    {
        double r = std::fmod(degrees + 180.0, 360.0);
        if (r <= 0.0)
            r += 360.0;
        return r - 180.0;
    }

    double UnwrapDegreesNear(double angle, double reference)
    {
        return reference + WrapDegrees(angle - reference);
    }

    Quat Normalize(const Quat& q)
    {
        const double l = std::sqrt(Dot(q, q));
        if (l < 1e-15)
            return {};
        return q * (1.0 / l);
    }

    Quat QuatFromAxisAngle(const Vec3& axis, double radians)
    {
        const Vec3 n = Normalize(axis);
        const double s = std::sin(radians * 0.5);
        return {std::cos(radians * 0.5), n.x * s, n.y * s, n.z * s};
    }

    Vec3 Rotate(const Quat& q, const Vec3& v)
    {
        // v' = v + 2w(u x v) + 2 u x (u x v), u = vector part
        const Vec3 u{q.x, q.y, q.z};
        const Vec3 t = Cross(u, v) * 2.0;
        return v + t * q.w + Cross(u, t);
    }

    Quat Log(const Quat& q)
    {
        const Quat n = Normalize(q);
        const double vlen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (vlen < 1e-12)
            return {0.0, 0.0, 0.0, 0.0};
        const double angle = std::atan2(vlen, n.w);
        const double s = angle / vlen;
        return {0.0, n.x * s, n.y * s, n.z * s};
    }

    Quat Exp(const Quat& q)
    {
        const double angle = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (angle < 1e-12)
            return {1.0, q.x, q.y, q.z};
        const double s = std::sin(angle) / angle;
        return Normalize(Quat{std::cos(angle), q.x * s, q.y * s, q.z * s});
    }

    Quat Slerp(const Quat& a, Quat b, double t)
    {
        double cosom = Dot(a, b);
        if (cosom < 0.0)
        {
            b = -b;
            cosom = -cosom;
        }
        double s0, s1;
        if (1.0 - cosom > 1e-9)
        {
            const double omega = std::acos(Clamp(cosom, -1.0, 1.0));
            const double sinom = std::sin(omega);
            s0 = std::sin((1.0 - t) * omega) / sinom;
            s1 = std::sin(t * omega) / sinom;
        }
        else
        {
            s0 = 1.0 - t;
            s1 = t;
        }
        return Normalize(a * s0 + b * s1);
    }

    static Quat SlerpNoFlip(const Quat& a, const Quat& b, double t)
    {
        const double cosom = Clamp(Dot(a, b), -1.0, 1.0);
        if (std::abs(cosom) > 1.0 - 1e-9)
            return Normalize(a * (1.0 - t) + b * t);
        const double omega = std::acos(cosom);
        const double sinom = std::sin(omega);
        return Normalize(a * (std::sin((1.0 - t) * omega) / sinom) + b * (std::sin(t * omega) / sinom));
    }

    Quat Squad(const Quat& q1, const Quat& q2, const Quat& s1, const Quat& s2, double t)
    {
        return SlerpNoFlip(SlerpNoFlip(q1, q2, t), SlerpNoFlip(s1, s2, t), 2.0 * t * (1.0 - t));
    }

    Quat SquadControlPoint(const Quat& previous, const Quat& q, const Quat& nextIn)
    {
        // Keep the neighbours on the same hemisphere as q.
        const Quat prev = Dot(previous, q) < 0.0 ? -previous : previous;
        const Quat next = Dot(nextIn, q) < 0.0 ? -nextIn : nextIn;
        const Quat inv = q.Conjugate();
        const Quat l1 = Log(inv * next);
        const Quat l2 = Log(inv * prev);
        const Quat sum = (l1 + l2) * -0.25;
        return Normalize(q * Exp(sum));
    }

    Mat3 ToMatrix(const Quat& qIn)
    {
        const Quat q = Normalize(qIn);
        const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat3 m;
        m.col[0] = {1.0 - 2.0 * (yy + zz), 2.0 * (xy + wz), 2.0 * (xz - wy)};
        m.col[1] = {2.0 * (xy - wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz + wx)};
        m.col[2] = {2.0 * (xz + wy), 2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy)};
        return m;
    }

    Quat FromMatrix(const Mat3& m)
    {
        // element (row r, column c) = m.col[c][r]
        auto e = [&](int r, int c) { return m.col[c][r]; };
        const double trace = e(0, 0) + e(1, 1) + e(2, 2);
        Quat q;
        if (trace > 0.0)
        {
            const double s = std::sqrt(trace + 1.0) * 2.0;
            q = {0.25 * s, (e(2, 1) - e(1, 2)) / s, (e(0, 2) - e(2, 0)) / s, (e(1, 0) - e(0, 1)) / s};
        }
        else if (e(0, 0) > e(1, 1) && e(0, 0) > e(2, 2))
        {
            const double s = std::sqrt(1.0 + e(0, 0) - e(1, 1) - e(2, 2)) * 2.0;
            q = {(e(2, 1) - e(1, 2)) / s, 0.25 * s, (e(0, 1) + e(1, 0)) / s, (e(0, 2) + e(2, 0)) / s};
        }
        else if (e(1, 1) > e(2, 2))
        {
            const double s = std::sqrt(1.0 + e(1, 1) - e(0, 0) - e(2, 2)) * 2.0;
            q = {(e(0, 2) - e(2, 0)) / s, (e(0, 1) + e(1, 0)) / s, 0.25 * s, (e(1, 2) + e(2, 1)) / s};
        }
        else
        {
            const double s = std::sqrt(1.0 + e(2, 2) - e(0, 0) - e(1, 1)) * 2.0;
            q = {(e(1, 0) - e(0, 1)) / s, (e(0, 2) + e(2, 0)) / s, (e(1, 2) + e(2, 1)) / s, 0.25 * s};
        }
        return Normalize(q);
    }

    Mat3 AnglesToAxis(const EulerAngles& a)
    {
        const double sp = std::sin(a.pitch * kDegToRad), cp = std::cos(a.pitch * kDegToRad);
        const double sy = std::sin(a.yaw * kDegToRad), cy = std::cos(a.yaw * kDegToRad);
        const double sr = std::sin(a.roll * kDegToRad), cr = std::cos(a.roll * kDegToRad);
        Mat3 m;
        m.col[0] = {cp * cy, cp * sy, -sp};                                       // forward
        m.col[1] = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp};     // left
        m.col[2] = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};     // up
        return m;
    }

    EulerAngles AxisToAngles(const Mat3& axis)
    {
        const Vec3& f = axis.col[0];
        const Vec3& l = axis.col[1];
        const Vec3& u = axis.col[2];
        EulerAngles a;
        const double horizontal = std::sqrt(f.x * f.x + f.y * f.y);
        a.pitch = std::atan2(-f.z, horizontal) * kRadToDeg;
        if (horizontal > 1e-9)
        {
            a.yaw = std::atan2(f.y, f.x) * kRadToDeg;
            a.roll = std::atan2(l.z, u.z) * kRadToDeg;
        }
        else
        {
            // Looking straight up or down: yaw and roll are the same rotation,
            // so put it all in yaw.
            a.roll = 0.0;
            // With roll = 0 and pitch = +-90, left = (-sy, cy, 0).
            a.yaw = std::atan2(-l.x, l.y) * kRadToDeg;
        }
        return a;
    }

    Quat AnglesToQuat(const EulerAngles& a)
    {
        const Quat qz = QuatFromAxisAngle({0, 0, 1}, a.yaw * kDegToRad);
        const Quat qy = QuatFromAxisAngle({0, 1, 0}, a.pitch * kDegToRad);
        const Quat qx = QuatFromAxisAngle({1, 0, 0}, a.roll * kDegToRad);
        return Normalize(qz * qy * qx);
    }

    EulerAngles QuatToAngles(const Quat& q)
    {
        return AxisToAngles(ToMatrix(q));
    }

    Vec3 AnglesToForward(const EulerAngles& a)
    {
        return AnglesToAxis(a).col[0];
    }

    EulerAngles LookAt(const Vec3& from, const Vec3& to, double roll)
    {
        const Vec3 d = to - from;
        EulerAngles a;
        const double horizontal = std::sqrt(d.x * d.x + d.y * d.y);
        a.yaw = horizontal > 1e-12 ? std::atan2(d.y, d.x) * kRadToDeg : 0.0;
        a.pitch = std::atan2(-d.z, horizontal) * kRadToDeg;
        a.roll = roll;
        return a;
    }

    bool NearlyEqual(double a, double b, double epsilon)
    {
        return std::abs(a - b) <= epsilon;
    }

    bool NearlyEqual(const Vec3& a, const Vec3& b, double epsilon)
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
    }

    bool SameRotation(const Quat& a, const Quat& b, double epsilon)
    {
        return std::abs(std::abs(Dot(Normalize(a), Normalize(b))) - 1.0) <= epsilon;
    }
}  // namespace iris
