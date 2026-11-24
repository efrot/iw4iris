// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Logo import for motion graphics: SVG vector artwork (nanosvg) and
// transparent raster images (alpha contour tracing). Both produce filled
// 2D shapes that keep the artwork's proportions; the caller extrudes them
// or keeps them flat. Raster images can also be used as a textured plane.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/mesh.hpp"

namespace iris
{
    // One separately coloured part of a logo (an SVG shape, or the traced
    // silhouette of an image). Parts can get their own material and be
    // animated separately (logo assembly).
    struct LogoPart
    {
        std::string name;
        Vec3 color{1, 1, 1};   // fill colour from the artwork, 0..1
        double opacity = 1.0;
        std::vector<Shape2D> shapes;
        Bounds2D bounds;
    };

    struct LogoImport
    {
        std::vector<LogoPart> parts;
        Bounds2D bounds;           // of every part, after scaling
        double sourceWidth = 0.0;  // artwork size before scaling (px / user units)
        double sourceHeight = 0.0;
        std::vector<std::string> warnings;
        std::string error;
        explicit operator bool() const { return error.empty() && !parts.empty(); }
    };

    struct SvgImportSettings
    {
        double width = 100.0;      // target width in game units (proportions kept)
        double tolerance = 0.25;   // curve flattening in game units
        bool centre = true;        // centre the logo on the origin
    };

    LogoImport ImportSvg(std::string_view svgText, const SvgImportSettings& settings);
    LogoImport ImportSvgFile(const std::filesystem::path& file, const SvgImportSettings& settings);

    // 8-bit RGBA image, rows top to bottom.
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;
        bool Valid() const { return width > 0 && height > 0 && rgba.size() == std::size_t(width) * height * 4; }
        std::uint8_t Alpha(int x, int y) const { return rgba[(std::size_t(y) * width + x) * 4 + 3]; }
    };

    // PNG, JPEG, BMP or TGA (stb_image). Images larger than 8192 x 8192 are refused.
    std::optional<Image> DecodeImage(const std::vector<std::uint8_t>& fileBytes, std::string* error = nullptr);
    std::optional<Image> LoadImageFile(const std::filesystem::path& file, std::string* error = nullptr);

    struct TraceSettings
    {
        double width = 100.0;          // target width in game units
        int alphaThreshold = 128;      // 0..255: pixels at or above are solid
        double simplify = 0.6;         // Douglas–Peucker tolerance in pixels (0 = off)
        int smoothing = 1;             // Chaikin iterations on the traced outline
        double minimumArea = 4.0;      // drop specks smaller than this many pixels
        bool centre = true;
    };

    // Traces the alpha silhouette of an image into shapes (marching squares
    // with sub-pixel edge interpolation). The part's colour is the average
    // colour of the solid pixels.
    LogoImport TraceImage(const Image& image, const TraceSettings& settings);

    // Raw marching-squares contours in pixel space (y down), for tests and
    // the comparison view.
    std::vector<Contour> TraceAlphaContours(const Image& image, int threshold);
}  // namespace iris
