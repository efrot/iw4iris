// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Scene and title templates. A template builds ordinary, editable project
// content (actors with directions, cameras with keys, camera cuts, speed
// keys, graphics with animations, look settings) from the assets the
// loaded map actually has. Nothing is pre-rendered.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "iris/assets.hpp"
#include "iris/project.hpp"
#include "iris/shots.hpp"

namespace iris
{
    struct TemplateAssets
    {
        std::vector<CharacterEntry> characters;
        std::vector<std::string> animations;
        std::vector<std::string> weapons;
        std::vector<std::string> models;
        std::vector<std::string> effects;
    };

    struct TemplateInfo
    {
        std::string id;
        std::string label;
        std::string description;
    };

    struct TemplateResult
    {
        std::vector<std::string> created;   // ids of the new objects
        std::vector<std::string> warnings;
    };

    const std::vector<TemplateInfo>& SceneTemplates();
    // Builds the template around `centre` (on the ground), facing `yaw`.
    TemplateResult ApplySceneTemplate(Project& project, std::string_view templateId, const Vec3& centre, double yaw,
                                      const TemplateAssets& assets, double start = 0.0);

    const std::vector<TemplateInfo>& TitleTemplates();
    TemplateResult ApplyTitleTemplate(Project& project, std::string_view templateId, const std::string& text,
                                      const Vec3& position, double yaw, const std::string& font, double start = 0.0);

    // Idle / walk / run / crouch / aim animations found among `animations`.
    AnimationRoles FindAnimationRoles(const std::vector<std::string>& animations);

    // Adds an actor from a character (with idle animation and weapon) at a
    // position, facing yaw; returns its id.
    std::string AddActor(Project& project, const CharacterEntry& character, const Vec3& position, double yaw,
                         const TemplateAssets& assets, const std::string& weapon = {});

    // A dolly camera orbiting a graphic's bounds (one-click camera movement).
    std::string AddGraphicOrbit(Project& project, const GraphicDef& graphic, const Bounds3D& bounds, double start,
                                double duration);
    // Adds a shot of an actor as a new camera (and makes it active).
    std::string AddActorShot(Project& project, const ActorDef& actor, ShotType type, double start, double duration,
                             const ActorDef* secondary = nullptr);

    // The environment word a character's model or label carries ("arctic",
    // "desert", "forest", "woodland", "urban"), empty when it has none.
    std::string_view CharacterEnvironment(const CharacterEntry& character);
    // The loaded map's environment, voted by its soldiers (not the ghillie
    // suits, which every map loads in all variants); "urban" when none says.
    std::string_view MapEnvironment(const TemplateAssets& assets);

    // Chooses the preferred weapon (exact names first, then by base, then by
    // category) among the available ones.
    std::string PickWeapon(const std::vector<std::string>& weapons, const std::vector<std::string>& preferences,
                           const std::string& category = {});
}  // namespace iris
