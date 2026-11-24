// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Geometry for motion graphics: 2D outlines (glyphs, SVG paths, traced
// logos), polygons with holes, triangulation, extrusion with bevels and
// simple primitives. Everything here is game independent and unit tested;
// the engine only uploads the resulting meshes.
//
// Graphic space: x is "right" and y is "up" as the graphic is read, z points
// out of its front face towards the viewer. The engine maps it onto a
// graphic's transform as front = forward axis, right = left axis (a viewer
// facing the graphic sees the graphic's left axis on their right), up = up
// axis; see docs/MOTION_GRAPHICS.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "iris/math.hpp"

namespace iris
{
    struct Vec2
    {
        double x = 0.0, y = 0.0;
        constexpr Vec2() = default;
        constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
        constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
        constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
        constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
        constexpr bool operator==(const Vec2&) const = default;
    };
    constexpr double Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
    constexpr double Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
    inline double Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }

    // A closed outline; the last point connects back to the first (it is not
    // repeated).
    using Contour = std::vector<Vec2>;

    // A filled region: one outer contour (counter-clockwise) and its holes
    // (clockwise) after NormalizeShape / BuildShapes.
    struct Shape2D
    {
        Contour outer;
        std::vector<Contour> holes;
    };

    struct Bounds2D
    {
        Vec2 min{1e300, 1e300};
        Vec2 max{-1e300, -1e300};
        bool Valid() const { return min.x <= max.x && min.y <= max.y; }
        void Add(const Vec2& p);
        void Add(const Bounds2D& b);
        Vec2 Size() const { return Valid() ? max - min : Vec2{}; }
        Vec2 Center() const { return Valid() ? (min + max) * 0.5 : Vec2{}; }
    };

    struct Bounds3D
    {
        Vec3 min{1e300, 1e300, 1e300};
        Vec3 max{-1e300, -1e300, -1e300};
        bool Valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
        void Add(const Vec3& p);
        void Add(const Bounds3D& b);
        Vec3 Size() const { return Valid() ? max - min : Vec3{}; }
        Vec3 Center() const { return Valid() ? (min + max) * 0.5 : Vec3{}; }
    };

    // Positive for counter-clockwise contours (y up).
    double SignedArea(const Contour& contour);
    Bounds2D ContourBounds(const Contour& contour);
    // Even-odd point in polygon test.
    bool PointInContour(const Contour& contour, const Vec2& p);

    // Curve flattening (adaptive subdivision until the control polygon is
    // within `tolerance` of the chord). Appends points after p0 up to and
    // including p1.
    void FlattenQuadratic(Contour& out, const Vec2& p0, const Vec2& c, const Vec2& p1, double tolerance);
    void FlattenCubic(Contour& out, const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, double tolerance);

    // Removes repeated points (closer than `epsilon`), a repeated closing
    // point and collinear points.
    Contour CleanContour(const Contour& contour, double epsilon = 1e-7);
    // Douglas–Peucker simplification of a closed contour.
    Contour SimplifyContour(const Contour& contour, double tolerance);
    // Chaikin corner cutting, `iterations` times (smooths traced pixels).
    Contour SmoothContour(const Contour& contour, int iterations);

    enum class FillRule
    {
        EvenOdd,  // nesting depth decides: even = filled, odd = hole
        NonZero,  // a contour nested in a filled one is a hole only if it winds the other way
    };

    // Groups loose contours into shapes by nesting. Orientation is
    // normalised (outer counter-clockwise, holes clockwise) and degenerate
    // contours (fewer than 3 points or no area) are dropped.
    std::vector<Shape2D> BuildShapes(std::vector<Contour> contours, FillRule rule = FillRule::EvenOdd);

    // Triangulates a shape (earcut). `points` receives the outer points then
    // each hole's points; the indices refer to them and wind counter-clockwise.
    std::vector<std::uint32_t> TriangulateShape(const Shape2D& shape, std::vector<Vec2>& points);

    // Insets every contour of a shape by `distance` (into the solid) with
    // mitred vertices. Returns nothing when the inset collapses or flips a
    // contour (the bevel is too large for the shape).
    std::optional<Shape2D> InsetShape(const Shape2D& shape, double distance);

    // ---------------------------------------------------------------- mesh

    struct MeshVertex
    {
        float px = 0, py = 0, pz = 0;
        float nx = 0, ny = 0, nz = 1;
        float u = 0, v = 0;
    };

    enum class MeshPart : std::uint8_t
    {
        Front,
        Back,
        Side,
        Bevel,
    };

    struct SubMesh
    {
        MeshPart part = MeshPart::Front;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct Mesh
    {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;  // triangle list, counter-clockwise seen from outside
        std::vector<SubMesh> submeshes;

        std::size_t TriangleCount() const { return indices.size() / 3; }
        Bounds3D Bounds() const;
        // Appends another mesh (its submeshes follow this mesh's).
        void Append(const Mesh& other);
        void Translate(const Vec3& offset);
        void Scale(double factor);
    };

    // Checks indices, finite values and unit normals.
    bool ValidateMesh(const Mesh& mesh, std::string* problem = nullptr);
    // True when every edge is shared by exactly two triangles with opposite
    // directions (a closed, consistently wound surface).
    bool IsWatertight(const Mesh& mesh);

    struct ExtrudeSettings
    {
        double depth = 4.0;         // total thickness along z
        double bevel = 0.0;         // bevel size (inset and depth of each bevelled edge)
        int bevelSegments = 2;      // segments of the rounded bevel (1 = chamfer)
        bool front = true;          // generate the front cap
        bool back = true;           // generate the back cap
        bool sides = true;          // generate the side walls
        double smoothAngle = 35.0;  // degrees; sharper corners get split normals
        double uvScale = 0.0;       // world size of one UV unit, 0 = fit the shape bounds
    };

    struct ExtrudeResult
    {
        Mesh mesh;
        double bevelUsed = 0.0;         // the bevel may be reduced to fit the shape
        std::vector<std::string> warnings;
    };

    // Extrudes filled shapes symmetrically around z = 0 (front cap at
    // +depth/2, back cap at -depth/2) with optional round / chamfer bevels on
    // both faces.
    ExtrudeResult ExtrudeShapes(const std::vector<Shape2D>& shapes, const ExtrudeSettings& settings);

    // Flat, single-sided triangulated shapes facing +z (image planes, decals).
    Mesh FlatShapes(const std::vector<Shape2D>& shapes, double uvScale = 0.0);

    // Primitives centred on the origin.
    Mesh MakeBox(const Vec3& size);
    Mesh MakeCylinder(double radius, double height, int segments = 32);  // axis along y (up)
    Mesh MakeSphere(double radius, int segments = 24, int rings = 16);
    Mesh MakePlane(double width, double height);  // in the xy plane, facing +z, UVs 0..1 (v down)
    Mesh MakeRing(double outerRadius, double innerRadius, double depth, int segments = 48);
}  // namespace iris
