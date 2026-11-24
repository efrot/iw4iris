// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/logo.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

#include "vendor/nanosvg_impl.hpp"
#include "vendor/stb_image_impl.hpp"

namespace iris
{
    namespace
    {
        std::vector<std::uint8_t> ReadAll(const std::filesystem::path& file, std::string* error)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                if (error)
                    *error = "Could not open the file.";
                return {};
            }
            return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        }

        Vec3 ColorFromAbgr(unsigned int c)
        {
            return {(c & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, ((c >> 16) & 0xFF) / 255.0};
        }

        void FinishImport(LogoImport& logo, bool centre)
        {
            Bounds2D all;
            for (auto& part : logo.parts)
            {
                part.bounds = {};
                for (const auto& s : part.shapes)
                    part.bounds.Add(ContourBounds(s.outer));
                all.Add(part.bounds);
            }
            if (centre && all.Valid())
            {
                const Vec2 c = all.Center();
                for (auto& part : logo.parts)
                {
                    for (auto& s : part.shapes)
                    {
                        for (auto& p : s.outer)
                            p = p - c;
                        for (auto& h : s.holes)
                            for (auto& p : h)
                                p = p - c;
                    }
                    part.bounds = {part.bounds.min - c, part.bounds.max - c};
                }
                all = {all.min - c, all.max - c};
            }
            logo.bounds = all;
            if (logo.parts.empty() && logo.error.empty())
                logo.error = "The artwork contains no filled shapes.";
        }
    }  // namespace

    LogoImport ImportSvg(std::string_view svgText, const SvgImportSettings& st)
    {
        LogoImport logo;
        std::string buffer(svgText);  // nanosvg parses in place
        std::unique_ptr<NSVGimage, void (*)(NSVGimage*)> image(nsvgParse(buffer.data(), "px", 96.0f), nsvgDelete);
        if (!image)
        {
            logo.error = "The SVG could not be parsed.";
            return logo;
        }
        logo.sourceWidth = image->width;
        logo.sourceHeight = image->height;

        // Bounds of the filled artwork decide the scale (viewBox margins do not).
        Bounds2D src;
        for (NSVGshape* shape = image->shapes; shape; shape = shape->next)
            if ((shape->flags & NSVG_FLAGS_VISIBLE) && shape->fill.type != NSVG_PAINT_NONE)
                for (NSVGpath* path = shape->paths; path; path = path->next)
                    for (int i = 0; i < path->npts; ++i)
                        src.Add({path->pts[i * 2], path->pts[i * 2 + 1]});
        if (!src.Valid() || src.Size().x <= 0.0)
        {
            logo.error = "The SVG contains no filled shapes (strokes are not converted).";
            return logo;
        }
        const double scale = std::max(st.width, 1e-3) / src.Size().x;
        const double tolerance = std::max(st.tolerance, 1e-4) / scale;

        bool strokes = false, gradients = false;
        int index = 0;
        for (NSVGshape* shape = image->shapes; shape; shape = shape->next, ++index)
        {
            if (!(shape->flags & NSVG_FLAGS_VISIBLE))
                continue;
            if (shape->fill.type == NSVG_PAINT_NONE)
            {
                if (shape->stroke.type != NSVG_PAINT_NONE)
                    strokes = true;
                continue;
            }
            LogoPart part;
            part.name = shape->id[0] ? std::string(shape->id) : "Shape " + std::to_string(index + 1);
            if (shape->fill.type == NSVG_PAINT_COLOR)
                part.color = ColorFromAbgr(shape->fill.color);
            else
            {
                gradients = true;
                const NSVGgradient* g = shape->fill.gradient;
                part.color = g && g->nstops > 0 ? ColorFromAbgr(g->stops[0].color) : Vec3{1, 1, 1};
            }
            part.opacity = shape->opacity;

            std::vector<Contour> contours;
            for (NSVGpath* path = shape->paths; path; path = path->next)
            {
                if (path->npts < 4)
                    continue;
                Contour c;
                const float* p = path->pts;
                c.push_back({p[0], p[1]});
                for (int i = 0; i + 3 < path->npts; i += 3)
                {
                    const float* q = &path->pts[i * 2];
                    FlattenCubic(c, {q[0], q[1]}, {q[2], q[3]}, {q[4], q[5]}, {q[6], q[7]}, tolerance);
                }
                // SVG y points down: flip so the logo stands upright, then scale.
                for (auto& v : c)
                    v = {v.x * scale, -v.y * scale};
                contours.push_back(std::move(c));
            }
            part.shapes = BuildShapes(std::move(contours), shape->fillRule == NSVG_FILLRULE_EVENODD ? FillRule::EvenOdd
                                                                                                   : FillRule::NonZero);
            if (!part.shapes.empty())
                logo.parts.push_back(std::move(part));
        }
        if (strokes)
            logo.warnings.push_back("Stroke-only paths were skipped; convert strokes to fills (Object to Path) to include them.");
        if (gradients)
            logo.warnings.push_back("Gradient fills were turned into solid colours (the first gradient stop).");
        FinishImport(logo, st.centre);
        return logo;
    }

    LogoImport ImportSvgFile(const std::filesystem::path& file, const SvgImportSettings& settings)
    {
        std::string error;
        const auto bytes = ReadAll(file, &error);
        if (bytes.empty())
        {
            LogoImport logo;
            logo.error = error.empty() ? "The file is empty." : error;
            return logo;
        }
        return ImportSvg(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), settings);
    }

    std::optional<Image> DecodeImage(const std::vector<std::uint8_t>& bytes, std::string* error)
    {
        auto fail = [&](const std::string& what) -> std::optional<Image> {
            if (error)
                *error = what;
            return std::nullopt;
        };
        if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0)
            return fail("WebP images are not supported yet; save the logo as PNG.");
        if (bytes.empty() || bytes.size() > 256u * 1024u * 1024u)
            return fail("The image file is empty or too large.");
        int w = 0, h = 0, n = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n))
            return fail(std::string("The image could not be read: ") + stbi_failure_reason());
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
            return fail("Images larger than 8192 x 8192 pixels are not supported.");
        std::unique_ptr<stbi_uc, void (*)(void*)> pixels(
            stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4), stbi_image_free);
        if (!pixels)
            return fail(std::string("The image could not be decoded: ") + stbi_failure_reason());
        Image img;
        img.width = w;
        img.height = h;
        img.rgba.assign(pixels.get(), pixels.get() + std::size_t(w) * h * 4);
        return img;
    }

    std::optional<Image> LoadImageFile(const std::filesystem::path& file, std::string* error)
    {
        const auto bytes = ReadAll(file, error);
        if (bytes.empty())
            return std::nullopt;
        return DecodeImage(bytes, error);
    }

    std::vector<Contour> TraceAlphaContours(const Image& image, int threshold)
    {
        std::vector<Contour> loops;
        if (!image.Valid())
            return loops;
        const int W = image.width, H = image.height;
        const double thr = std::clamp(threshold, 1, 255);
        // Grid points are pixel centres, padded by one transparent ring so
        // every outline closes.
        auto value = [&](int i, int j) -> double {
            if (i < 0 || j < 0 || i >= W || j >= H)
                return 0.0;
            return image.Alpha(i, j);
        };
        auto inside = [&](int i, int j) { return value(i, j) >= thr; };
        // Edge keys: horizontal edge from (i,j) to (i+1,j) -> dir 0,
        // vertical edge from (i,j) to (i,j+1) -> dir 1.
        auto key = [&](int i, int j, int dir) -> long long {
            return ((static_cast<long long>(j) + 1) * (W + 3) + (i + 1)) * 2 + dir;
        };
        auto point = [&](int i0, int j0, int i1, int j1) -> Vec2 {
            const double v0 = value(i0, j0), v1 = value(i1, j1);
            const double t = std::abs(v1 - v0) > 1e-9 ? std::clamp((thr - v0) / (v1 - v0), 0.0, 1.0) : 0.5;
            return {i0 + (i1 - i0) * t + 0.5, j0 + (j1 - j0) * t + 0.5};
        };
        struct Seg
        {
            long long a, b;
            Vec2 pa, pb;
        };
        std::vector<Seg> segs;
        for (int j = -1; j < H; ++j)
            for (int i = -1; i < W; ++i)
            {
                const bool a = inside(i, j), b = inside(i + 1, j), c = inside(i + 1, j + 1), d = inside(i, j + 1);
                const int code = (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0) | (d ? 8 : 0);
                if (code == 0 || code == 15)
                    continue;
                const long long T = key(i, j, 0), B = key(i, j + 1, 0), L = key(i, j, 1), R = key(i + 1, j, 1);
                auto P = [&](long long k) -> Vec2 {
                    if (k == T) return point(i, j, i + 1, j);
                    if (k == B) return point(i, j + 1, i + 1, j + 1);
                    if (k == L) return point(i, j, i, j + 1);
                    return point(i + 1, j, i + 1, j + 1);
                };
                auto add = [&](long long e0, long long e1) { segs.push_back({e0, e1, P(e0), P(e1)}); };
                const bool centre = (value(i, j) + value(i + 1, j) + value(i + 1, j + 1) + value(i, j + 1)) * 0.25 >= thr;
                switch (code)
                {
                    case 1: case 14: add(L, T); break;
                    case 2: case 13: add(T, R); break;
                    case 4: case 11: add(R, B); break;
                    case 8: case 7: add(B, L); break;
                    case 3: case 12: add(L, R); break;
                    case 6: case 9: add(T, B); break;
                    case 5:
                        if (centre) { add(T, R); add(B, L); }
                        else { add(L, T); add(R, B); }
                        break;
                    case 10:
                        if (centre) { add(L, T); add(R, B); }
                        else { add(T, R); add(B, L); }
                        break;
                    default: break;
                }
            }
        // Stitch segments sharing edge crossings into loops.
        std::multimap<long long, std::size_t> byEdge;
        for (std::size_t s = 0; s < segs.size(); ++s)
        {
            byEdge.emplace(segs[s].a, s);
            byEdge.emplace(segs[s].b, s);
        }
        std::vector<bool> used(segs.size(), false);
        for (std::size_t start = 0; start < segs.size(); ++start)
        {
            if (used[start])
                continue;
            Contour loop;
            std::size_t cur = start;
            long long edge = segs[start].a;
            used[start] = true;
            loop.push_back(segs[start].pa);
            for (std::size_t guard = 0; guard <= segs.size(); ++guard)
            {
                const Seg& s = segs[cur];
                const long long next = s.a == edge ? s.b : s.a;
                loop.push_back(s.a == edge ? s.pb : s.pa);
                edge = next;
                std::size_t found = segs.size();
                const auto range = byEdge.equal_range(edge);
                for (auto it = range.first; it != range.second; ++it)
                    if (!used[it->second])
                    {
                        found = it->second;
                        break;
                    }
                if (found == segs.size())
                    break;
                used[found] = true;
                cur = found;
            }
            if (loop.size() >= 4)
                loops.push_back(std::move(loop));
        }
        return loops;
    }

    LogoImport TraceImage(const Image& image, const TraceSettings& st)
    {
        LogoImport logo;
        if (!image.Valid())
        {
            logo.error = "The image is empty.";
            return logo;
        }
        logo.sourceWidth = image.width;
        logo.sourceHeight = image.height;
        const int thr = std::clamp(st.alphaThreshold, 1, 255);
        bool anyTransparent = false;
        double r = 0, g = 0, b = 0, solid = 0;
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x)
            {
                const std::size_t i = (std::size_t(y) * image.width + x) * 4;
                if (image.rgba[i + 3] >= thr)
                {
                    r += image.rgba[i];
                    g += image.rgba[i + 1];
                    b += image.rgba[i + 2];
                    ++solid;
                }
                else
                    anyTransparent = true;
            }
        if (solid == 0)
        {
            logo.error = "No pixel is opaque enough; lower the alpha threshold.";
            return logo;
        }
        if (!anyTransparent)
            logo.warnings.push_back("The image has no transparent pixels, so the outline is its rectangle. Use a PNG with transparency.");

        std::vector<Contour> contours;
        for (auto& c : TraceAlphaContours(image, thr))
        {
            if (std::abs(SignedArea(c)) < st.minimumArea)
                continue;
            Contour s = st.simplify > 0 ? SimplifyContour(CleanContour(c), st.simplify) : CleanContour(c);
            if (st.smoothing > 0)
                s = SmoothContour(s, std::min(st.smoothing, 4));
            contours.push_back(std::move(s));
        }
        const double scale = std::max(st.width, 1e-3) / image.width;
        for (auto& c : contours)
            for (auto& p : c)
                p = {p.x * scale, -p.y * scale};
        LogoPart part;
        part.name = "Silhouette";
        part.color = {r / solid / 255.0, g / solid / 255.0, b / solid / 255.0};
        part.shapes = BuildShapes(std::move(contours), FillRule::EvenOdd);
        if (!part.shapes.empty())
            logo.parts.push_back(std::move(part));
        FinishImport(logo, st.centre);
        return logo;
    }
}  // namespace iris
