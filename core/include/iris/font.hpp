// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Fonts and text layout for 3D typography. TrueType and OpenType (CFF)
// outlines are read with stb_truetype; glyphs become polygons with holes
// that the mesh code extrudes. Layout covers kerning (kern / GPOS pairs),
// spacing, alignment, multiple lines, vertical text and text on a circle.
// Complex-script shaping (ligatures, Arabic joining, Indic reordering) is not
// performed: each code point maps to one glyph.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "iris/mesh.hpp"

namespace iris
{
    struct FontInfo
    {
        std::string family;
        std::string style;
        std::string fullName;
        int fsType = 0;  // OS/2 embedding permissions
        // OS/2 fsType says the font may not be embedded in documents (bit 1
        // without the preview / editable bits). Rendering it into frames is
        // not embedding, but scene packages leave such fonts out.
        bool RestrictedEmbedding() const { return (fsType & 0x000E) == 0x0002; }
    };

    class Font
    {
       public:
        static std::shared_ptr<Font> Load(const std::filesystem::path& file, std::string* error = nullptr);
        static std::shared_ptr<Font> FromBytes(std::vector<unsigned char> bytes, std::string* error = nullptr,
                                               int faceIndex = 0);
        ~Font();
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;

        const FontInfo& Info() const { return info_; }

        // Metrics for an em size of `size` units.
        double Ascent(double size) const;
        double Descent(double size) const;  // negative
        double LineGap(double size) const;
        bool HasGlyph(char32_t codepoint) const;
        double Advance(char32_t codepoint, double size) const;
        double Kerning(char32_t left, char32_t right, double size) const;
        // Outline of a glyph relative to its origin (y up), curves flattened
        // to `tolerance` units.
        std::vector<Contour> GlyphOutline(char32_t codepoint, double size, double tolerance) const;

       private:
        Font();
        struct Impl;
        std::unique_ptr<Impl> impl_;
        FontInfo info_;
    };

    enum class TextAlign
    {
        Left,
        Center,
        Right,
    };

    enum class TextVerticalAlign
    {
        Top,
        Middle,
        Baseline,
        Bottom,
    };

    enum class TextLayoutKind
    {
        Horizontal,
        Vertical,  // characters stacked top to bottom, lines as columns
        Circle,    // along the top of a circle of `radius`, centred
    };

    struct TextSettings
    {
        double size = 24.0;           // em size in game units
        double letterSpacing = 0.0;   // extra units between characters
        double wordSpacing = 0.0;     // extra units for spaces
        double lineSpacing = 1.2;     // multiple of the font's line height
        TextAlign align = TextAlign::Center;
        TextVerticalAlign verticalAlign = TextVerticalAlign::Middle;
        TextLayoutKind layout = TextLayoutKind::Horizontal;
        double radius = 120.0;        // circle layout
        double tolerance = 0.05;      // curve flattening, units
        bool kerning = true;
    };

    struct GlyphPlacement
    {
        char32_t codepoint = 0;
        std::size_t charIndex = 0;  // index among the visible glyphs
        std::size_t line = 0;
        std::size_t word = 0;
        Vec2 origin;                // glyph origin in text space
        double rotation = 0.0;      // radians, counter-clockwise about `origin`
        std::vector<Shape2D> shapes;  // relative to `origin`, unrotated
        Bounds2D bounds;            // of `shapes`
    };

    struct TextLayout
    {
        std::vector<GlyphPlacement> glyphs;
        Bounds2D bounds;  // of every glyph in text space (rotation included)
        std::size_t lineCount = 0;
        std::vector<std::string> warnings;
    };

    // Decodes UTF-8 (invalid sequences become U+FFFD).
    std::u32string DecodeUtf8(std::string_view text);

    TextLayout LayoutText(const Font& font, std::string_view utf8, const TextSettings& settings);

    // The shapes of a placed glyph transformed into text space.
    std::vector<Shape2D> PlacedShapes(const GlyphPlacement& glyph);
}  // namespace iris
