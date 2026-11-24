// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Motion graphics geometry: outlines, nesting, triangulation, extrusion,
// bevels, primitives, fonts, SVG / raster logos and OBJ models.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "iris/font.hpp"
#include "iris/logo.hpp"
#include "iris/mesh.hpp"
#include "iris/model.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    Contour Square(double x0, double y0, double size, bool ccw = true)
    {
        Contour c{{x0, y0}, {x0 + size, y0}, {x0 + size, y0 + size}, {x0, y0 + size}};
        if (!ccw)
            std::reverse(c.begin(), c.end());
        return c;
    }

    Contour Circle(double r, int n)
    {
        Contour c;
        for (int i = 0; i < n; ++i)
            c.push_back({r * std::cos(2 * kPi * i / n), r * std::sin(2 * kPi * i / n)});
        return c;
    }

    // Volume enclosed by a closed mesh (positive when wound outwards).
    double SignedVolume(const Mesh& m)
    {
        double v = 0.0;
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
        {
            const auto& a = m.vertices[m.indices[i]];
            const auto& b = m.vertices[m.indices[i + 1]];
            const auto& c = m.vertices[m.indices[i + 2]];
            const Vec3 pa{a.px, a.py, a.pz}, pb{b.px, b.py, b.pz}, pc{c.px, c.py, c.pz};
            v += Dot(pa, Cross(pb, pc)) / 6.0;
        }
        return v;
    }

    double TriangleArea(const std::vector<Vec2>& pts, const std::vector<std::uint32_t>& idx)
    {
        double a = 0;
        for (std::size_t i = 0; i + 2 < idx.size(); i += 3)
            a += Cross(pts[idx[i + 1]] - pts[idx[i]], pts[idx[i + 2]] - pts[idx[i]]) * 0.5;
        return a;
    }

    std::filesystem::path FontPath() { return std::filesystem::path(IW4IRIS_SOURCE_DIR) / "resources/fonts/Rubik-Regular.ttf"; }

    std::filesystem::path TempFile(const char* name, const std::string& content)
    {
        const auto p = std::filesystem::temp_directory_path() / name;
        std::ofstream(p, std::ios::binary) << content;
        return p;
    }

    // 32-bit uncompressed TGA (top-left origin) from an alpha function.
    std::vector<std::uint8_t> MakeTga(int w, int h, double (*alpha)(int, int))
    {
        std::vector<std::uint8_t> b(18, 0);
        b[2] = 2;
        b[12] = static_cast<std::uint8_t>(w & 0xFF);
        b[13] = static_cast<std::uint8_t>(w >> 8);
        b[14] = static_cast<std::uint8_t>(h & 0xFF);
        b[15] = static_cast<std::uint8_t>(h >> 8);
        b[16] = 32;
        b[17] = 0x28;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                b.push_back(40);   // B
                b.push_back(120);  // G
                b.push_back(200);  // R
                b.push_back(static_cast<std::uint8_t>(std::lround(Clamp(alpha(x, y), 0.0, 1.0) * 255)));
            }
        return b;
    }
}  // namespace

TEST_CASE("geometry: signed area, bounds and point in contour")
{
    const Contour sq = Square(0, 0, 2);
    CHECK_NEAR(SignedArea(sq), 4.0, 1e-12);
    Contour cw = sq;
    std::reverse(cw.begin(), cw.end());
    CHECK_NEAR(SignedArea(cw), -4.0, 1e-12);
    CHECK(PointInContour(sq, {1, 1}));
    CHECK(!PointInContour(sq, {3, 1}));
    const auto b = ContourBounds(sq);
    CHECK_NEAR(b.Size().x, 2.0, 0);
}

TEST_CASE("geometry: curve flattening stays within tolerance")
{
    Contour c{{0, 0}};
    FlattenCubic(c, {0, 0}, {0, 10}, {10, 10}, {10, 0}, 0.01);
    CHECK(c.size() > 8);
    CHECK(c.back() == Vec2(10, 0));
    // Every point lies close to the true curve (dense sampling as reference).
    for (const auto& p : c)
    {
        double best = 1e9;
        for (int i = 0; i <= 2000; ++i)
        {
            const double t = i / 2000.0, u = 1 - t;
            const Vec2 q{3 * u * t * t * 10 + t * t * t * 10, 3 * u * u * t * 10 + 3 * u * t * t * 10};
            best = std::min(best, Length(q - p));
        }
        CHECK(best < 0.05);
    }
    Contour q{{0, 0}};
    FlattenQuadratic(q, {0, 0}, {5, 10}, {10, 0}, 0.05);
    CHECK(q.size() > 4);
}

TEST_CASE("geometry: clean, simplify and smooth contours")
{
    Contour c{{0, 0}, {1, 0}, {2, 0}, {2, 0}, {2, 2}, {0, 2}, {0, 0}};
    const auto clean = CleanContour(c);
    CHECK(clean.size() == 4);  // the collinear (1,0), the duplicate and the closing point are gone
    Contour noisy;
    for (int i = 0; i < 100; ++i)
        noisy.push_back({i * 0.1, (i % 2) * 0.001});
    noisy.push_back({10, 5});
    const auto simple = SimplifyContour(noisy, 0.01);
    CHECK(simple.size() <= 4);
    CHECK(SmoothContour(Square(0, 0, 1), 1).size() == 8);
}

TEST_CASE("geometry: nesting builds shapes with holes (even-odd and non-zero)")
{
    // Square with a hole and an island inside the hole.
    auto shapes = BuildShapes({Square(0, 0, 10), Square(2, 2, 6), Square(4, 4, 2)}, FillRule::EvenOdd);
    CHECK(shapes.size() == 2);
    CHECK(shapes[0].holes.size() + shapes[1].holes.size() == 1);
    for (const auto& s : shapes)
    {
        CHECK(SignedArea(s.outer) > 0);
        for (const auto& h : s.holes)
            CHECK(SignedArea(h) < 0);
    }
    // Non-zero: a nested contour winding the same way is not a hole.
    auto same = BuildShapes({Square(0, 0, 10), Square(2, 2, 6)}, FillRule::NonZero);
    CHECK(same.size() == 2);
    auto opposite = BuildShapes({Square(0, 0, 10), Square(2, 2, 6, false)}, FillRule::NonZero);
    CHECK(opposite.size() == 1);
    CHECK(opposite[0].holes.size() == 1);
    // Degenerate input is dropped.
    CHECK(BuildShapes({Contour{{0, 0}, {1, 1}}}).empty());
}

TEST_CASE("geometry: triangulation covers the shape minus its holes")
{
    Shape2D s{Square(0, 0, 10), {Square(3, 3, 4, false)}};
    std::vector<Vec2> pts;
    const auto idx = TriangulateShape(s, pts);
    CHECK(idx.size() % 3 == 0);
    CHECK(idx.size() / 3 == 8);  // n + 2h - 2 for 8 vertices, 1 hole
    CHECK_NEAR(TriangleArea(pts, idx), 100.0 - 16.0, 1e-9);
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3)
        CHECK(Cross(pts[idx[i + 1]] - pts[idx[i]], pts[idx[i + 2]] - pts[idx[i]]) > 0);  // counter-clockwise
}

TEST_CASE("geometry: inset shapes shrink outers and grow holes, refuse collapse")
{
    Shape2D s{Square(0, 0, 10), {Square(4, 4, 2, false)}};
    const auto in = InsetShape(s, 1.0);
    CHECK(in.has_value());
    CHECK_NEAR(SignedArea(in->outer), 64.0, 1e-9);
    CHECK_NEAR(SignedArea(in->holes[0]), -16.0, 1e-9);
    CHECK(!InsetShape(s, 3.5).has_value());  // the hole would cross the outline
}

TEST_CASE("geometry: extrusion is a closed, outward-wound solid")
{
    for (const double bevel : {0.0, 0.5})
        for (const int segments : {1, 3})
        {
            ExtrudeSettings es;
            es.depth = 4;
            es.bevel = bevel;
            es.bevelSegments = segments;
            const auto r = ExtrudeShapes({Shape2D{Square(0, 0, 10), {Square(3, 3, 4, false)}}}, es);
            std::string problem;
            CHECK_MSG(ValidateMesh(r.mesh, &problem), problem);
            CHECK(IsWatertight(r.mesh));
            const double v = SignedVolume(r.mesh);
            CHECK(v > 0);
            if (bevel == 0.0)
                CHECK_NEAR(v, (100.0 - 16.0) * 4.0, 1e-6);
            else
                CHECK(v < (100.0 - 16.0) * 4.0 && v > (100.0 - 16.0) * 4.0 * 0.8);
            const auto b = r.mesh.Bounds();
            CHECK_NEAR(b.min.z, -2.0, 1e-5);
            CHECK_NEAR(b.max.z, 2.0, 1e-5);
            bool front = false, side = false, bev = false, back = false;
            for (const auto& sm : r.mesh.submeshes)
            {
                front |= sm.part == MeshPart::Front;
                back |= sm.part == MeshPart::Back;
                side |= sm.part == MeshPart::Side;
                bev |= sm.part == MeshPart::Bevel;
            }
            CHECK(front && back && side);
            CHECK(bev == (bevel > 0.0));
        }
}

TEST_CASE("geometry: an oversized bevel is reduced with a warning")
{
    ExtrudeSettings es;
    es.depth = 20;
    es.bevel = 4;
    const auto r = ExtrudeShapes({Shape2D{Square(0, 0, 5), {}}}, es);
    CHECK(r.bevelUsed < 4.0);
    CHECK(!r.warnings.empty());
    CHECK(IsWatertight(r.mesh));
}

TEST_CASE("geometry: primitives are closed with the right volume")
{
    const Mesh box = MakeBox({2, 4, 6});
    CHECK(IsWatertight(box));
    CHECK_NEAR(SignedVolume(box), 48.0, 1e-5);
    const Mesh sphere = MakeSphere(10, 48, 32);
    CHECK(ValidateMesh(sphere));
    CHECK(IsWatertight(sphere));
    CHECK_NEAR(SignedVolume(sphere) / (4.0 / 3.0 * kPi * 1000.0), 1.0, 0.02);
    const Mesh cyl = MakeCylinder(5, 10, 64);
    CHECK(IsWatertight(cyl));
    CHECK_NEAR(SignedVolume(cyl) / (kPi * 25 * 10), 1.0, 0.01);
    CHECK(cyl.Bounds().Size().y > 9.99);  // the axis is up (y)
    const Mesh ring = MakeRing(10, 7, 2, 48);
    CHECK(IsWatertight(ring));
    CHECK(SignedVolume(ring) > 0);
    const Mesh plane = MakePlane(4, 2);
    CHECK(plane.TriangleCount() == 2);
    CHECK(ValidateMesh(plane));
}

TEST_CASE("geometry: flat shapes face +z")
{
    const Mesh m = FlatShapes({Shape2D{Circle(5, 32), {}}});
    CHECK(ValidateMesh(m));
    for (const auto& v : m.vertices)
        CHECK_NEAR(v.nz, 1.0, 1e-6);
}

TEST_CASE("font: Rubik loads with names and glyph outlines with holes")
{
    std::string error;
    const auto font = Font::Load(FontPath(), &error);
    CHECK_MSG(font != nullptr, error);
    if (!font)
        return;
    CHECK(font->Info().family == "Rubik");
    CHECK(!font->Info().RestrictedEmbedding());
    CHECK(font->Ascent(100) > 0 && font->Descent(100) < 0);
    auto shapesOf = [&](char32_t c) { return BuildShapes(font->GlyphOutline(c, 100, 0.1), FillRule::NonZero); };
    const auto o = shapesOf(U'O');
    CHECK(o.size() == 1 && o[0].holes.size() == 1);
    const auto b = shapesOf(U'B');
    CHECK(b.size() == 1 && b[0].holes.size() == 2);
    const auto i = shapesOf(U'i');
    CHECK(i.size() == 2);
    const auto r = shapesOf(U'R');
    CHECK(r.size() == 1 && r[0].holes.size() == 1);
    CHECK(font->Advance(U'W', 100) > font->Advance(U'i', 100));
}

TEST_CASE("font: UTF-8 decoding")
{
    const auto s = DecodeUtf8("A\xC3\x84\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK(s.size() == 4);
    CHECK(s[1] == 0xC4 && s[2] == 0x20AC && s[3] == 0x1F600);
    const auto bad = DecodeUtf8("a\xFF" "b");
    CHECK(bad.size() == 3 && bad[1] == 0xFFFD);
}

TEST_CASE("font: text layout aligns, kerns, wraps lines and bends on a circle")
{
    const auto font = Font::Load(FontPath());
    if (!font)
        return;
    TextSettings st;
    st.size = 32;
    const auto centred = LayoutText(*font, "IW4IRIS", st);
    CHECK(centred.glyphs.size() == 7);
    CHECK_NEAR(centred.bounds.Center().x, 0.0, 2.0);
    CHECK_NEAR(centred.bounds.Center().y, 0.0, 6.0);
    st.align = TextAlign::Left;
    const auto left = LayoutText(*font, "IW4IRIS", st);
    CHECK(left.bounds.min.x > -1.0);
    st.kerning = false;
    const auto noKern = LayoutText(*font, "AVAV", st);
    st.kerning = true;
    const auto kern = LayoutText(*font, "AVAV", st);
    CHECK(kern.glyphs.back().origin.x <= noKern.glyphs.back().origin.x + 1e-9);
    const auto two = LayoutText(*font, "TOP\nBOTTOM", st);
    CHECK(two.lineCount == 2);
    CHECK(two.glyphs.back().line == 1);
    CHECK(two.glyphs.back().origin.y < two.glyphs.front().origin.y);
    st.letterSpacing = 10;
    const auto spaced = LayoutText(*font, "AB", st);
    CHECK(spaced.glyphs[1].origin.x > kern.glyphs[1].origin.x + 9.0);
    st = TextSettings{};
    st.layout = TextLayoutKind::Circle;
    st.radius = 100;
    const auto arc = LayoutText(*font, "CIRCLE", st);
    CHECK(arc.glyphs.size() == 6);
    CHECK(arc.glyphs.front().rotation > 0.0);
    CHECK(arc.glyphs.back().rotation < 0.0);
    st.layout = TextLayoutKind::Vertical;
    const auto vert = LayoutText(*font, "ABC", st);
    CHECK(vert.glyphs[2].origin.y < vert.glyphs[0].origin.y);
    const auto missing = LayoutText(*font, "A\xF0\x9F\x98\x80", TextSettings{});
    CHECK(missing.glyphs.size() == 1);
    CHECK(!missing.warnings.empty());
    const auto words = LayoutText(*font, "MW2 IW4X", TextSettings{});
    CHECK(words.glyphs.front().word != words.glyphs.back().word);
}

TEST_CASE("font: every glyph of a title extrudes into a closed solid")
{
    const auto font = Font::Load(FontPath());
    if (!font)
        return;
    TextSettings st;
    st.size = 40;
    st.tolerance = 0.2;
    const auto layout = LayoutText(*font, "oG ABDOR 8%", st);
    ExtrudeSettings es;
    es.depth = 6;
    es.bevel = 0.5;
    es.bevelSegments = 2;
    for (const auto& g : layout.glyphs)
    {
        const auto r = ExtrudeShapes(g.shapes, es);
        std::string problem;
        CHECK_MSG(ValidateMesh(r.mesh, &problem), problem);
        CHECK_MSG(IsWatertight(r.mesh), static_cast<unsigned>(g.codepoint));
        CHECK(SignedVolume(r.mesh) > 0);
    }
}

TEST_CASE("logo: SVG paths become shapes with holes and colours, scaled to width")
{
    const char* svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 200 100">
      <path id="ring" fill="#ff0000" fill-rule="evenodd" d="M0 0 H100 V100 H0 Z M25 25 H75 V75 H25 Z"/>
      <circle id="dot" fill="#0000ff" cx="150" cy="50" r="40"/>
      <path fill="none" stroke="#000" d="M0 0 L10 10"/>
    </svg>)";
    SvgImportSettings st;
    st.width = 50;
    const auto logo = ImportSvg(svg, st);
    CHECK_MSG(logo, logo.error);
    CHECK(logo.parts.size() == 2);
    CHECK(logo.parts[0].name == "ring");
    CHECK_NEAR(logo.parts[0].color.x, 1.0, 1e-6);
    CHECK(logo.parts[0].shapes.size() == 1 && logo.parts[0].shapes[0].holes.size() == 1);
    CHECK_NEAR(logo.parts[1].color.z, 1.0, 1e-6);
    CHECK_NEAR(logo.bounds.Size().x, 50.0, 0.05);       // proportions kept
    CHECK_NEAR(logo.bounds.Size().y, 50.0 * 100 / 190, 0.2);
    CHECK_NEAR(logo.bounds.Center().x, 0.0, 1e-6);
    CHECK(!logo.warnings.empty());  // the stroke-only path
    // Upright: the circle (right in the SVG) stays on the right.
    CHECK(logo.parts[1].bounds.Center().x > logo.parts[0].bounds.Center().x);
    const auto bad = ImportSvg("<svg xmlns=\"http://www.w3.org/2000/svg\"><path fill=\"none\" stroke=\"#000\" d=\"M0 0 L1 1\"/></svg>", st);
    CHECK(!bad);
    const auto file = ImportSvgFile(TempFile("iw4iris-test.svg", svg), st);
    CHECK(file && file.parts.size() == 2);
}

TEST_CASE("logo: transparent images trace into silhouettes with holes")
{
    // A ring: alpha 1 between radius 10 and 20 around (32, 32), soft edges.
    auto ring = [](int x, int y) {
        const double r = std::hypot(x + 0.5 - 32.0, y + 0.5 - 32.0);
        return Clamp(std::min(r - 10.0, 20.0 - r) + 0.5, 0.0, 1.0);
    };
    std::string error;
    const auto img = DecodeImage(MakeTga(64, 64, ring), &error);
    CHECK_MSG(img.has_value(), error);
    if (!img)
        return;
    CHECK(img->width == 64 && img->height == 64);
    const auto contours = TraceAlphaContours(*img, 128);
    CHECK(contours.size() == 2);
    TraceSettings st;
    st.width = 64;  // one unit per pixel
    st.smoothing = 0;
    st.simplify = 0.2;
    const auto logo = TraceImage(*img, st);
    CHECK_MSG(logo, logo.error);
    CHECK(logo.parts.size() == 1);
    CHECK(logo.parts[0].shapes.size() == 1 && logo.parts[0].shapes[0].holes.size() == 1);
    const auto& s = logo.parts[0].shapes[0];
    const double area = SignedArea(s.outer) + SignedArea(s.holes[0]);
    CHECK_NEAR(area / (kPi * (400.0 - 100.0)), 1.0, 0.04);
    CHECK_NEAR(logo.parts[0].color.x, 200.0 / 255.0, 0.01);
    ExtrudeSettings es;
    es.depth = 3;
    es.bevel = 0.3;
    const auto mesh = ExtrudeShapes(logo.parts[0].shapes, es).mesh;
    CHECK(IsWatertight(mesh));
    // Fully opaque images warn that the outline is the rectangle.
    const auto solid = DecodeImage(MakeTga(8, 8, [](int, int) { return 1.0; }));
    CHECK(!TraceImage(*solid, TraceSettings{}).warnings.empty());
    // Two separate blobs.
    const auto blobs = DecodeImage(MakeTga(40, 20, [](int x, int y) {
        return (std::hypot(x - 10.0, y - 10.0) < 6 || std::hypot(x - 30.0, y - 10.0) < 6) ? 1.0 : 0.0;
    }));
    CHECK(TraceImage(*blobs, TraceSettings{}).parts[0].shapes.size() == 2);
}

TEST_CASE("logo: unsupported or broken images give readable errors")
{
    std::string error;
    std::vector<std::uint8_t> webp{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 0, 0, 0, 0};
    CHECK(!DecodeImage(webp, &error));
    CHECK(error.find("WebP") != std::string::npos);
    CHECK(!DecodeImage(std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, &error));
    CHECK(!LoadImageFile("/nonexistent/logo.png", &error));
}

TEST_CASE("model: OBJ import triangulates, keeps normals, groups and colours")
{
    const char* obj = R"(# cube
mtllib cube.mtl
v -1 -1 -1
v 1 -1 -1
v 1 1 -1
v -1 1 -1
v -1 -1 1
v 1 -1 1
v 1 1 1
v -1 1 1
vt 0 0
vt 1 0
vt 1 1
usemtl red
f 1 4 3 2
f 5 6 7 8
f 1 2 6 5
usemtl blue
f 2 3 7 6
f 3 4 8 7
f -8 -4 -1 -5
)";
    const char* mtl = "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n";
    ModelImportSettings st;
    st.scale = 10;
    const auto m = ImportObj(obj, st, mtl);
    CHECK_MSG(m, m.error);
    CHECK(m.mesh.TriangleCount() == 12);
    CHECK(m.normalsComputed);
    CHECK(m.groups.size() == 2);
    CHECK_NEAR(m.groups[0].color.x, 1.0, 0);
    CHECK_NEAR(m.groups[1].color.z, 1.0, 0);
    CHECK(ValidateMesh(m.mesh));
    CHECK(IsWatertight(m.mesh));
    CHECK_NEAR(m.bounds.Size().x, 20.0, 1e-5);
    CHECK_NEAR(m.bounds.Center().x, 0.0, 1e-5);
    // Z up converts to y up.
    ModelImportSettings z;
    z.upAxis = ModelUpAxis::Z;
    z.centre = false;
    const auto tall = ImportObj("v 0 0 0\nv 1 0 0\nv 0 0 5\nf 1 2 3\n", z);
    CHECK_NEAR(tall.bounds.max.y, 5.0, 1e-6);
    // Limits and invalid input.
    ModelImportSettings tiny;
    tiny.maxTriangles = 3;
    CHECK(!ImportObj(obj, tiny));
    CHECK(!ImportObj("v 0 0 0\n", st));
    const auto bad = ImportObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 1 2 9\n", st);
    CHECK(bad && !bad.warnings.empty());
}
