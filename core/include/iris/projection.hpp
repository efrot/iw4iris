// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// View / projection math shared by the world-space graphics renderer, the
// viewport gizmos, click selection and the shot generator. One definition
// of how a world point reaches the screen, unit tested, so graphics,
// gizmos and picking can never disagree.
//
// D3D matrices use Direct3D 9's row-vector convention (v' = v * M) and a
// left-handed view space: x right, y up, z into the screen.
#pragma once

#include <array>
#include <optional>

#include "iris/camera.hpp"
#include "iris/mesh.hpp"

namespace iris
{
    struct ViewParams
    {
        Vec3 origin;
        Mat3 axis;                   // engine view axis: forward, left, up
        double tanHalfFovX = 1.0;
        double tanHalfFovY = 0.5625;
        double zNear = 4.0;
    };

    // View parameters for a camera state on a screen of the given aspect
    // ratio (width / height); the FOV is horizontal, as in the engine.
    ViewParams MakeViewParams(const CameraState& camera, double aspect, double zNear = 4.0);

    struct ScreenPoint
    {
        double x = 0.0, y = 0.0;  // pixels, y down
        double viewDepth = 0.0;   // distance along the view direction
    };

    // Nothing for points closer than the near plane.
    std::optional<ScreenPoint> ProjectToScreen(const ViewParams& view, const Vec3& world, double width, double height);

    struct Ray
    {
        Vec3 origin;
        Vec3 direction;  // unit
    };

    Ray ScreenRay(const ViewParams& view, double px, double py, double width, double height);

    std::optional<double> IntersectRayPlane(const Ray& ray, const Vec3& point, const Vec3& normal);
    // Ray against an oriented box given in an object's local space.
    std::optional<double> IntersectRayOrientedBox(const Ray& ray, const Bounds3D& local, const Vec3& position,
                                                  const Mat3& axis, const Vec3& scale);
    // Closest approach parameter along the ray to an infinite line.
    double ClosestOnLine(const Ray& ray, const Vec3& linePoint, const Vec3& lineDirection);

    // Pixel distance from p to the segment ab.
    double DistanceToSegment2D(double px, double py, double ax, double ay, double bx, double by);

    enum class DepthConvention
    {
        StandardInfinite,  // z_ndc = 1 - near / z (depth test LESSEQUAL)
        ReversedInfinite,  // z_ndc = near / z     (depth test GREATEREQUAL)
    };

    using Matrix4 = std::array<float, 16>;  // row-major, row-vector convention

    Matrix4 D3DViewMatrix(const ViewParams& view);
    Matrix4 D3DProjectionMatrix(const ViewParams& view, DepthConvention convention);
    // Graphic space (x right, y up, z out of the front) to world: front faces
    // along the object's forward axis, its right is the object's left axis.
    Matrix4 GraphicWorldMatrix(const Vec3& position, const Mat3& axis, const Vec3& scale);
    Matrix4 Multiply(const Matrix4& a, const Matrix4& b);
    // Applies a row-vector matrix to a point (with perspective divide).
    Vec3 TransformPoint(const Matrix4& m, const Vec3& p, double* w = nullptr);

    double DepthValue(DepthConvention convention, double viewDepth, double zNear);

    // A graphic-space point to world space (same mapping as GraphicWorldMatrix).
    Vec3 GraphicToWorld(const Vec3& local, const Vec3& position, const Mat3& axis, const Vec3& scale);
}  // namespace iris
