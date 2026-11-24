// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/font.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "vendor/stb_truetype_impl.hpp"

namespace iris
{
    struct Font::Impl
    {
        std::vector<unsigned char> data;
        stbtt_fontinfo info{};
        double unitScale = 1.0;  // scale for an em of 1 unit
        int ascent = 0, descent = 0, lineGap = 0;
    };

    namespace
    {
        // UTF-16 big endian (name table, Microsoft platform) to UTF-8.
        std::string Utf16BeToUtf8(const char* bytes, int length)
        {
            std::string out;
            for (int i = 0; i + 1 < length; i += 2)
            {
                char32_t c = (static_cast<unsigned char>(bytes[i]) << 8) | static_cast<unsigned char>(bytes[i + 1]);
                if (c >= 0xD800 && c < 0xDC00 && i + 3 < length)
                {
                    const char32_t lo = (static_cast<unsigned char>(bytes[i + 2]) << 8) |
                                        static_cast<unsigned char>(bytes[i + 3]);
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
                if (c < 0x80)
                    out += static_cast<char>(c);
                else if (c < 0x800)
                {
                    out += static_cast<char>(0xC0 | (c >> 6));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                }
                else if (c < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (c >> 12));
                    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (c >> 18));
                    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                }
            }
            return out;
        }

        std::string NameString(const stbtt_fontinfo& info, int nameId)
        {
            int length = 0;
            // Microsoft platform, Unicode BMP, US English, then any Microsoft entry.
            if (const char* s = stbtt_GetFontNameString(&info, &length, STBTT_PLATFORM_ID_MICROSOFT,
                                                        STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, nameId))
                return Utf16BeToUtf8(s, length);
            // Macintosh Roman (single byte).
            if (const char* s = stbtt_GetFontNameString(&info, &length, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                                        STBTT_MAC_LANG_ENGLISH, nameId))
                return std::string(s, static_cast<std::size_t>(length));
            return {};
        }
    }  // namespace

    Font::Font() : impl_(std::make_unique<Impl>()) {}
    Font::~Font() = default;

    std::shared_ptr<Font> Font::Load(const std::filesystem::path& file, std::string* error)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            if (error)
                *error = "Could not open the font file.";
            return nullptr;
        }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return FromBytes(std::move(bytes), error);
    }

    std::shared_ptr<Font> Font::FromBytes(std::vector<unsigned char> bytes, std::string* error, int faceIndex)
    {
        auto fail = [&](const char* what) -> std::shared_ptr<Font> {
            if (error)
                *error = what;
            return nullptr;
        };
        if (bytes.size() < 12 || bytes.size() > 64u * 1024u * 1024u)
            return fail("The file is not a font (or larger than 64 MB).");
        std::shared_ptr<Font> font(new Font());
        font->impl_->data = std::move(bytes);
        const unsigned char* data = font->impl_->data.data();
        const int offset = stbtt_GetFontOffsetForIndex(data, faceIndex);
        if (offset < 0)
            return fail("The font face was not found in the file.");
        if (!stbtt_InitFont(&font->impl_->info, data, offset))
            return fail("The file is not a TrueType / OpenType font stb_truetype can read.");
        auto& impl = *font->impl_;
        impl.unitScale = static_cast<double>(stbtt_ScaleForMappingEmToPixels(&impl.info, 1.0f));
        stbtt_GetFontVMetrics(&impl.info, &impl.ascent, &impl.descent, &impl.lineGap);
        font->info_.family = NameString(impl.info, 1);
        font->info_.style = NameString(impl.info, 2);
        font->info_.fullName = NameString(impl.info, 4);
        if (const stbtt_uint32 os2 = stbtt__find_table(impl.info.data, static_cast<stbtt_uint32>(impl.info.fontstart), "OS/2"))
            font->info_.fsType = ttUSHORT(impl.info.data + os2 + 8);
        return font;
    }

    double Font::Ascent(double size) const { return static_cast<double>(impl_->ascent) * impl_->unitScale * size; }
    double Font::Descent(double size) const { return static_cast<double>(impl_->descent) * impl_->unitScale * size; }
    double Font::LineGap(double size) const { return static_cast<double>(impl_->lineGap) * impl_->unitScale * size; }

    bool Font::HasGlyph(char32_t cp) const
    {
        return stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp)) != 0;
    }

    double Font::Advance(char32_t cp, double size) const
    {
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(cp), &advance, &lsb);
        return static_cast<double>(advance) * impl_->unitScale * size;
    }

    double Font::Kerning(char32_t left, char32_t right, double size) const
    {
        return static_cast<double>(
                   stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(left), static_cast<int>(right))) *
               impl_->unitScale * size;
    }

    std::vector<Contour> Font::GlyphOutline(char32_t cp, double size, double tolerance) const
    {
        stbtt_vertex* verts = nullptr;
        const int count = stbtt_GetCodepointShape(&impl_->info, static_cast<int>(cp), &verts);
        const double s = impl_->unitScale * size;
        std::vector<Contour> contours;
        Vec2 pen;
        for (int i = 0; i < count; ++i)
        {
            const stbtt_vertex& v = verts[i];
            const Vec2 p{v.x * s, v.y * s};
            switch (v.type)
            {
                case STBTT_vmove:
                    contours.emplace_back();
                    contours.back().push_back(p);
                    break;
                case STBTT_vline:
                    if (!contours.empty())
                        contours.back().push_back(p);
                    break;
                case STBTT_vcurve:
                    if (!contours.empty())
                        FlattenQuadratic(contours.back(), pen, {v.cx * s, v.cy * s}, p, tolerance);
                    break;
                case STBTT_vcubic:
                    if (!contours.empty())
                        FlattenCubic(contours.back(), pen, {v.cx * s, v.cy * s}, {v.cx1 * s, v.cy1 * s}, p, tolerance);
                    break;
                default:
                    break;
            }
            pen = p;
        }
        stbtt_FreeShape(&impl_->info, verts);
        return contours;
    }

    std::u32string DecodeUtf8(std::string_view text)
    {
        std::u32string out;
        for (std::size_t i = 0; i < text.size();)
        {
            const auto c = static_cast<unsigned char>(text[i]);
            int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
            if (len == 0 || i + static_cast<std::size_t>(len) > text.size())
            {
                out += U'�';
                ++i;
                continue;
            }
            char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
            bool ok = true;
            for (int k = 1; k < len; ++k)
            {
                const auto cc = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
                if ((cc & 0xC0) != 0x80)
                    ok = false;
                cp = (cp << 6) | (cc & 0x3F);
            }
            out += ok ? cp : U'�';
            i += ok ? static_cast<std::size_t>(len) : 1;
        }
        return out;
    }

    namespace
    {
        Vec2 Rotate2(const Vec2& p, double a)
        {
            const double c = std::cos(a), s = std::sin(a);
            return {p.x * c - p.y * s, p.x * s + p.y * c};
        }

        bool IsSpace(char32_t c) { return c == U' ' || c == U'\t' || c == 0xA0 || c == 0x3000; }
    }  // namespace

    std::vector<Shape2D> PlacedShapes(const GlyphPlacement& g)
    {
        std::vector<Shape2D> out = g.shapes;
        auto place = [&](Contour& c) {
            for (auto& p : c)
                p = g.origin + Rotate2(p, g.rotation);
        };
        for (auto& s : out)
        {
            place(s.outer);
            for (auto& h : s.holes)
                place(h);
        }
        return out;
    }

    TextLayout LayoutText(const Font& font, std::string_view utf8, const TextSettings& st)
    {
        TextLayout layout;
        const std::u32string text = DecodeUtf8(utf8);
        const double size = std::max(st.size, 1e-3);
        const double lineHeight = (font.Ascent(size) - font.Descent(size) + font.LineGap(size)) * st.lineSpacing;

        struct Pending
        {
            GlyphPlacement g;
            double advance = 0.0;
        };
        std::vector<std::vector<Pending>> lines(1);
        std::vector<double> widths(1, 0.0);
        std::size_t word = 0, visible = 0;
        bool lastWasSpace = false;
        char32_t prev = 0;
        std::vector<char32_t> missing;

        for (const char32_t c : text)
        {
            if (c == U'\n')
            {
                lines.emplace_back();
                widths.push_back(0.0);
                prev = 0;
                ++word;
                continue;
            }
            if (c == U'\r')
                continue;
            double& pen = widths.back();
            if (prev && st.kerning)
                pen += font.Kerning(prev, c, size);
            const double advance = font.Advance(c, size) + st.letterSpacing + (IsSpace(c) ? st.wordSpacing : 0.0);
            if (IsSpace(c))
            {
                if (!lastWasSpace)
                    ++word;
                lastWasSpace = true;
                pen += advance;
                prev = c;
                continue;
            }
            lastWasSpace = false;
            if (!font.HasGlyph(c))
            {
                if (std::find(missing.begin(), missing.end(), c) == missing.end())
                    missing.push_back(c);
                prev = 0;
                continue;
            }
            Pending p;
            p.g.codepoint = c;
            p.g.charIndex = visible++;
            p.g.line = lines.size() - 1;
            p.g.word = word;
            p.g.origin = {pen, 0.0};
            p.g.shapes = BuildShapes(font.GlyphOutline(c, size, st.tolerance), FillRule::NonZero);
            for (const auto& s : p.g.shapes)
                p.g.bounds.Add(ContourBounds(s.outer));
            p.advance = advance;
            lines.back().push_back(std::move(p));
            pen += advance;
            prev = c;
        }
        for (const char32_t c : missing)
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "The font has no glyph for U+%04X.", static_cast<unsigned>(c));
            layout.warnings.push_back(buf);
        }
        // Trailing letter spacing does not count towards a line's width.
        for (auto& w : widths)
            w = std::max(0.0, w - st.letterSpacing);
        layout.lineCount = lines.size();

        const double ascent = font.Ascent(size), descent = font.Descent(size);
        const double blockTop = ascent, blockBottom = -(static_cast<double>(lines.size()) - 1) * lineHeight + descent;
        double yOffset = 0.0;
        switch (st.verticalAlign)
        {
            case TextVerticalAlign::Top: yOffset = -blockTop; break;
            case TextVerticalAlign::Middle: yOffset = -(blockTop + blockBottom) * 0.5; break;
            case TextVerticalAlign::Baseline: yOffset = 0.0; break;
            case TextVerticalAlign::Bottom: yOffset = -blockBottom; break;
        }
        auto alignX = [&](double width) {
            switch (st.align)
            {
                case TextAlign::Left: return 0.0;
                case TextAlign::Center: return -width * 0.5;
                case TextAlign::Right: return -width;
            }
            return 0.0;
        };

        if (st.layout == TextLayoutKind::Vertical)
        {
            // Each line becomes a column; characters are centred on it.
            const double columnWidth = size * st.lineSpacing;
            const double charStep = (ascent - descent) * 0.9 + st.letterSpacing;
            std::size_t longest = 0;
            for (const auto& l : lines)
                longest = std::max(longest, l.size());
            const double columnsWidth = (static_cast<double>(lines.size()) - 1) * columnWidth;
            const double height = static_cast<double>(longest) * charStep;
            for (std::size_t li = 0; li < lines.size(); ++li)
                for (std::size_t k = 0; k < lines[li].size(); ++k)
                {
                    auto& p = lines[li][k];
                    const double x = static_cast<double>(li) * columnWidth - columnsWidth * 0.5;
                    const double y = height * 0.5 - (static_cast<double>(k) + 1) * charStep - descent * 0.9;
                    p.g.origin = {x - (p.advance - st.letterSpacing) * 0.5, y};
                    layout.glyphs.push_back(std::move(p.g));
                }
        }
        else
        {
            for (std::size_t li = 0; li < lines.size(); ++li)
            {
                const double dx = alignX(widths[li]);
                const double y = -static_cast<double>(li) * lineHeight + yOffset;
                for (auto& p : lines[li])
                {
                    p.g.origin = {p.g.origin.x + dx, y};
                    if (st.layout == TextLayoutKind::Circle)
                    {
                        // Arc length = x of the glyph's centre; the baseline
                        // follows a circle whose top is at y.
                        const double r = std::max(st.radius, 1.0);
                        const double centreX = p.g.origin.x + (p.advance - st.letterSpacing) * 0.5;
                        const double theta = centreX / r;
                        const Vec2 centre{0.0, y - r};
                        const Vec2 onCircle = centre + Vec2{r * std::sin(theta), r * std::cos(theta)};
                        p.g.rotation = -theta;
                        p.g.origin = onCircle + Rotate2({-(p.advance - st.letterSpacing) * 0.5, 0.0}, p.g.rotation);
                    }
                    layout.glyphs.push_back(std::move(p.g));
                }
            }
        }
        for (const auto& g : layout.glyphs)
            for (const auto& s : PlacedShapes(g))
                layout.bounds.Add(ContourBounds(s.outer));
        return layout;
    }
}  // namespace iris
