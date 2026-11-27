// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Director Mode scene objects: actors (character models with animation
// clips, equipment and high-level directions), props, effects and motion
// graphics, placed on a film set (a map loaded as a local offline session).
//
// Like everything in the core, the scene is data plus pure functions of
// cinematic time. The engine side turns the evaluated state into requests
// for the IW4Iris Director Mod (GSC), which owns the game entities.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/curve.hpp"
#include "iris/graphics.hpp"
#include "iris/transform.hpp"

namespace iris
{
    // An animation clip on an actor's timeline. Script models play an
    // animation from its start at the engine's own rate: the clip says
    // which animation plays from when to when; offsets, speed and blending
    // are not available for script models (see FEATURE_MATRIX.md).
    struct AnimClip
    {
        std::string id;
        std::string anim;        // XAnimParts name, e.g. "pb_stand_alert"
        double start = 0.0;      // cinematic seconds
        double duration = 2.0;
        bool operator==(const AnimClip&) const = default;
        double End() const { return start + duration; }
    };

    // A model attached to an actor tag (helmet, backpack, second weapon).
    struct ActorAttachment
    {
        std::string model;       // XModel, or a weapon name ending in "_mp"
        std::string tag = "tag_weapon_left";
        int camo = 0;
        bool operator==(const ActorAttachment&) const = default;
    };

    enum class ActionKind
    {
        Wait,           // stand idle
        WalkTo,
        RunTo,
        TurnTo,         // turn towards a point
        FaceActor,
        FaceCamera,
        PlayAnimation,
        Crouch,
        AimAt,          // turn towards a point and hold the aim animation
        Enter,          // becomes visible
        Exit,           // becomes hidden
        FollowActor,
    };
    std::string_view ToString(ActionKind kind);
    std::optional<ActionKind> ActionKindFromString(std::string_view text);
    // Human-readable sentence for the action list ("Walk to marker", ...).
    std::string Describe(ActionKind kind);

    struct ActorAction
    {
        ActionKind kind = ActionKind::Wait;
        double duration = 2.0;    // Wait, PlayAnimation, Crouch, AimAt, FollowActor
        Vec3 target;              // WalkTo, RunTo, TurnTo, AimAt
        std::string targetActor;  // FaceActor, FollowActor
        std::string anim;         // PlayAnimation (or override for the others)
        double speed = 0.0;       // units per second, 0 = the default walk / run speed
        bool operator==(const ActorAction&) const = default;
    };

    struct ActorDef
    {
        std::string id;
        std::string name;
        std::string character;    // display name from the character library
        std::string bodyModel;
        std::string headModel;    // empty: the body has no separate head
        std::string idleAnim;     // plays when no clip is active
        std::string weapon;       // e.g. "cheytac_mp"; empty = unarmed
        std::string weaponTag = "tag_weapon_right";
        int camo = 0;
        std::vector<ActorAttachment> attachments;

        Transform transform;      // rest transform (feet on the ground)
        TransformTrack track;
        ScalarCurve visibility{1.0};  // > 0.5 visible (keys use Hold)
        std::vector<AnimClip> clips;
        std::vector<ActorAction> actions;  // directions; compiled into track + clips
        double actionsStart = 0.0;

        bool visible = true;
        bool locked = false;
        std::string group;

        bool operator==(const ActorDef& o) const;
    };

    struct PropDef
    {
        std::string id;
        std::string name;
        std::string model;
        Transform transform;
        TransformTrack track;
        ScalarCurve visibility{1.0};
        std::string attachActor;  // follows this actor's tag when set
        std::string attachTag;
        bool visible = true;
        bool locked = false;
        std::string group;
        bool operator==(const PropDef& o) const;
    };

    struct EffectDef
    {
        std::string id;
        std::string name;
        std::string fx;           // FxEffectDef name, e.g. "smoke/thin_black_smoke_m"
        Transform transform;
        double start = 0.0;
        double repeat = 0.0;      // seconds between plays, 0 = once
        int count = 1;            // plays when repeating (0 = until the end)
        std::string attachActor;
        std::string attachTag;
        bool enabled = true;
        bool operator==(const EffectDef&) const = default;
    };

    // The film set is the project's map (Project::mapName) loaded as a
    // local, offline session.
    struct FilmSetSettings
    {
        std::string gametype = "dm";   // the game mode the set runs under
        // The IW4x mod folder the set runs with ("mods/<name>"), empty for the
        // standard game files. Chosen on purpose (custom camos or skins);
        // never inherited from the last demo (whose server's mod IW4x
        // switches to while the demo plays).
        std::string modFolder;
        bool operator==(const FilmSetSettings&) const = default;
    };

    // "mods/<name>" with a plain folder name (letters, digits, - _ .; no
    // "..", no path separators beyond the one after "mods"), or empty.
    bool IsValidModFolder(std::string_view folder);

    struct Scene
    {
        FilmSetSettings filmSet;
        std::vector<ActorDef> actors;
        std::vector<PropDef> props;
        std::vector<EffectDef> effects;
        std::vector<GraphicDef> graphics;

        bool Empty() const { return actors.empty() && props.empty() && effects.empty() && graphics.empty(); }
        ActorDef* FindActor(std::string_view id);
        const ActorDef* FindActor(std::string_view id) const;
        PropDef* FindProp(std::string_view id);
        EffectDef* FindEffect(std::string_view id);
        GraphicDef* FindGraphic(std::string_view id);
        const GraphicDef* FindGraphic(std::string_view id) const;
        bool Contains(std::string_view id) const;

        // Every model / animation / effect the scene uses, de-duplicated:
        // the Director Mod precaches these when the film set loads.
        std::vector<std::string> ReferencedModels() const;
        std::vector<std::string> ReferencedAnimations() const;
        std::vector<std::string> ReferencedWeapons() const;
        std::vector<std::string> ReferencedEffects() const;

        double ContentEnd() const;
        bool operator==(const Scene&) const = default;
    };

    // ---------------------------------------------------------------- evaluation

    struct ActorState
    {
        std::string id;
        Transform transform;
        bool visible = true;
        std::string anim;         // the animation that should be playing
        double animStart = 0.0;   // when it started (cinematic seconds)
        std::string clipId;       // empty for the idle animation
    };
    ActorState EvaluateActor(const ActorDef& actor, double t);

    struct PropState
    {
        std::string id;
        Transform transform;
        bool visible = true;
    };
    PropState EvaluateProp(const PropDef& prop, double t);

    // Effect plays with t0 < time <= t1 (playback moving forward).
    struct EffectEvent
    {
        const EffectDef* effect = nullptr;
        double time = 0.0;
    };
    std::vector<EffectEvent> EffectEventsBetween(const Scene& scene, double t0, double t1);

    // ---------------------------------------------------------------- directions

    struct AnimationRoles
    {
        std::string idle;
        std::string walk;
        std::string run;
        std::string crouch;
        std::string aim;
    };

    struct DirectionContext
    {
        AnimationRoles roles;
        double walkSpeed = 80.0;     // units per second
        double runSpeed = 190.0;
        double turnRate = 240.0;     // degrees per second
        double followDistance = 64.0;
        std::function<std::optional<Vec3>(std::string_view actor, double t)> actorPosition;
        std::function<std::optional<Vec3>(double t)> cameraPosition;
    };

    struct CompiledDirections
    {
        TransformTrack track;
        std::vector<AnimClip> clips;
        ScalarCurve visibility{1.0};
        double end = 0.0;
        std::vector<std::string> warnings;
    };

    // Turns an actor's action list into a transform track (linear moves,
    // turns at a fixed rate) and animation clips, starting at the actor's
    // rest transform at `actionsStart`. Deterministic.
    CompiledDirections CompileDirections(const ActorDef& actor, const DirectionContext& context);

    // Height of the default character bounds (units), used for framing and picking.
    inline constexpr double kActorHeight = 72.0;
    inline constexpr double kActorRadius = 16.0;
}  // namespace iris
