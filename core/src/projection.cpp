// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/projection.hpp"

#include <cmath>

namespace iris
{
    ViewParams MakeViewParams(const CameraState& camera, double aspect, double zNear)
    {
        ViewParams v;
        v.origin = camera.position;
        v.axis = AnglesToAxis(camera.angles);
        v.tanHalfFovX = std::tan(Clamp(camera.horizontalFov, 1.0, 179.0) * 0.5 * kDegToRad);
        v.tanHalfFovY = v.tanHalfFovX / std::max(aspect, 1e-6);
        v.zNear = zNear;
        return v;
    }

    namespace
    {
        // World to view space: x right, y up, z forward.
        Vec3 ToView(const ViewParams& view, const Vec3& world)
        {
            const Vec3 d = world - view.origin;
            return {-Dot(d, view.axis.col[1]), Dot(d, view.axis.col[2]), Dot(d, view.axis.col[0])};
        }
    }  // namespace

    std::optional<ScreenPoint> ProjectToScreen(const ViewParams& view, const Vec3& world, double width, double height)
    {
        const Vec3 v = ToView(view, world);
        if (v.z < view.zNear)
            return std::nullopt;
        const double nx = v.x / (v.z * view.tanHalfFovX);
        const double ny = v.y / (v.z * view.tanHalfFovY);
        return ScreenPoint{(nx * 0.5 + 0.5) * width, (0.5 - ny * 0.5) * height, v.z};
    }

    Ray ScreenRay(const ViewParams& view, double px, double py, double width, double height)
    {
        const double nx = (px / std::max(width, 1.0)) * 2.0 - 1.0;
        const double ny = 1.0 - (py / std::max(height, 1.0)) * 2.0;
        const Vec3 dir = view.axis.col[0] - view.axis.col[1] * (nx * view.tanHalfFovX) + view.axis.col[2] * (ny * view.tanHalfFovY);
        return {view.origin, Normalize(dir)};
    }

    std::optional<double> IntersectRayPlane(const Ray& ray, const Vec3& point, const Vec3& normal)
    {
        const double denom = Dot(ray.direction, normal);
        if (std::abs(denom) < 1e-12)
            return std::nullopt;
        const double t = Dot(point - ray.origin, normal) / denom;
        return t >= 0.0 ? std::optional<double>(t) : std::nullopt;
    }

    std::optional<double> IntersectRayOrientedBox(const Ray& ray, const Bounds3D& local, const Vec3& position,
                                                  const Mat3& axis, const Vec3& scale)
    {
        if (!local.Valid())
            return std::nullopt;
        // Into graphic space: inverse of GraphicToWorld.
        auto toLocal = [&](const Vec3& w, bool isDirection) {
            const Vec3 d = isDirection ? w : w - position;
            return Vec3{Dot(d, axis.col[1]) / std::max(scale.x, 1e-9), Dot(d, axis.col[2]) / std::max(scale.y, 1e-9),
                        Dot(d, axis.col[0]) / std::max(scale.z, 1e-9)};
        };
        const Vec3 o = toLocal(ray.origin, false);
        const Vec3 d = toLocal(ray.direction, true);
        double tmin = -1e300, tmax = 1e300;
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs(d[i]) < 1e-12)
            {
                if (o[i] < local.min[i] || o[i] > local.max[i])
                    return std::nullopt;
                continue;
            }
            double t1 = (local.min[i] - o[i]) / d[i];
            double t2 = (local.max[i] - o[i]) / d[i];
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return std::nullopt;
        }
        if (tmax < 0.0)
            return std::nullopt;
        // Local parameters equal world ones only without scale; measure in world.
        const double tl = tmin >= 0.0 ? tmin : tmax;
        const Vec3 hitLocal = o + d * tl;
        const Vec3 hitWorld = GraphicToWorld(hitLocal, position, axis, scale);
        return Dot(hitWorld - ray.origin, ray.direction);
    }

    double ClosestOnLine(const Ray& ray, const Vec3& p, const Vec3& dirIn)
    {
        const Vec3 u = ray.direction, v = Normalize(dirIn), w0 = ray.origin - p;
        const double a = Dot(u, u), b = Dot(u, v), c = Dot(v, v), d = Dot(u, w0), e = Dot(v, w0);
        const double denom = a * c - b * b;
        if (std::abs(denom) < 1e-12)
            return 0.0;
        // Parameter along the line (not the ray).
        return (a * e - b * d) / denom;
    }

    double DistanceToSegment2D(double px, double py, double ax, double ay, double bx, double by)
    {
        const double dx = bx - ax, dy = by - ay;
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
        t = Clamp(t, 0.0, 1.0);
        const double cx = ax + dx * t - px, cy = ay + dy * t - py;
        return std::sqrt(cx * cx + cy * cy);
    }

    Matrix4 D3DViewMatrix(const ViewParams& view)
    {
        const Vec3 right = -view.axis.col[1], up = view.axis.col[2], fwd = view.axis.col[0];
        auto f = [](double v) { return static_cast<float>(v); };
        return {f(right.x), f(up.x), f(fwd.x), 0.0f,
                f(right.y), f(up.y), f(fwd.y), 0.0f,
                f(right.z), f(up.z), f(fwd.z), 0.0f,
                f(-Dot(view.origin, right)), f(-Dot(view.origin, up)), f(-Dot(view.origin, fwd)), 1.0f};
    }

    Matrix4 D3DProjectionMatrix(const ViewParams& view, DepthConvention convention)
    {
        const auto sx = static_cast<float>(1.0 / view.tanHalfFovX);
        const auto sy = static_cast<float>(1.0 / view.tanHalfFovY);
        const auto n = static_cast<float>(view.zNear);
        if (convention == DepthConvention::ReversedInfinite)
            return {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 0, 1, 0, 0, n, 0};
        return {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 1, 0, 0, -n, 0};
    }

    Matrix4 GraphicWorldMatrix(const Vec3& p, const Mat3& axis, const Vec3& s)
    {
        const Vec3 r = axis.col[1] * s.x, u = axis.col[2] * s.y, f = axis.col[0] * s.z;
        auto c = [](double v) { return static_cast<float>(v); };
        return {c(r.x), c(r.y), c(r.z), 0.0f, c(u.x), c(u.y), c(u.z), 0.0f,
                c(f.x), c(f.y), c(f.z), 0.0f, c(p.x), c(p.y), c(p.z), 1.0f};
    }

    Matrix4 Multiply(const Matrix4& a, const Matrix4& b)
    {
        Matrix4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                    sum += double(a[std::size_t(i * 4 + k)]) * b[std::size_t(k * 4 + j)];
                r[std::size_t(i * 4 + j)] = static_cast<float>(sum);
            }
        return r;
    }

    Vec3 TransformPoint(const Matrix4& m, const Vec3& p, double* wOut)
    {
        const double x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
        const double y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
        const double z = p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14];
        const double w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
        if (wOut)
            *wOut = w;
        return std::abs(w) > 1e-12 ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
    }

    double DepthValue(DepthConvention convention, double viewDepth, double zNear)
    {
        if (viewDepth <= 0.0)
            return convention == DepthConvention::ReversedInfinite ? 1e9 : -1e9;
        return convention == DepthConvention::ReversedInfinite ? zNear / viewDepth : 1.0 - zNear / viewDepth;
    }

    Vec3 GraphicToWorld(const Vec3& l, const Vec3& p, const Mat3& axis, const Vec3& s)
    {
        return p + axis.col[1] * (l.x * s.x) + axis.col[2] * (l.y * s.y) + axis.col[0] * (l.z * s.z);
    }
}  // namespace iris
