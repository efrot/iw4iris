// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Motion graphics scene objects: extruded 3D text, logos (SVG / traced
// PNG / textured plane), primitives, imported models and tracking anchors.
// A graphic is data (what to build, where, how it animates); geometry is
// built from it on demand and cached by its geometry key; its pose is a pure
// function of cinematic time, including procedural animations (seeded), so
// previews, seeks and every render pass agree.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/camera.hpp"
#include "iris/font.hpp"
#include "iris/logo.hpp"
#include "iris/mesh.hpp"
#include "iris/model.hpp"
#include "iris/transform.hpp"

namespace iris
{
    enum class GraphicKind
    {
        Text,    // extruded 3D typography
        Logo,    // SVG or traced raster artwork, extruded or flat
        Image,   // a textured plane (keeps the image's colours and alpha)
        Shape,   // box, cylinder, sphere, plane, ring
        Model,   // imported OBJ
        Anchor,  // an invisible tracking point (parent / export marker)
    };
    std::string_view ToString(GraphicKind kind);
    std::optional<GraphicKind> GraphicKindFromString(std::string_view text);

    enum class ShapeKind
    {
        Box,
        Cylinder,
        Sphere,
        Plane,
        Ring,
    };
    std::string_view ToString(ShapeKind kind);
    std::optional<ShapeKind> ShapeKindFromString(std::string_view text);

    enum class LogoMode
    {
        Extruded,  // shapes extruded into solid geometry
        Flat,      // flat filled shapes (decal-like)
    };

    // How a graphic is composited with the scene.
    enum class GraphicRenderMode
    {
        Depth,    // tested against the scene's depth (needs a verified depth buffer)
        OnTop,    // always visible, drawn over the scene (explicit choice)
        XRay,     // depth tested, plus a faint see-through pass where hidden
    };
    std::string_view ToString(GraphicRenderMode mode);
    std::optional<GraphicRenderMode> GraphicRenderModeFromString(std::string_view text);

    enum class TrackingMode
    {
        World,      // fixed in the world (its own transform track)
        Actor,      // follows an actor's attach point
        Billboard,  // world position, always turned towards the camera
        Camera,     // locked in front of the shot camera
    };
    std::string_view ToString(TrackingMode mode);
    std::optional<TrackingMode> TrackingModeFromString(std::string_view text);

    struct TrackingDef
    {
        TrackingMode mode = TrackingMode::World;
        std::string actor;              // Actor mode: actor id
        std::string attach = "origin";  // origin, head, neck, chest, pelvis, rightHand, leftHand, weapon
        bool inheritRotation = false;   // Actor mode: rotate with the attach point
        bool yawOnly = true;            // Billboard: turn about the up axis only
        double distance = 120.0;        // Camera mode: units in front of the lens
        bool operator==(const TrackingDef&) const = default;
    };

    // A material for the fixed-function renderer: Blinn–Phong with an
    // emissive term. "Metal", "chrome" and "glass" are approximations of
    // those looks, not physically based shading.
    struct MaterialDef
    {
        std::string preset = "matte_white";
        Vec3 color{0.9, 0.9, 0.9};
        double opacity = 1.0;
        double specular = 0.15;     // 0..1 specular strength
        double gloss = 12.0;        // specular power
        double metallic = 0.0;      // 0..1 tints the specular by the colour
        Vec3 emission{0, 0, 0};
        double emissionStrength = 0.0;
        bool operator==(const MaterialDef&) const = default;
    };

    struct MaterialSlots
    {
        MaterialDef front;
        MaterialDef side;
        MaterialDef bevel;
        MaterialDef back;
        bool separate = false;  // false: every part uses `front`
        bool operator==(const MaterialSlots&) const = default;
        const MaterialDef& For(MeshPart part) const;
    };

    struct MaterialPreset
    {
        std::string id;
        std::string label;
        MaterialDef material;
    };
    const std::vector<MaterialPreset>& MaterialPresets();
    MaterialDef MakeMaterial(std::string_view presetId);

    // A procedural animation. Every parameter is explicit and the randomness
    // is seeded, so the same time always gives the same pose.
    struct GraphicAnimation
    {
        std::string preset = "flyIn";
        double start = 0.0;       // cinematic seconds
        double duration = 1.0;
        double stagger = 0.05;    // seconds between elements (letters, logo parts)
        double intensity = 1.0;
        Vec3 direction{0, -1, 0}; // graphic space, for fly / glitch presets
        std::string easing = "easeOutCubic";
        std::uint32_t seed = 1;
        bool enabled = true;
        bool operator==(const GraphicAnimation&) const = default;
    };

    struct AnimationPresetInfo
    {
        std::string id;
        std::string label;
        bool perElement;  // uses the stagger
        bool continuous;  // keeps running after its duration (loops)
    };
    const std::vector<AnimationPresetInfo>& AnimationPresets();
    const std::vector<std::string>& EasingNames();
    double EaseValue(std::string_view easing, double t);

    struct GeometrySettings
    {
        double depth = 6.0;
        double bevel = 0.6;
        int bevelSegments = 3;
        double detail = 0.08;     // curve tolerance in units (smaller = smoother)
        bool operator==(const GeometrySettings&) const = default;
    };

    struct GraphicDef
    {
        std::string id;
        std::string name;
        GraphicKind kind = GraphicKind::Text;

        // Text
        std::string text = "IW4IRIS";
        std::string font;          // font file (absolute, or relative to the project)
        TextSettings textSettings;
        bool separateWords = false;  // animate words rather than letters

        // Logo / Image / Model source file
        std::string source;
        LogoMode logoMode = LogoMode::Extruded;
        double width = 100.0;      // logo / image width in units
        int alphaThreshold = 128;
        double simplify = 0.6;     // traced outline simplification, pixels
        int smoothing = 1;
        bool useLogoColors = true; // SVG / traced colours as the front colour
        double modelScale = 1.0;
        ModelUpAxis modelUp = ModelUpAxis::Y;

        // Shape
        ShapeKind shape = ShapeKind::Box;
        Vec3 shapeSize{40, 40, 40};

        GeometrySettings geometry;
        MaterialSlots materials;
        bool doubleSided = false;

        Transform transform;       // rest transform (world, or offset when tracking an actor)
        TransformTrack track;
        ScalarCurve opacity{1.0};
        TrackingDef tracking;
        GraphicRenderMode renderMode = GraphicRenderMode::Depth;
        std::vector<GraphicAnimation> animations;

        bool visible = true;
        bool locked = false;
        bool castShadow = false;   // planned: see FEATURE_MATRIX.md
        std::string group;
        std::string parent;        // another graphic (anchor) this one follows

        bool operator==(const GraphicDef& o) const;
    };

    // ---------------------------------------------------------------- geometry

    struct GraphicElement
    {
        Mesh mesh;              // relative to `pivot`
        Vec3 pivot;             // element origin in graphic space
        std::size_t word = 0;
        std::size_t line = 0;
        Vec3 color{1, 1, 1};    // artwork colour (logo parts)
        bool hasColor = false;
        std::string name;
    };

    struct GraphicGeometry
    {
        std::vector<GraphicElement> elements;
        Bounds3D bounds;  // graphic space, at rest
        std::shared_ptr<Image> texture;  // Image kind: the RGBA texture
        std::vector<std::string> warnings;
        std::string error;
        std::size_t TriangleCount() const;
        explicit operator bool() const { return error.empty(); }
    };

    using FontResolver = std::function<std::shared_ptr<Font>(const std::string& font, std::string* error)>;

    // A string of every parameter that changes the geometry; rebuild when it
    // changes.
    std::string GeometryKey(const GraphicDef& graphic);
    GraphicGeometry BuildGraphicGeometry(const GraphicDef& graphic, const FontResolver& fonts,
                                         const std::filesystem::path& baseDirectory);

    // ---------------------------------------------------------------- pose

    struct ElementPose
    {
        Vec3 offset;             // graphic space, added after the element's rotation and scale
        Vec3 rotationDeg;        // about the element's x (right), y (up), z (front) axes
        double scale = 1.0;
        double depthScale = 1.0; // extrusion growth
        double opacity = 1.0;
        double emissionBoost = 0.0;
    };

    struct GraphicPose
    {
        Transform world;         // graphic space -> world
        double opacity = 1.0;
        bool visible = true;
        std::vector<ElementPose> elements;
    };

    struct TrackingContext
    {
        // World transform of an actor's attach point at the evaluated time.
        std::function<std::optional<Transform>(std::string_view actor, std::string_view attach)> attachPoint;
        // The camera the frame is rendered from.
        std::optional<CameraState> camera;
        // Pose of another graphic (for parenting); may be empty.
        std::function<std::optional<Transform>(std::string_view graphic)> graphicPose;
    };

    GraphicPose EvaluateGraphic(const GraphicDef& graphic, double t, std::size_t elementCount,
                                const TrackingContext& context = {});

    // Rotates graphic-space vectors by element rotations (x, then y, then z).
    Vec3 RotateGraphicSpace(const Vec3& v, const Vec3& rotationDeg);
}  // namespace iris
