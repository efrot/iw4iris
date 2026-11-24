// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <tuple>

#include "vendor/earcut.hpp"

namespace iris
{
    void Bounds2D::Add(const Vec2& p)
    {
        min = {std::min(min.x, p.x), std::min(min.y, p.y)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y)};
    }

    void Bounds2D::Add(const Bounds2D& b)
    {
        if (!b.Valid())
            return;
        Add(b.min);
        Add(b.max);
    }

    void Bounds3D::Add(const Vec3& p)
    {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }

    void Bounds3D::Add(const Bounds3D& b)
    {
        if (!b.Valid())
            return;
        Add(b.min);
        Add(b.max);
    }

    double SignedArea(const Contour& c)
    {
        double a = 0.0;
        for (std::size_t i = 0, n = c.size(); i < n; ++i)
            a += Cross(c[i], c[(i + 1) % n]);
        return a * 0.5;
    }

    Bounds2D ContourBounds(const Contour& c)
    {
        Bounds2D b;
        for (const auto& p : c)
            b.Add(p);
        return b;
    }

    bool PointInContour(const Contour& c, const Vec2& p)
    {
        bool inside = false;
        for (std::size_t i = 0, n = c.size(), j = n - 1; i < n; j = i++)
        {
            const Vec2& a = c[i];
            const Vec2& b = c[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
        return inside;
    }

    // ---------------------------------------------------------------- curves

    namespace
    {
        double DistanceToLine(const Vec2& p, const Vec2& a, const Vec2& b)
        {
            const Vec2 d = b - a;
            const double len = Length(d);
            if (len < 1e-12)
                return Length(p - a);
            return std::abs(Cross(d, p - a)) / len;
        }

        void QuadraticRec(Contour& out, Vec2 p0, Vec2 c, Vec2 p1, double tol, int depth)
        {
            if (depth >= 16 || DistanceToLine(c, p0, p1) <= tol)
            {
                out.push_back(p1);
                return;
            }
            const Vec2 a = (p0 + c) * 0.5, b = (c + p1) * 0.5, m = (a + b) * 0.5;
            QuadraticRec(out, p0, a, m, tol, depth + 1);
            QuadraticRec(out, m, b, p1, tol, depth + 1);
        }

        void CubicRec(Contour& out, Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p1, double tol, int depth)
        {
            if (depth >= 16 || std::max(DistanceToLine(c1, p0, p1), DistanceToLine(c2, p0, p1)) <= tol)
            {
                out.push_back(p1);
                return;
            }
            const Vec2 a = (p0 + c1) * 0.5, b = (c1 + c2) * 0.5, c = (c2 + p1) * 0.5;
            const Vec2 d = (a + b) * 0.5, e = (b + c) * 0.5, m = (d + e) * 0.5;
            CubicRec(out, p0, a, d, m, tol, depth + 1);
            CubicRec(out, m, e, c, p1, tol, depth + 1);
        }
    }  // namespace

    void FlattenQuadratic(Contour& out, const Vec2& p0, const Vec2& c, const Vec2& p1, double tolerance)
    {
        QuadraticRec(out, p0, c, p1, std::max(tolerance, 1e-6), 0);
    }

    void FlattenCubic(Contour& out, const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, double tolerance)
    {
        CubicRec(out, p0, c1, c2, p1, std::max(tolerance, 1e-6), 0);
    }

    Contour CleanContour(const Contour& contour, double epsilon)
    {
        Contour c;
        c.reserve(contour.size());
        for (const auto& p : contour)
            if (c.empty() || Length(p - c.back()) > epsilon)
                c.push_back(p);
        while (c.size() > 1 && Length(c.front() - c.back()) <= epsilon)
            c.pop_back();
        // Drop collinear points (and back-tracking spikes) until stable.
        bool changed = true;
        while (changed && c.size() >= 3)
        {
            changed = false;
            for (std::size_t i = 0; i < c.size() && c.size() >= 3; ++i)
            {
                const Vec2& prev = c[(i + c.size() - 1) % c.size()];
                const Vec2& next = c[(i + 1) % c.size()];
                const Vec2 d1 = c[i] - prev, d2 = next - c[i];
                const double scale = std::max(Length(d1) * Length(d2), 1e-300);
                if (std::abs(Cross(d1, d2)) / scale < 1e-9)
                {
                    c.erase(c.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    --i;
                }
            }
        }
        return c;
    }

    namespace
    {
        void DouglasPeucker(const Contour& pts, std::size_t first, std::size_t last, double tol, std::vector<bool>& keep)
        {
            if (last <= first + 1)
                return;
            double best = -1.0;
            std::size_t index = first;
            for (std::size_t i = first + 1; i < last; ++i)
            {
                const double d = DistanceToLine(pts[i], pts[first], pts[last]);
                if (d > best)
                {
                    best = d;
                    index = i;
                }
            }
            if (best > tol)
            {
                keep[index] = true;
                DouglasPeucker(pts, first, index, tol, keep);
                DouglasPeucker(pts, index, last, tol, keep);
            }
        }
    }  // namespace

    Contour SimplifyContour(const Contour& contour, double tolerance)
    {
        if (contour.size() < 4 || tolerance <= 0.0)
            return contour;
        // Split the loop at the point farthest from the first one.
        std::size_t far = 0;
        double best = -1.0;
        for (std::size_t i = 1; i < contour.size(); ++i)
            if (const double d = Length(contour[i] - contour[0]); d > best)
            {
                best = d;
                far = i;
            }
        Contour loop(contour);
        loop.push_back(contour[0]);
        std::vector<bool> keep(loop.size(), false);
        keep[0] = keep[far] = keep.back() = true;
        DouglasPeucker(loop, 0, far, tolerance, keep);
        DouglasPeucker(loop, far, loop.size() - 1, tolerance, keep);
        Contour out;
        for (std::size_t i = 0; i + 1 < loop.size(); ++i)
            if (keep[i])
                out.push_back(loop[i]);
        return out.size() >= 3 ? out : contour;
    }

    Contour SmoothContour(const Contour& contour, int iterations)
    {
        Contour c = contour;
        for (int it = 0; it < iterations && c.size() >= 3; ++it)
        {
            Contour next;
            next.reserve(c.size() * 2);
            for (std::size_t i = 0; i < c.size(); ++i)
            {
                const Vec2& p = c[i];
                const Vec2& q = c[(i + 1) % c.size()];
                next.push_back(p * 0.75 + q * 0.25);
                next.push_back(p * 0.25 + q * 0.75);
            }
            c = std::move(next);
        }
        return c;
    }

    // ---------------------------------------------------------------- shapes

    std::vector<Shape2D> BuildShapes(std::vector<Contour> contours, FillRule rule)
    {
        struct Info
        {
            Contour c;
            double area = 0.0;  // signed, as given
            int parent = -1;
            int depth = 0;
            bool filled = true;
        };
        std::vector<Info> infos;
        for (auto& c : contours)
        {
            Contour clean = CleanContour(c);
            if (clean.size() < 3)
                continue;
            const double a = SignedArea(clean);
            if (std::abs(a) < 1e-10)
                continue;
            infos.push_back({std::move(clean), a});
        }
        // Immediate parent: the smallest contour containing this one.
        for (std::size_t i = 0; i < infos.size(); ++i)
        {
            double bestArea = 1e300;
            // A test point slightly inside the contour's first edge avoids
            // ambiguity when contours touch at vertices.
            const Contour& ci = infos[i].c;
            const Vec2 mid = (ci[0] + ci[1]) * 0.5;
            for (std::size_t j = 0; j < infos.size(); ++j)
            {
                if (i == j)
                    continue;
                const double aj = std::abs(infos[j].area);
                if (aj <= std::abs(infos[i].area) || aj >= bestArea)
                    continue;
                if (PointInContour(infos[j].c, mid) && PointInContour(infos[j].c, ci[0]))
                {
                    bestArea = aj;
                    infos[i].parent = static_cast<int>(j);
                }
            }
        }
        // Depth and fill state, resolved parents first.
        std::vector<bool> done(infos.size(), false);
        std::function<void(std::size_t)> resolve = [&](std::size_t i) {
            if (done[i])
                return;
            done[i] = true;
            auto& info = infos[i];
            if (info.parent < 0)
            {
                info.depth = 0;
                info.filled = true;
                return;
            }
            resolve(static_cast<std::size_t>(info.parent));
            const auto& p = infos[static_cast<std::size_t>(info.parent)];
            info.depth = p.depth + 1;
            if (rule == FillRule::EvenOdd)
                info.filled = info.depth % 2 == 0;
            else
                info.filled = p.filled ? ((info.area > 0) == (p.area > 0)) : true;
        };
        for (std::size_t i = 0; i < infos.size(); ++i)
            resolve(i);

        std::vector<Shape2D> shapes;
        std::map<std::size_t, std::size_t> shapeOf;
        for (std::size_t i = 0; i < infos.size(); ++i)
            if (infos[i].filled)
            {
                Shape2D s;
                s.outer = infos[i].c;
                if (SignedArea(s.outer) < 0)
                    std::reverse(s.outer.begin(), s.outer.end());
                shapeOf[i] = shapes.size();
                shapes.push_back(std::move(s));
            }
        for (std::size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].filled || infos[i].parent < 0)
                continue;
            const auto it = shapeOf.find(static_cast<std::size_t>(infos[i].parent));
            if (it == shapeOf.end())
                continue;
            Contour hole = infos[i].c;
            if (SignedArea(hole) > 0)
                std::reverse(hole.begin(), hole.end());
            shapes[it->second].holes.push_back(std::move(hole));
        }
        return shapes;
    }

    std::vector<std::uint32_t> TriangulateShape(const Shape2D& shape, std::vector<Vec2>& points)
    {
        using Point = std::array<double, 2>;
        std::vector<std::vector<Point>> polygon;
        points.clear();
        auto add = [&](const Contour& c) {
            std::vector<Point> ring;
            ring.reserve(c.size());
            for (const auto& p : c)
            {
                ring.push_back({p.x, p.y});
                points.push_back(p);
            }
            polygon.push_back(std::move(ring));
        };
        add(shape.outer);
        for (const auto& h : shape.holes)
            add(h);
        std::vector<std::uint32_t> indices = mapbox::earcut<std::uint32_t>(polygon);
        // Wind every triangle counter-clockwise.
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const Vec2& a = points[indices[i]];
            const Vec2& b = points[indices[i + 1]];
            const Vec2& c = points[indices[i + 2]];
            if (Cross(b - a, c - a) < 0)
                std::swap(indices[i + 1], indices[i + 2]);
        }
        return indices;
    }

    namespace
    {
        Vec2 LeftNormal(const Vec2& d)
        {
            const double l = Length(d);
            return l > 1e-15 ? Vec2{-d.y / l, d.x / l} : Vec2{};
        }

        Contour InsetContour(const Contour& c, double distance)
        {
            Contour out;
            out.reserve(c.size());
            const std::size_t n = c.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const Vec2& prev = c[(i + n - 1) % n];
                const Vec2& next = c[(i + 1) % n];
                const Vec2 n1 = LeftNormal(c[i] - prev);
                const Vec2 n2 = LeftNormal(next - c[i]);
                Vec2 m = n1 + n2;
                const double ml = Length(m);
                m = ml > 1e-9 ? m * (1.0 / ml) : n1;
                const double cosHalf = std::max(Dot(m, n1), 0.5);  // miter limit 2x
                out.push_back(c[i] + m * (distance / cosHalf));
            }
            return out;
        }

        bool SegmentsCross(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d)
        {
            const double d1 = Cross(b - a, c - a), d2 = Cross(b - a, d - a);
            const double d3 = Cross(d - c, a - c), d4 = Cross(d - c, b - c);
            return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && std::abs(d1) > 1e-12 &&
                   std::abs(d2) > 1e-12 && std::abs(d3) > 1e-12 && std::abs(d4) > 1e-12;
        }

        // Proper crossings between any two non-adjacent segments of the
        // shape. Skipped (assumed fine) for very large outlines.
        bool HasCrossings(const Shape2D& s)
        {
            std::vector<std::pair<Vec2, Vec2>> segs;
            auto add = [&](const Contour& c) {
                for (std::size_t i = 0; i < c.size(); ++i)
                    segs.emplace_back(c[i], c[(i + 1) % c.size()]);
            };
            add(s.outer);
            for (const auto& h : s.holes)
                add(h);
            if (segs.size() > 4000)
                return false;
            for (std::size_t i = 0; i < segs.size(); ++i)
                for (std::size_t j = i + 1; j < segs.size(); ++j)
                {
                    const auto& [a, b] = segs[i];
                    const auto& [c, d] = segs[j];
                    if (a == c || a == d || b == c || b == d)
                        continue;
                    if (SegmentsCross(a, b, c, d))
                        return true;
                }
            return false;
        }
    }  // namespace

    namespace
    {
        // An inset that passes vertices through each other reverses edges
        // (a collapsed square comes out rotated, with the same orientation).
        bool EdgesKeepDirection(const Contour& before, const Contour& after)
        {
            const std::size_t n = before.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const Vec2 e0 = before[(i + 1) % n] - before[i];
                const Vec2 e1 = after[(i + 1) % n] - after[i];
                if (Dot(e0, e1) <= 0.0)
                    return false;
            }
            return true;
        }
    }  // namespace

    std::optional<Shape2D> InsetShape(const Shape2D& shape, double distance)
    {
        if (distance <= 0.0)
            return shape;
        Shape2D out;
        out.outer = InsetContour(shape.outer, distance);
        if (!EdgesKeepDirection(shape.outer, out.outer))
            return std::nullopt;
        const double a0 = SignedArea(shape.outer), a1 = SignedArea(out.outer);
        if (a1 <= 0.0 || a1 >= a0)
            return std::nullopt;
        for (const auto& h : shape.holes)
        {
            Contour hh = InsetContour(h, distance);
            if (!EdgesKeepDirection(h, hh))
                return std::nullopt;
            const double hb = SignedArea(h), ha = SignedArea(hh);
            if (ha >= 0.0 || ha > hb)  // a hole must stay clockwise and grow
                return std::nullopt;
            if (!PointInContour(out.outer, hh[0]))
                return std::nullopt;
            out.holes.push_back(std::move(hh));
        }
        if (HasCrossings(out))
            return std::nullopt;
        return out;
    }

    // ---------------------------------------------------------------- mesh

    Bounds3D Mesh::Bounds() const
    {
        Bounds3D b;
        for (const auto& v : vertices)
            b.Add({v.px, v.py, v.pz});
        return b;
    }

    void Mesh::Append(const Mesh& other)
    {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        const auto indexBase = static_cast<std::uint32_t>(indices.size());
        vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
        for (const auto i : other.indices)
            indices.push_back(base + i);
        for (auto sm : other.submeshes)
        {
            sm.firstIndex += indexBase;
            submeshes.push_back(sm);
        }
    }

    void Mesh::Translate(const Vec3& o)
    {
        for (auto& v : vertices)
        {
            v.px = static_cast<float>(v.px + o.x);
            v.py = static_cast<float>(v.py + o.y);
            v.pz = static_cast<float>(v.pz + o.z);
        }
    }

    void Mesh::Scale(double f)
    {
        for (auto& v : vertices)
        {
            v.px = static_cast<float>(v.px * f);
            v.py = static_cast<float>(v.py * f);
            v.pz = static_cast<float>(v.pz * f);
        }
        if (f < 0)
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                std::swap(indices[i + 1], indices[i + 2]);
    }

    bool ValidateMesh(const Mesh& mesh, std::string* problem)
    {
        auto fail = [&](const std::string& what) {
            if (problem)
                *problem = what;
            return false;
        };
        if (mesh.indices.size() % 3 != 0)
            return fail("index count is not a multiple of 3");
        for (const auto i : mesh.indices)
            if (i >= mesh.vertices.size())
                return fail("index out of range");
        for (const auto& v : mesh.vertices)
        {
            for (const float f : {v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v})
                if (!std::isfinite(f))
                    return fail("non-finite vertex value");
            const double nl = std::sqrt(double(v.nx) * v.nx + double(v.ny) * v.ny + double(v.nz) * v.nz);
            if (std::abs(nl - 1.0) > 1e-3)
                return fail("normal is not unit length");
        }
        for (const auto& sm : mesh.submeshes)
            if (std::size_t(sm.firstIndex) + sm.indexCount > mesh.indices.size() || sm.indexCount % 3 != 0)
                return fail("submesh range is invalid");
        return true;
    }

    bool IsWatertight(const Mesh& mesh)
    {
        // Weld vertices by position so split normals do not break the test.
        std::map<std::tuple<long long, long long, long long>, std::uint32_t> ids;
        std::vector<std::uint32_t> weld(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            const auto& v = mesh.vertices[i];
            const auto key = std::make_tuple(std::llround(v.px * 1e4), std::llround(v.py * 1e4), std::llround(v.pz * 1e4));
            const auto [it, inserted] = ids.emplace(key, static_cast<std::uint32_t>(ids.size()));
            weld[i] = it->second;
        }
        std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
        {
            const std::uint32_t a = weld[mesh.indices[t]], b = weld[mesh.indices[t + 1]], c = weld[mesh.indices[t + 2]];
            if (a == b || b == c || a == c)
                continue;  // degenerate after welding
            for (const auto& [p, q] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}})
                ++edges[{p, q}];
        }
        for (const auto& [e, count] : edges)
        {
            if (count != 1)
                return false;
            const auto back = edges.find({e.second, e.first});
            if (back == edges.end() || back->second != 1)
                return false;
        }
        return !edges.empty();
    }

    namespace
    {
        MeshVertex MakeVertex(const Vec3& p, const Vec3& n, double u, double v)
        {
            const Vec3 nn = Normalize(n);
            return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                    static_cast<float>(nn.x), static_cast<float>(nn.y), static_cast<float>(nn.z),
                    static_cast<float>(u), static_cast<float>(v)};
        }

        struct UvFrame
        {
            Bounds2D bounds;
            double scale = 1.0;  // world units per UV unit
            bool fit = true;
            double U(const Vec2& p) const
            {
                return fit ? (p.x - bounds.min.x) / std::max(bounds.Size().x, 1e-9) : p.x / scale;
            }
            double V(const Vec2& p) const
            {
                return fit ? (bounds.max.y - p.y) / std::max(bounds.Size().y, 1e-9) : -p.y / scale;
            }
        };

        void AddCap(Mesh& mesh, const Shape2D& shape, double z, bool front, const UvFrame& uv)
        {
            std::vector<Vec2> pts;
            const auto tris = TriangulateShape(shape, pts);
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const Vec3 n{0, 0, front ? 1.0 : -1.0};
            for (const auto& p : pts)
                mesh.vertices.push_back(MakeVertex({p.x, p.y, z}, n, front ? uv.U(p) : 1.0 - uv.U(p), uv.V(p)));
            for (std::size_t i = 0; i + 2 < tris.size(); i += 3)
            {
                mesh.indices.push_back(base + tris[i]);
                if (front)
                {
                    mesh.indices.push_back(base + tris[i + 1]);
                    mesh.indices.push_back(base + tris[i + 2]);
                }
                else
                {
                    mesh.indices.push_back(base + tris[i + 2]);
                    mesh.indices.push_back(base + tris[i + 1]);
                }
            }
        }

        // Outward (right-hand) unit normals of a contour's edges and whether
        // each vertex is smooth (edges meet at less than the smooth angle).
        struct EdgeInfo
        {
            std::vector<Vec2> edgeNormal;   // edge i: from point i to i+1
            std::vector<Vec2> vertexNormal; // averaged at vertex i
            std::vector<bool> smooth;
            std::vector<double> arc;        // arc length at vertex i
            double length = 0.0;
        };

        EdgeInfo AnalyseContour(const Contour& c, double smoothAngleDeg)
        {
            EdgeInfo e;
            const std::size_t n = c.size();
            e.edgeNormal.resize(n);
            e.vertexNormal.resize(n);
            e.smooth.resize(n);
            e.arc.resize(n + 1);
            for (std::size_t i = 0; i < n; ++i)
            {
                const Vec2 d = c[(i + 1) % n] - c[i];
                const Vec2 left = LeftNormal(d);
                e.edgeNormal[i] = {-left.x, -left.y};
                e.arc[i + 1] = e.arc[i] + Length(d);
            }
            e.length = e.arc[n];
            const double cosLimit = std::cos(smoothAngleDeg * kDegToRad);
            for (std::size_t i = 0; i < n; ++i)
            {
                const Vec2& a = e.edgeNormal[(i + n - 1) % n];
                const Vec2& b = e.edgeNormal[i];
                e.smooth[i] = Dot(a, b) >= cosLimit;
                Vec2 m = a + b;
                const double l = Length(m);
                e.vertexNormal[i] = l > 1e-9 ? m * (1.0 / l) : b;
            }
            return e;
        }

        // A strip between two rings of the same contour (same vertex count).
        // `lo` / `hi` give the ring contours, their z and the z component
        // weights of the normals (normal = outward * cosW + z * sinW).
        void AddStrip(Mesh& mesh, const Contour& lo, const Contour& hi, double zLo, double zHi, double nzLo,
                      double nzHi, const EdgeInfo& info, bool flip, double uvScale, double vLo, double vHi)
        {
            const std::size_t n = lo.size();
            const double cwLo = std::sqrt(std::max(0.0, 1.0 - nzLo * nzLo));
            const double cwHi = std::sqrt(std::max(0.0, 1.0 - nzHi * nzHi));
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::size_t j = (i + 1) % n;
                const Vec2 nA = info.smooth[i] ? info.vertexNormal[i] : info.edgeNormal[i];
                const Vec2 nB = info.smooth[j] ? info.vertexNormal[j] : info.edgeNormal[i];
                const double uA = info.arc[i] / uvScale, uB = info.arc[i + 1] / uvScale;
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(MakeVertex({lo[i].x, lo[i].y, zLo}, {nA.x * cwLo, nA.y * cwLo, nzLo}, uA, vLo));
                mesh.vertices.push_back(MakeVertex({lo[j].x, lo[j].y, zLo}, {nB.x * cwLo, nB.y * cwLo, nzLo}, uB, vLo));
                mesh.vertices.push_back(MakeVertex({hi[j].x, hi[j].y, zHi}, {nB.x * cwHi, nB.y * cwHi, nzHi}, uB, vHi));
                mesh.vertices.push_back(MakeVertex({hi[i].x, hi[i].y, zHi}, {nA.x * cwHi, nA.y * cwHi, nzHi}, uA, vHi));
                const std::array<std::uint32_t, 6> quad =
                    flip ? std::array<std::uint32_t, 6>{0, 2, 1, 0, 3, 2} : std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3};
                for (const auto q : quad)
                    mesh.indices.push_back(base + q);
            }
        }

        void BeginPart(Mesh& mesh, MeshPart part)
        {
            mesh.submeshes.push_back({part, static_cast<std::uint32_t>(mesh.indices.size()), 0});
        }

        void EndPart(Mesh& mesh)
        {
            auto& sm = mesh.submeshes.back();
            sm.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - sm.firstIndex;
            if (sm.indexCount == 0)
                mesh.submeshes.pop_back();
        }
    }  // namespace

    ExtrudeResult ExtrudeShapes(const std::vector<Shape2D>& shapes, const ExtrudeSettings& s)
    {
        ExtrudeResult result;
        Mesh& mesh = result.mesh;
        const double depth = std::max(s.depth, 0.0);
        const int segments = std::clamp(s.bevelSegments, 1, 16);

        Bounds2D all;
        for (const auto& sh : shapes)
            all.Add(ContourBounds(sh.outer));
        UvFrame uv{all, s.uvScale > 0 ? s.uvScale : 1.0, s.uvScale <= 0};
        const double sideUv = s.uvScale > 0 ? s.uvScale : std::max({all.Size().x, all.Size().y, 1e-9});

        // One bevel size for the whole graphic: the largest that fits every shape.
        double bevel = std::clamp(s.bevel, 0.0, depth * 0.49);
        if (bevel > 0.0)
        {
            for (int attempt = 0; attempt < 6 && bevel > 0.0; ++attempt)
            {
                bool ok = true;
                for (const auto& sh : shapes)
                    if (!InsetShape(sh, bevel))
                    {
                        ok = false;
                        break;
                    }
                if (ok)
                    break;
                bevel *= 0.5;
                if (attempt == 5)
                    bevel = 0.0;
            }
            if (bevel < s.bevel - 1e-9)
                result.warnings.push_back(bevel > 0.0 ? "The bevel was reduced to fit the shape."
                                                      : "The bevel does not fit the shape and was turned off.");
        }
        result.bevelUsed = bevel;

        const double half = depth * 0.5;
        const double zSide = half - bevel;  // where the side walls end and the bevel begins

        for (const auto& shape : shapes)
        {
            // Rings from the outline (inset 0) to the cap (inset = bevel).
            std::vector<Shape2D> rings;
            std::vector<double> ringZ, ringNz;
            if (bevel > 0.0)
            {
                for (int k = 0; k <= segments; ++k)
                {
                    const double phi = (static_cast<double>(k) / segments) * kPi * 0.5;
                    const double inset = bevel * (1.0 - std::cos(phi));
                    auto ring = InsetShape(shape, inset);
                    if (!ring)
                        ring = InsetShape(shape, bevel);  // unreachable after the fit test, kept defensive
                    rings.push_back(ring ? *ring : shape);
                    ringZ.push_back(zSide + bevel * std::sin(phi));
                    // Normal tilts from the side (0) to the face (1); a chamfer
                    // gets the average of both.
                    ringNz.push_back(segments == 1 ? std::sin(kPi * 0.25) : std::sin(phi));
                }
            }
            else
            {
                rings.push_back(shape);
                ringZ.push_back(half);
                ringNz.push_back(0.0);
            }
            const Shape2D& cap = rings.back();

            if (s.front)
            {
                BeginPart(mesh, MeshPart::Front);
                AddCap(mesh, cap, half, true, uv);
                EndPart(mesh);
            }
            if (s.back)
            {
                BeginPart(mesh, MeshPart::Back);
                AddCap(mesh, cap, -half, false, uv);
                EndPart(mesh);
            }

            auto forEachContour = [&](auto&& fn) {
                fn([&](const Shape2D& r) -> const Contour& { return r.outer; });
                for (std::size_t h = 0; h < shape.holes.size(); ++h)
                    fn([h](const Shape2D& r) -> const Contour& { return r.holes[h]; });
            };

            if (bevel > 0.0)
            {
                BeginPart(mesh, MeshPart::Bevel);
                forEachContour([&](auto contourOf) {
                    for (int k = 0; k < segments; ++k)
                    {
                        const Contour& lo = contourOf(rings[static_cast<std::size_t>(k)]);
                        const Contour& hi = contourOf(rings[static_cast<std::size_t>(k + 1)]);
                        const EdgeInfo info = AnalyseContour(lo, s.smoothAngle);
                        const double vLo = (half - ringZ[static_cast<std::size_t>(k)]) / sideUv;
                        const double vHi = (half - ringZ[static_cast<std::size_t>(k + 1)]) / sideUv;
                        const double nzLo = segments == 1 ? ringNz[0] : ringNz[static_cast<std::size_t>(k)];
                        const double nzHi = segments == 1 ? ringNz[0] : ringNz[static_cast<std::size_t>(k + 1)];
                        // Front bevel
                        AddStrip(mesh, lo, hi, ringZ[static_cast<std::size_t>(k)], ringZ[static_cast<std::size_t>(k + 1)],
                                 nzLo, nzHi, info, false, sideUv, vLo, vHi);
                        // Back bevel (mirrored in z)
                        if (s.back)
                            AddStrip(mesh, lo, hi, -ringZ[static_cast<std::size_t>(k)],
                                     -ringZ[static_cast<std::size_t>(k + 1)], -nzLo, -nzHi, info, true, sideUv,
                                     (depth - (half - ringZ[static_cast<std::size_t>(k)])) / sideUv,
                                     (depth - (half - ringZ[static_cast<std::size_t>(k + 1)])) / sideUv);
                    }
                });
                EndPart(mesh);
            }

            if (s.sides && zSide > 1e-9)
            {
                BeginPart(mesh, MeshPart::Side);
                forEachContour([&](auto contourOf) {
                    const Contour& c = contourOf(rings.front());
                    const EdgeInfo info = AnalyseContour(c, s.smoothAngle);
                    AddStrip(mesh, c, c, -zSide, zSide, 0.0, 0.0, info, false, sideUv, (half + zSide) / sideUv,
                             (half - zSide) / sideUv);
                });
                EndPart(mesh);
            }
        }
        return result;
    }

    Mesh FlatShapes(const std::vector<Shape2D>& shapes, double uvScale)
    {
        Mesh mesh;
        Bounds2D all;
        for (const auto& sh : shapes)
            all.Add(ContourBounds(sh.outer));
        const UvFrame uv{all, uvScale > 0 ? uvScale : 1.0, uvScale <= 0};
        BeginPart(mesh, MeshPart::Front);
        for (const auto& sh : shapes)
            AddCap(mesh, sh, 0.0, true, uv);
        EndPart(mesh);
        return mesh;
    }

    // ---------------------------------------------------------------- primitives

    Mesh MakeBox(const Vec3& size)
    {
        Mesh m;
        const Vec3 h = size * 0.5;
        struct Face
        {
            Vec3 n, u, v;
        };
        const Face faces[] = {
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}}, {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}},
            {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}},
        };
        BeginPart(m, MeshPart::Front);
        for (const auto& f : faces)
        {
            const auto base = static_cast<std::uint32_t>(m.vertices.size());
            const double corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (const auto& c : corners)
            {
                const Vec3 p{f.n.x * h.x + f.u.x * c[0] * h.x + f.v.x * c[1] * h.x,
                             f.n.y * h.y + f.u.y * c[0] * h.y + f.v.y * c[1] * h.y,
                             f.n.z * h.z + f.u.z * c[0] * h.z + f.v.z * c[1] * h.z};
                m.vertices.push_back(MakeVertex(p, f.n, (c[0] + 1) * 0.5, (1 - c[1]) * 0.5));
            }
            for (const std::uint32_t q : {0u, 1u, 2u, 0u, 2u, 3u})
                m.indices.push_back(base + q);
        }
        EndPart(m);
        return m;
    }

    Mesh MakeCylinder(double radius, double height, int segments)
    {
        segments = std::clamp(segments, 3, 256);
        Contour circle;
        for (int i = 0; i < segments; ++i)
        {
            const double a = 2.0 * kPi * i / segments;
            circle.push_back({radius * std::cos(a), radius * std::sin(a)});
        }
        // Extruded along z, then turned so the axis is y (up).
        ExtrudeSettings es;
        es.depth = height;
        es.smoothAngle = 360.0 / segments + 1.0;
        Mesh m = ExtrudeShapes({Shape2D{circle, {}}}, es).mesh;
        for (auto& v : m.vertices)
        {
            std::swap(v.py, v.pz);
            v.pz = -v.pz;
            std::swap(v.ny, v.nz);
            v.nz = -v.nz;
        }
        return m;
    }

    Mesh MakeSphere(double radius, int segments, int rings)
    {
        segments = std::clamp(segments, 3, 256);
        rings = std::clamp(rings, 2, 256);
        Mesh m;
        BeginPart(m, MeshPart::Front);
        for (int r = 0; r <= rings; ++r)
        {
            const double theta = kPi * r / rings;  // 0 at the top
            for (int s = 0; s <= segments; ++s)
            {
                const double phi = 2.0 * kPi * s / segments;
                const Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                m.vertices.push_back(MakeVertex(n * radius, n, double(s) / segments, double(r) / rings));
            }
        }
        const auto row = static_cast<std::uint32_t>(segments + 1);
        for (std::uint32_t r = 0; r < static_cast<std::uint32_t>(rings); ++r)
            for (std::uint32_t s = 0; s < static_cast<std::uint32_t>(segments); ++s)
            {
                const std::uint32_t a = r * row + s, b = a + row;
                if (r != 0)
                    for (const auto i : {a, a + 1, b})
                        m.indices.push_back(i);
                if (r + 1 != static_cast<std::uint32_t>(rings))
                    for (const auto i : {a + 1, b + 1, b})
                        m.indices.push_back(i);
            }
        EndPart(m);
        return m;
    }

    Mesh MakePlane(double width, double height)
    {
        Mesh m;
        const double w = width * 0.5, h = height * 0.5;
        BeginPart(m, MeshPart::Front);
        m.vertices = {MakeVertex({-w, -h, 0}, {0, 0, 1}, 0, 1), MakeVertex({w, -h, 0}, {0, 0, 1}, 1, 1),
                      MakeVertex({w, h, 0}, {0, 0, 1}, 1, 0), MakeVertex({-w, h, 0}, {0, 0, 1}, 0, 0)};
        m.indices = {0, 1, 2, 0, 2, 3};
        EndPart(m);
        return m;
    }

    Mesh MakeRing(double outerRadius, double innerRadius, double depth, int segments)
    {
        segments = std::clamp(segments, 3, 256);
        Contour outer, inner;
        for (int i = 0; i < segments; ++i)
        {
            const double a = 2.0 * kPi * i / segments;
            outer.push_back({outerRadius * std::cos(a), outerRadius * std::sin(a)});
            inner.push_back({innerRadius * std::cos(a), innerRadius * std::sin(a)});
        }
        std::reverse(inner.begin(), inner.end());
        ExtrudeSettings es;
        es.depth = depth;
        es.smoothAngle = 360.0 / segments + 1.0;
        return ExtrudeShapes({Shape2D{outer, {inner}}}, es).mesh;
    }
}  // namespace iris
