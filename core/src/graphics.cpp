// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/graphics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>

namespace iris
{
    // ---------------------------------------------------------------- enums

    std::string_view ToString(GraphicKind kind)
    {
        switch (kind)
        {
            case GraphicKind::Text: return "text";
            case GraphicKind::Logo: return "logo";
            case GraphicKind::Image: return "image";
            case GraphicKind::Shape: return "shape";
            case GraphicKind::Model: return "model";
            case GraphicKind::Anchor: return "anchor";
        }
        return "text";
    }

    std::optional<GraphicKind> GraphicKindFromString(std::string_view s)
    {
        for (auto k : {GraphicKind::Text, GraphicKind::Logo, GraphicKind::Image, GraphicKind::Shape, GraphicKind::Model,
                       GraphicKind::Anchor})
            if (ToString(k) == s)
                return k;
        return std::nullopt;
    }

    std::string_view ToString(ShapeKind kind)
    {
        switch (kind)
        {
            case ShapeKind::Box: return "box";
            case ShapeKind::Cylinder: return "cylinder";
            case ShapeKind::Sphere: return "sphere";
            case ShapeKind::Plane: return "plane";
            case ShapeKind::Ring: return "ring";
        }
        return "box";
    }

    std::optional<ShapeKind> ShapeKindFromString(std::string_view s)
    {
        for (auto k : {ShapeKind::Box, ShapeKind::Cylinder, ShapeKind::Sphere, ShapeKind::Plane, ShapeKind::Ring})
            if (ToString(k) == s)
                return k;
        return std::nullopt;
    }

    std::string_view ToString(GraphicRenderMode mode)
    {
        switch (mode)
        {
            case GraphicRenderMode::Depth: return "depth";
            case GraphicRenderMode::OnTop: return "onTop";
            case GraphicRenderMode::XRay: return "xray";
        }
        return "depth";
    }

    std::optional<GraphicRenderMode> GraphicRenderModeFromString(std::string_view s)
    {
        for (auto m : {GraphicRenderMode::Depth, GraphicRenderMode::OnTop, GraphicRenderMode::XRay})
            if (ToString(m) == s)
                return m;
        return std::nullopt;
    }

    std::string_view ToString(TrackingMode mode)
    {
        switch (mode)
        {
            case TrackingMode::World: return "world";
            case TrackingMode::Actor: return "actor";
            case TrackingMode::Billboard: return "billboard";
            case TrackingMode::Camera: return "camera";
        }
        return "world";
    }

    std::optional<TrackingMode> TrackingModeFromString(std::string_view s)
    {
        for (auto m : {TrackingMode::World, TrackingMode::Actor, TrackingMode::Billboard, TrackingMode::Camera})
            if (ToString(m) == s)
                return m;
        return std::nullopt;
    }

    // ---------------------------------------------------------------- materials

    const MaterialDef& MaterialSlots::For(MeshPart part) const
    {
        if (!separate)
            return front;
        switch (part)
        {
            case MeshPart::Front: return front;
            case MeshPart::Back: return back;
            case MeshPart::Side: return side;
            case MeshPart::Bevel: return bevel;
        }
        return front;
    }

    const std::vector<MaterialPreset>& MaterialPresets()
    {
        auto m = [](const char* id, Vec3 color, double specular, double gloss, double metallic, double opacity = 1.0,
                    Vec3 emission = {}, double emissionStrength = 0.0) {
            MaterialDef d;
            d.preset = id;
            d.color = color;
            d.specular = specular;
            d.gloss = gloss;
            d.metallic = metallic;
            d.opacity = opacity;
            d.emission = emission;
            d.emissionStrength = emissionStrength;
            return d;
        };
        static const std::vector<MaterialPreset> presets{
            {"matte_black", "Matte black", m("matte_black", {0.06, 0.06, 0.065}, 0.05, 6, 0)},
            {"matte_white", "Matte white", m("matte_white", {0.9, 0.9, 0.9}, 0.08, 8, 0)},
            {"brushed_metal", "Brushed metal", m("brushed_metal", {0.62, 0.63, 0.65}, 0.55, 20, 0.8)},
            {"polished_metal", "Polished metal", m("polished_metal", {0.75, 0.76, 0.78}, 0.85, 60, 0.9)},
            {"chrome", "Chrome", m("chrome", {0.55, 0.57, 0.6}, 1.0, 110, 1.0)},
            {"gold", "Gold", m("gold", {0.86, 0.66, 0.24}, 0.9, 60, 1.0)},
            {"silver", "Silver", m("silver", {0.8, 0.81, 0.83}, 0.9, 70, 1.0)},
            {"painted_steel", "Painted steel", m("painted_steel", {0.18, 0.24, 0.32}, 0.35, 28, 0.2)},
            {"concrete", "Concrete", m("concrete", {0.52, 0.51, 0.49}, 0.04, 4, 0)},
            {"stone", "Stone", m("stone", {0.42, 0.4, 0.37}, 0.06, 6, 0)},
            {"glass", "Glass", m("glass", {0.75, 0.85, 0.9}, 0.9, 90, 0.1, 0.35)},
            {"plastic_clear", "Transparent plastic", m("plastic_clear", {0.9, 0.92, 0.95}, 0.6, 40, 0.0, 0.5)},
            {"plastic_glossy", "Glossy plastic", m("plastic_glossy", {0.85, 0.12, 0.1}, 0.6, 48, 0.0)},
            {"carbon", "Carbon fibre", m("carbon", {0.08, 0.08, 0.09}, 0.5, 40, 0.3)},
            {"neon", "Emissive neon", m("neon", {0.2, 0.9, 1.0}, 0.2, 20, 0, 1.0, {0.2, 0.9, 1.0}, 1.0)},
            {"holographic", "Holographic", m("holographic", {0.55, 0.75, 1.0}, 0.7, 50, 0.2, 0.6, {0.3, 0.5, 1.0}, 0.4)},
            {"camo", "Military green", m("camo", {0.28, 0.32, 0.2}, 0.08, 8, 0)},
            {"weathered_metal", "Weathered metal", m("weathered_metal", {0.45, 0.43, 0.4}, 0.3, 14, 0.5)},
            {"rusted_metal", "Rusted metal", m("rusted_metal", {0.45, 0.24, 0.12}, 0.15, 8, 0.3)},
        };
        return presets;
    }

    MaterialDef MakeMaterial(std::string_view presetId)
    {
        for (const auto& p : MaterialPresets())
            if (p.id == presetId)
                return p.material;
        return MaterialDef{};
    }

    // ---------------------------------------------------------------- easing

    const std::vector<std::string>& EasingNames()
    {
        static const std::vector<std::string> names{"linear",      "easeInCubic", "easeOutCubic", "easeInOutCubic",
                                                    "easeOutBack", "easeOutBounce", "easeOutElastic", "hold"};
        return names;
    }

    double EaseValue(std::string_view e, double t)
    {
        t = Clamp(t, 0.0, 1.0);
        if (e == "linear")
            return t;
        if (e == "easeInCubic")
            return t * t * t;
        if (e == "easeInOutCubic")
            return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
        if (e == "easeOutBack")
        {
            const double c1 = 1.70158, c3 = c1 + 1;
            return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
        }
        if (e == "easeOutBounce")
        {
            const double n = 7.5625, d = 2.75;
            if (t < 1 / d)
                return n * t * t;
            if (t < 2 / d)
            {
                t -= 1.5 / d;
                return n * t * t + 0.75;
            }
            if (t < 2.5 / d)
            {
                t -= 2.25 / d;
                return n * t * t + 0.9375;
            }
            t -= 2.625 / d;
            return n * t * t + 0.984375;
        }
        if (e == "easeOutElastic")
        {
            if (t == 0 || t == 1)
                return t;
            return std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * kPi / 3)) + 1;
        }
        if (e == "hold")
            return t >= 1.0 ? 1.0 : 0.0;
        return 1 - std::pow(1 - t, 3);  // easeOutCubic (default)
    }

    const std::vector<AnimationPresetInfo>& AnimationPresets()
    {
        static const std::vector<AnimationPresetInfo> presets{
            {"fadeIn", "Fade in", true, false},
            {"fadeOut", "Fade out", true, false},
            {"flyIn", "Fly in", true, false},
            {"flyOut", "Fly out", true, false},
            {"rise", "Rise into place", true, false},
            {"drop", "Drop into place", true, false},
            {"scaleIn", "Scale in", true, false},
            {"popIn", "Pop in (overshoot)", true, false},
            {"spinIn", "Spin in", true, false},
            {"typewriter", "Typewriter", true, false},
            {"assemble", "Assemble from pieces", true, false},
            {"disassemble", "Break into pieces", true, false},
            {"extrudeGrow", "Extrusion growth", true, false},
            {"flicker", "Flicker", true, false},
            {"glitch", "Glitch", true, false},
            {"float", "Float", true, true},
            {"rotate", "Rotate (whole graphic)", false, true},
            {"pulse", "Pulse", true, true},
        };
        return presets;
    }

    namespace
    {
        // SplitMix-style integer hash to [0, 1).
        double Hash01(std::uint32_t seed, std::uint32_t a, std::uint32_t b)
        {
            std::uint64_t x = (std::uint64_t(seed) << 40) ^ (std::uint64_t(a) << 20) ^ b;
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            x ^= x >> 31;
            return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
        }

        Vec3 HashDirection(std::uint32_t seed, std::uint32_t i)
        {
            const double u = Hash01(seed, i, 1) * 2 - 1, v = Hash01(seed, i, 2) * 2 * kPi;
            const double r = std::sqrt(std::max(0.0, 1 - u * u));
            return {r * std::cos(v), r * std::sin(v), u};
        }

        bool SameCurve(const ScalarCurve& a, const ScalarCurve& b)
        {
            return a.Keys() == b.Keys() && a.DefaultValue() == b.DefaultValue();
        }
    }  // namespace

    bool GraphicDef::operator==(const GraphicDef& o) const
    {
        return id == o.id && name == o.name && kind == o.kind && text == o.text && font == o.font &&
               textSettings.size == o.textSettings.size && textSettings.letterSpacing == o.textSettings.letterSpacing &&
               textSettings.wordSpacing == o.textSettings.wordSpacing &&
               textSettings.lineSpacing == o.textSettings.lineSpacing && textSettings.align == o.textSettings.align &&
               textSettings.verticalAlign == o.textSettings.verticalAlign && textSettings.layout == o.textSettings.layout &&
               textSettings.radius == o.textSettings.radius && textSettings.kerning == o.textSettings.kerning &&
               separateWords == o.separateWords && source == o.source && logoMode == o.logoMode && width == o.width &&
               alphaThreshold == o.alphaThreshold && simplify == o.simplify && smoothing == o.smoothing &&
               useLogoColors == o.useLogoColors && modelScale == o.modelScale && modelUp == o.modelUp &&
               shape == o.shape && shapeSize == o.shapeSize && geometry == o.geometry && materials == o.materials &&
               doubleSided == o.doubleSided && transform == o.transform && track == o.track &&
               SameCurve(opacity, o.opacity) && tracking == o.tracking && renderMode == o.renderMode &&
               animations == o.animations && visible == o.visible && locked == o.locked && castShadow == o.castShadow &&
               group == o.group && parent == o.parent;
    }

    // ---------------------------------------------------------------- geometry

    std::size_t GraphicGeometry::TriangleCount() const
    {
        std::size_t n = 0;
        for (const auto& e : elements)
            n += e.mesh.TriangleCount();
        return n;
    }

    std::string GeometryKey(const GraphicDef& g)
    {
        std::ostringstream k;
        k.precision(9);
        k << ToString(g.kind) << '|';
        switch (g.kind)
        {
            case GraphicKind::Text:
            {
                const auto& t = g.textSettings;
                k << g.text << '|' << g.font << '|' << t.size << ',' << t.letterSpacing << ',' << t.wordSpacing << ','
                  << t.lineSpacing << ',' << int(t.align) << ',' << int(t.verticalAlign) << ',' << int(t.layout) << ','
                  << t.radius << ',' << t.kerning << ',' << g.separateWords;
                break;
            }
            case GraphicKind::Logo:
                k << g.source << '|' << int(g.logoMode) << ',' << g.width << ',' << g.alphaThreshold << ',' << g.simplify
                  << ',' << g.smoothing;
                break;
            case GraphicKind::Image: k << g.source << '|' << g.width; break;
            case GraphicKind::Shape: k << ToString(g.shape) << ',' << g.shapeSize.x << ',' << g.shapeSize.y << ',' << g.shapeSize.z; break;
            case GraphicKind::Model: k << g.source << '|' << g.modelScale << ',' << int(g.modelUp); break;
            case GraphicKind::Anchor: break;
        }
        const auto& geo = g.geometry;
        k << '|' << geo.depth << ',' << geo.bevel << ',' << geo.bevelSegments << ',' << geo.detail;
        return k.str();
    }

    namespace
    {
        GraphicElement Element(Mesh mesh, const Vec3& pivot, std::size_t word = 0, std::size_t line = 0)
        {
            GraphicElement e;
            e.mesh = std::move(mesh);
            e.pivot = pivot;
            e.word = word;
            e.line = line;
            return e;
        }

        void RotateMeshZ(Mesh& mesh, double radians)
        {
            if (std::abs(radians) < 1e-12)
                return;
            const double c = std::cos(radians), s = std::sin(radians);
            for (auto& v : mesh.vertices)
            {
                const double x = v.px, y = v.py, nx = v.nx, ny = v.ny;
                v.px = static_cast<float>(x * c - y * s);
                v.py = static_cast<float>(x * s + y * c);
                v.nx = static_cast<float>(nx * c - ny * s);
                v.ny = static_cast<float>(nx * s + ny * c);
            }
        }

        std::vector<Shape2D> Offset(std::vector<Shape2D> shapes, const Vec2& d)
        {
            for (auto& s : shapes)
            {
                for (auto& p : s.outer)
                    p = p - d;
                for (auto& h : s.holes)
                    for (auto& p : h)
                        p = p - d;
            }
            return shapes;
        }

        ExtrudeSettings ExtrudeFrom(const GeometrySettings& g)
        {
            ExtrudeSettings es;
            es.depth = std::max(g.depth, 0.01);
            es.bevel = std::max(g.bevel, 0.0);
            es.bevelSegments = std::clamp(g.bevelSegments, 1, 8);
            return es;
        }

        void AddWarnings(std::vector<std::string>& to, const std::vector<std::string>& from)
        {
            for (const auto& w : from)
                if (std::find(to.begin(), to.end(), w) == to.end())
                    to.push_back(w);
        }

        std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& base)
        {
            std::filesystem::path p(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
            return p.is_relative() && !base.empty() ? base / p : p;
        }

        bool EndsWithI(const std::string& s, const char* suffix)
        {
            const std::string suf(suffix);
            if (s.size() < suf.size())
                return false;
            for (std::size_t i = 0; i < suf.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(s[s.size() - suf.size() + i])) != suf[i])
                    return false;
            return true;
        }
    }  // namespace

    GraphicGeometry BuildGraphicGeometry(const GraphicDef& g, const FontResolver& fonts, const std::filesystem::path& base)
    {
        GraphicGeometry out;
        const ExtrudeSettings es = ExtrudeFrom(g.geometry);
        switch (g.kind)
        {
            case GraphicKind::Text:
            {
                std::string error;
                const auto font = fonts ? fonts(g.font, &error) : nullptr;
                if (!font)
                {
                    out.error = error.empty() ? "No font is selected." : error;
                    return out;
                }
                TextSettings ts = g.textSettings;
                ts.tolerance = std::clamp(g.geometry.detail, 0.005, 5.0);
                const TextLayout layout = LayoutText(*font, g.text, ts);
                AddWarnings(out.warnings, layout.warnings);
                std::map<std::size_t, std::size_t> wordElement;
                for (const auto& glyph : layout.glyphs)
                {
                    if (glyph.shapes.empty())
                        continue;
                    const Vec2 c = glyph.bounds.Center();
                    auto result = ExtrudeShapes(Offset(glyph.shapes, c), es);
                    AddWarnings(out.warnings, result.warnings);
                    RotateMeshZ(result.mesh, glyph.rotation);
                    const Vec2 rc{c.x * std::cos(glyph.rotation) - c.y * std::sin(glyph.rotation),
                                  c.x * std::sin(glyph.rotation) + c.y * std::cos(glyph.rotation)};
                    const Vec3 pivot{glyph.origin.x + rc.x, glyph.origin.y + rc.y, 0.0};
                    if (g.separateWords)
                    {
                        const auto it = wordElement.find(glyph.word);
                        if (it == wordElement.end())
                        {
                            wordElement[glyph.word] = out.elements.size();
                            out.elements.push_back(Element(std::move(result.mesh), pivot, glyph.word, glyph.line));
                        }
                        else
                        {
                            auto& e = out.elements[it->second];
                            result.mesh.Translate(pivot - e.pivot);
                            e.mesh.Append(result.mesh);
                        }
                    }
                    else
                        out.elements.push_back(Element(std::move(result.mesh), pivot, glyph.word, glyph.line));
                }
                if (out.elements.empty())
                    out.error = g.text.empty() ? "The text is empty." : "None of the characters has an outline in this font.";
                break;
            }
            case GraphicKind::Logo:
            {
                const auto path = Resolve(g.source, base);
                LogoImport logo;
                if (EndsWithI(g.source, ".svg"))
                {
                    SvgImportSettings st;
                    st.width = g.width;
                    st.tolerance = std::clamp(g.geometry.detail, 0.005, 5.0);
                    logo = ImportSvgFile(path, st);
                }
                else
                {
                    std::string error;
                    const auto image = LoadImageFile(path, &error);
                    if (!image)
                    {
                        out.error = error;
                        return out;
                    }
                    TraceSettings st;
                    st.width = g.width;
                    st.alphaThreshold = g.alphaThreshold;
                    st.simplify = g.simplify;
                    st.smoothing = g.smoothing;
                    logo = TraceImage(*image, st);
                }
                AddWarnings(out.warnings, logo.warnings);
                if (!logo)
                {
                    out.error = logo.error.empty() ? "The logo has no shapes." : logo.error;
                    return out;
                }
                for (const auto& part : logo.parts)
                {
                    const Vec2 c = part.bounds.Center();
                    GraphicElement e;
                    if (g.logoMode == LogoMode::Flat)
                        e.mesh = FlatShapes(Offset(part.shapes, c));
                    else
                    {
                        auto result = ExtrudeShapes(Offset(part.shapes, c), es);
                        AddWarnings(out.warnings, result.warnings);
                        e.mesh = std::move(result.mesh);
                    }
                    e.pivot = {c.x, c.y, 0.0};
                    e.color = part.color;
                    e.hasColor = true;
                    e.name = part.name;
                    out.elements.push_back(std::move(e));
                }
                break;
            }
            case GraphicKind::Image:
            {
                std::string error;
                auto image = LoadImageFile(Resolve(g.source, base), &error);
                if (!image)
                {
                    out.error = error;
                    return out;
                }
                const double w = std::max(g.width, 0.1);
                const double h = w * image->height / std::max(image->width, 1);
                out.elements.push_back(Element(MakePlane(w, h), {}));
                out.texture = std::make_shared<Image>(std::move(*image));
                break;
            }
            case GraphicKind::Shape:
            {
                const Vec3 s{std::max(g.shapeSize.x, 0.01), std::max(g.shapeSize.y, 0.01), std::max(g.shapeSize.z, 0.01)};
                Mesh m;
                switch (g.shape)
                {
                    case ShapeKind::Box: m = MakeBox({s.x, s.y, s.z}); break;
                    case ShapeKind::Cylinder: m = MakeCylinder(s.x * 0.5, s.y, 48); break;
                    case ShapeKind::Sphere: m = MakeSphere(s.x * 0.5, 32, 20); break;
                    case ShapeKind::Plane: m = MakePlane(s.x, s.y); break;
                    case ShapeKind::Ring: m = MakeRing(s.x * 0.5, s.x * 0.5 * 0.72, s.z, 64); break;
                }
                out.elements.push_back(Element(std::move(m), {}));
                break;
            }
            case GraphicKind::Model:
            {
                ModelImportSettings st;
                st.scale = g.modelScale;
                st.upAxis = g.modelUp;
                auto model = ImportObjFile(Resolve(g.source, base), st);
                AddWarnings(out.warnings, model.warnings);
                if (!model)
                {
                    out.error = model.error;
                    return out;
                }
                // One element per material group, so groups keep their colours.
                for (const auto& group : model.groups)
                {
                    GraphicElement e;
                    std::map<std::uint32_t, std::uint32_t> remap;
                    for (std::uint32_t i = group.firstIndex; i < group.firstIndex + group.indexCount; ++i)
                    {
                        const std::uint32_t src = model.mesh.indices[i];
                        auto it = remap.find(src);
                        if (it == remap.end())
                        {
                            it = remap.emplace(src, static_cast<std::uint32_t>(e.mesh.vertices.size())).first;
                            e.mesh.vertices.push_back(model.mesh.vertices[src]);
                        }
                        e.mesh.indices.push_back(it->second);
                    }
                    e.mesh.submeshes.push_back({MeshPart::Front, 0, static_cast<std::uint32_t>(e.mesh.indices.size())});
                    e.color = group.color;
                    e.hasColor = true;
                    e.name = group.material;
                    out.elements.push_back(std::move(e));
                }
                break;
            }
            case GraphicKind::Anchor: break;
        }
        for (const auto& e : out.elements)
        {
            Bounds3D b = e.mesh.Bounds();
            if (b.Valid())
                out.bounds.Add(Bounds3D{b.min + e.pivot, b.max + e.pivot});
        }
        if (!out.bounds.Valid())
            out.bounds = {{-4, -4, -4}, {4, 4, 4}};
        if (out.TriangleCount() > 400000)
        {
            out.elements.clear();
            out.error = "The graphic has more than 400,000 triangles; lower the detail or simplify the artwork.";
        }
        return out;
    }

    // ---------------------------------------------------------------- pose

    Vec3 RotateGraphicSpace(const Vec3& v, const Vec3& r)
    {
        Vec3 p = v;
        if (r.x != 0.0)
        {
            const double a = r.x * kDegToRad, c = std::cos(a), s = std::sin(a);
            p = {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
        }
        if (r.y != 0.0)
        {
            const double a = r.y * kDegToRad, c = std::cos(a), s = std::sin(a);
            p = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
        }
        if (r.z != 0.0)
        {
            const double a = r.z * kDegToRad, c = std::cos(a), s = std::sin(a);
            p = {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
        }
        return p;
    }

    namespace
    {
        EulerAngles Compose(const EulerAngles& parent, const EulerAngles& child)
        {
            return AxisToAngles(AnglesToAxis(parent) * AnglesToAxis(child));
        }

        void ApplyAnimation(const GraphicAnimation& a, double t, std::size_t count, GraphicPose& pose)
        {
            if (!a.enabled || a.duration <= 0.0)
                return;
            const double dur = a.duration;
            const double I = a.intensity;
            const Vec3 dir = Length(a.direction) > 1e-9 ? Normalize(a.direction) : Vec3{0, -1, 0};
            if (a.preset == "rotate")
            {
                if (t > a.start)
                    pose.world.angles.yaw += 360.0 * I * (t - a.start) / dur;
                return;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                ElementPose& e = pose.elements[i];
                const double delay = static_cast<double>(i) * a.stagger;
                const double u = (t - a.start - delay) / dur;
                const double p = EaseValue(a.easing, u);
                const double lin = Clamp(u, 0.0, 1.0);
                const auto ui = static_cast<std::uint32_t>(i);
                if (a.preset == "fadeIn")
                    e.opacity *= p;
                else if (a.preset == "fadeOut")
                    e.opacity *= 1.0 - p;
                else if (a.preset == "flyIn")
                {
                    e.offset += dir * (-120.0 * I * (1.0 - p));
                    e.opacity *= Clamp(lin * 3.0, 0.0, 1.0);
                }
                else if (a.preset == "flyOut")
                {
                    e.offset += dir * (120.0 * I * p);
                    e.opacity *= 1.0 - Clamp((lin - 0.66) * 3.0, 0.0, 1.0);
                }
                else if (a.preset == "rise")
                {
                    e.offset.y -= 30.0 * I * (1.0 - p);
                    e.opacity *= Clamp(lin * 2.0, 0.0, 1.0);
                }
                else if (a.preset == "drop")
                {
                    e.offset.y += 60.0 * I * (1.0 - p);
                    e.opacity *= Clamp(lin * 4.0, 0.0, 1.0);
                }
                else if (a.preset == "scaleIn")
                    e.scale *= std::max(p, 0.0);
                else if (a.preset == "popIn")
                    e.scale *= std::max(EaseValue("easeOutBack", u), 0.0);
                else if (a.preset == "spinIn")
                {
                    e.rotationDeg.y += 360.0 * I * (1.0 - p);
                    e.opacity *= Clamp(lin * 2.0, 0.0, 1.0);
                }
                else if (a.preset == "typewriter")
                    e.opacity *= t >= a.start + delay ? 1.0 : 0.0;
                else if (a.preset == "assemble" || a.preset == "disassemble")
                {
                    const double k = a.preset == "assemble" ? 1.0 - p : p;
                    e.offset += HashDirection(a.seed, ui) * (160.0 * I * k);
                    e.rotationDeg += Vec3{Hash01(a.seed, ui, 3) - 0.5, Hash01(a.seed, ui, 4) - 0.5,
                                          Hash01(a.seed, ui, 5) - 0.5} *
                                     (540.0 * I * k);
                    e.opacity *= a.preset == "assemble" ? Clamp(lin * 2.0, 0.0, 1.0) : 1.0 - Clamp((lin - 0.5) * 2.0, 0.0, 1.0);
                }
                else if (a.preset == "extrudeGrow")
                    e.depthScale *= std::max(p, 0.001);
                else if (a.preset == "flicker")
                {
                    if (u >= 0.0 && u <= 1.0)
                    {
                        const auto step = static_cast<std::uint32_t>(std::floor((t - a.start) * 24.0));
                        if (Hash01(a.seed, ui, step) < 0.45 * I)
                            e.opacity *= 0.1;
                    }
                }
                else if (a.preset == "glitch")
                {
                    if (u >= 0.0 && u <= 1.0)
                    {
                        const auto step = static_cast<std::uint32_t>(std::floor((t - a.start) * 24.0));
                        if (Hash01(a.seed, ui, step * 2 + 7) > 1.0 - 0.35 * I)
                            e.offset.x += (Hash01(a.seed, ui, step * 2 + 8) - 0.5) * 12.0 * I;
                    }
                }
                else if (a.preset == "float")
                {
                    if (t > a.start)
                        e.offset.y += std::sin(2.0 * kPi * (t - a.start) / dur + static_cast<double>(i) * 0.6) * 3.0 * I;
                }
                else if (a.preset == "pulse")
                {
                    if (t > a.start)
                    {
                        const double s = 0.5 + 0.5 * std::sin(2.0 * kPi * (t - a.start - delay) / dur);
                        e.scale *= 1.0 + 0.08 * I * s;
                        e.emissionBoost += 0.6 * I * s;
                    }
                }
            }
        }
    }  // namespace

    GraphicPose EvaluateGraphic(const GraphicDef& g, double t, std::size_t elementCount, const TrackingContext& ctx)
    {
        GraphicPose pose;
        Transform base = g.track.Evaluate(t, g.transform);
        pose.opacity = Clamp(g.opacity.Empty() ? g.opacity.DefaultValue() : g.opacity.Evaluate(t), 0.0, 1.0);

        switch (g.tracking.mode)
        {
            case TrackingMode::World: pose.world = base; break;
            case TrackingMode::Actor:
            {
                const auto ap = ctx.attachPoint ? ctx.attachPoint(g.tracking.actor, g.tracking.attach) : std::nullopt;
                if (!ap)
                {
                    pose.world = base;
                    pose.visible = false;  // the actor is not in the scene
                    break;
                }
                pose.world.scale = base.scale;
                if (g.tracking.inheritRotation)
                {
                    pose.world.position = ap->position + AnglesToAxis(ap->angles) * base.position;
                    pose.world.angles = Compose(ap->angles, base.angles);
                }
                else
                {
                    pose.world.position = ap->position + base.position;
                    pose.world.angles = base.angles;
                }
                break;
            }
            case TrackingMode::Billboard:
            {
                pose.world = base;
                if (ctx.camera)
                {
                    const Vec3 d = ctx.camera->position - base.position;
                    if (g.tracking.yawOnly)
                        pose.world.angles = {0.0, std::atan2(d.y, d.x) * kRadToDeg, base.angles.roll};
                    else
                        pose.world.angles = LookAt(base.position, ctx.camera->position, base.angles.roll);
                }
                break;
            }
            case TrackingMode::Camera:
            {
                pose.world = base;
                if (ctx.camera)
                {
                    const Mat3 c = AnglesToAxis(ctx.camera->angles);
                    pose.world.position = ctx.camera->position + c.col[0] * (g.tracking.distance + base.position.x) +
                                          c.col[1] * base.position.y + c.col[2] * base.position.z;
                    Mat3 facing;
                    facing.col = {-c.col[0], -c.col[1], c.col[2]};
                    pose.world.angles = Compose(AxisToAngles(facing), base.angles);
                }
                break;
            }
        }
        if (!g.parent.empty() && ctx.graphicPose)
            if (const auto parent = ctx.graphicPose(g.parent))
            {
                const Mat3 pa = AnglesToAxis(parent->angles);
                const Vec3 local{pose.world.position.x * parent->scale.x, pose.world.position.y * parent->scale.y,
                                 pose.world.position.z * parent->scale.z};
                pose.world.position = parent->position + pa * local;
                pose.world.angles = Compose(parent->angles, pose.world.angles);
                pose.world.scale = {pose.world.scale.x * parent->scale.x, pose.world.scale.y * parent->scale.y,
                                    pose.world.scale.z * parent->scale.z};
            }

        pose.elements.assign(elementCount, ElementPose{});
        for (const auto& a : g.animations)
            ApplyAnimation(a, t, elementCount, pose);
        pose.visible = pose.visible && g.visible && pose.opacity > 0.0;
        return pose;
    }
}  // namespace iris
