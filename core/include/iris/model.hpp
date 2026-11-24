// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Import of user models for motion graphics: Wavefront OBJ (what Blender,
// Cinema 4D and most tools export). Polygons are triangulated, missing
// normals are computed, materials become groups. glTF / FBX import is
// planned (see ROADMAP.md).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "iris/mesh.hpp"

namespace iris
{
    enum class ModelUpAxis
    {
        Y,  // OBJ default (Blender's exporter with "Y up")
        Z,  // Z-up exports: (x, y, z) -> (x, z, -y)
    };

    struct ModelImportSettings
    {
        double scale = 1.0;              // multiplies the file's units
        ModelUpAxis upAxis = ModelUpAxis::Y;
        bool centre = true;              // centre the bounds on the origin
        std::size_t maxTriangles = 250000;  // larger models are refused (32-bit game process)
        std::size_t maxVertices = 400000;
    };

    struct ModelGroup
    {
        std::string material;  // usemtl name
        Vec3 color{0.8, 0.8, 0.8};  // Kd from the .mtl when found
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct ModelImport
    {
        Mesh mesh;
        std::vector<ModelGroup> groups;
        Bounds3D bounds;
        std::size_t sourceFaces = 0;
        bool normalsComputed = false;
        std::vector<std::string> warnings;
        std::string error;
        explicit operator bool() const { return error.empty() && !mesh.indices.empty(); }
    };

    // `mtlText` is the content of the material library, if any.
    ModelImport ImportObj(std::string_view objText, const ModelImportSettings& settings, std::string_view mtlText = {});
    // Reads the .obj and, when referenced and present, its .mtl.
    ModelImport ImportObjFile(const std::filesystem::path& file, const ModelImportSettings& settings);
}  // namespace iris
