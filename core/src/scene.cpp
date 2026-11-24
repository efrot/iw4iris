// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/scene.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace iris
{
    std::string_view ToString(ActionKind kind)
    {
        switch (kind)
        {
            case ActionKind::Wait: return "wait";
            case ActionKind::WalkTo: return "walkTo";
            case ActionKind::RunTo: return "runTo";
            case ActionKind::TurnTo: return "turnTo";
            case ActionKind::FaceActor: return "faceActor";
            case ActionKind::FaceCamera: return "faceCamera";
            case ActionKind::PlayAnimation: return "playAnimation";
            case ActionKind::Crouch: return "crouch";
            case ActionKind::AimAt: return "aimAt";
            case ActionKind::Enter: return "enter";
            case ActionKind::Exit: return "exit";
            case ActionKind::FollowActor: return "followActor";
        }
        return "wait";
    }

    std::optional<ActionKind> ActionKindFromString(std::string_view s)
    {
        for (int i = 0; i <= static_cast<int>(ActionKind::FollowActor); ++i)
            if (ToString(static_cast<ActionKind>(i)) == s)
                return static_cast<ActionKind>(i);
        return std::nullopt;
    }

    std::string Describe(ActionKind kind)
    {
        switch (kind)
        {
            case ActionKind::Wait: return "Stand still";
            case ActionKind::WalkTo: return "Walk to a point";
            case ActionKind::RunTo: return "Run to a point";
            case ActionKind::TurnTo: return "Turn towards a point";
            case ActionKind::FaceActor: return "Turn towards another actor";
            case ActionKind::FaceCamera: return "Turn towards the camera";
            case ActionKind::PlayAnimation: return "Play an animation";
            case ActionKind::Crouch: return "Crouch";
            case ActionKind::AimAt: return "Aim at a point";
            case ActionKind::Enter: return "Enter the scene";
            case ActionKind::Exit: return "Leave the scene";
            case ActionKind::FollowActor: return "Follow another actor";
        }
        return "Stand still";
    }

    namespace
    {
        bool SameCurve(const ScalarCurve& a, const ScalarCurve& b)
        {
            return a.Keys() == b.Keys() && a.DefaultValue() == b.DefaultValue();
        }

        template <typename T>
        T* FindById(std::vector<T>& v, std::string_view id)
        {
            for (auto& x : v)
                if (x.id == id)
                    return &x;
            return nullptr;
        }

        void AddUnique(std::vector<std::string>& v, const std::string& s)
        {
            if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end())
                v.push_back(s);
        }

        bool IsWeaponName(const std::string& s)
        {
            return s.size() > 3 && s.compare(s.size() - 3, 3, "_mp") == 0;
        }
    }  // namespace

    bool ActorDef::operator==(const ActorDef& o) const
    {
        return id == o.id && name == o.name && character == o.character && bodyModel == o.bodyModel &&
               headModel == o.headModel && idleAnim == o.idleAnim && weapon == o.weapon && weaponTag == o.weaponTag &&
               camo == o.camo && attachments == o.attachments && transform == o.transform && track == o.track &&
               SameCurve(visibility, o.visibility) && clips == o.clips && actions == o.actions &&
               actionsStart == o.actionsStart && visible == o.visible && locked == o.locked && group == o.group;
    }

    bool PropDef::operator==(const PropDef& o) const
    {
        return id == o.id && name == o.name && model == o.model && transform == o.transform && track == o.track &&
               SameCurve(visibility, o.visibility) && attachActor == o.attachActor && attachTag == o.attachTag &&
               visible == o.visible && locked == o.locked && group == o.group;
    }

    ActorDef* Scene::FindActor(std::string_view id) { return FindById(actors, id); }
    const ActorDef* Scene::FindActor(std::string_view id) const
    {
        return const_cast<Scene*>(this)->FindActor(id);
    }
    PropDef* Scene::FindProp(std::string_view id) { return FindById(props, id); }
    EffectDef* Scene::FindEffect(std::string_view id) { return FindById(effects, id); }
    GraphicDef* Scene::FindGraphic(std::string_view id) { return FindById(graphics, id); }
    const GraphicDef* Scene::FindGraphic(std::string_view id) const
    {
        return const_cast<Scene*>(this)->FindGraphic(id);
    }

    bool Scene::Contains(std::string_view id) const
    {
        auto has = [&](const auto& v) {
            return std::any_of(v.begin(), v.end(), [&](const auto& x) { return x.id == id; });
        };
        return has(actors) || has(props) || has(effects) || has(graphics);
    }

    std::vector<std::string> Scene::ReferencedModels() const
    {
        std::vector<std::string> out;
        for (const auto& a : actors)
        {
            AddUnique(out, a.bodyModel);
            AddUnique(out, a.headModel);
            for (const auto& at : a.attachments)
                if (!IsWeaponName(at.model))
                    AddUnique(out, at.model);
        }
        for (const auto& p : props)
            AddUnique(out, p.model);
        return out;
    }

    std::vector<std::string> Scene::ReferencedAnimations() const
    {
        std::vector<std::string> out;
        for (const auto& a : actors)
        {
            AddUnique(out, a.idleAnim);
            for (const auto& c : a.clips)
                AddUnique(out, c.anim);
        }
        return out;
    }

    std::vector<std::string> Scene::ReferencedWeapons() const
    {
        std::vector<std::string> out;
        for (const auto& a : actors)
        {
            AddUnique(out, a.weapon);
            for (const auto& at : a.attachments)
                if (IsWeaponName(at.model))
                    AddUnique(out, at.model);
        }
        return out;
    }

    std::vector<std::string> Scene::ReferencedEffects() const
    {
        std::vector<std::string> out;
        for (const auto& e : effects)
            AddUnique(out, e.fx);
        return out;
    }

    double Scene::ContentEnd() const
    {
        double end = 0.0;
        auto track = [&](const TransformTrack& tr) {
            for (const double t : tr.KeyTimes())
                end = std::max(end, t);
        };
        for (const auto& a : actors)
        {
            track(a.track);
            for (const auto& c : a.clips)
                end = std::max(end, c.End());
        }
        for (const auto& p : props)
            track(p.track);
        for (const auto& e : effects)
            end = std::max(end, e.start + (e.repeat > 0 && e.count > 1 ? e.repeat * (e.count - 1) : 0.0));
        for (const auto& g : graphics)
        {
            track(g.track);
            for (const auto& a : g.animations)
                end = std::max(end, a.start + a.duration);
        }
        return end;
    }

    // ---------------------------------------------------------------- evaluation

    ActorState EvaluateActor(const ActorDef& a, double t)
    {
        ActorState s;
        s.id = a.id;
        s.transform = a.track.Evaluate(t, a.transform);
        s.visible = a.visible && (a.visibility.Empty() ? a.visibility.DefaultValue() : a.visibility.Evaluate(t)) > 0.5;
        // The latest-starting clip that covers t wins.
        const AnimClip* active = nullptr;
        for (const auto& c : a.clips)
            if (t >= c.start && t < c.End() && (!active || c.start >= active->start))
                active = &c;
        if (active)
        {
            s.anim = active->anim;
            s.animStart = active->start;
            s.clipId = active->id;
        }
        else
        {
            s.anim = a.idleAnim;
            // The idle animation (re)starts when the last clip before t ended.
            double since = 0.0;
            for (const auto& c : a.clips)
                if (c.End() <= t)
                    since = std::max(since, c.End());
            s.animStart = since;
        }
        return s;
    }

    PropState EvaluateProp(const PropDef& p, double t)
    {
        PropState s;
        s.id = p.id;
        s.transform = p.track.Evaluate(t, p.transform);
        s.visible = p.visible && (p.visibility.Empty() ? p.visibility.DefaultValue() : p.visibility.Evaluate(t)) > 0.5;
        return s;
    }

    std::vector<EffectEvent> EffectEventsBetween(const Scene& scene, double t0, double t1)
    {
        std::vector<EffectEvent> events;
        if (t1 <= t0)
            return events;
        for (const auto& e : scene.effects)
        {
            if (!e.enabled || e.fx.empty())
                continue;
            if (e.repeat <= 0.0)
            {
                if (e.start > t0 && e.start <= t1)
                    events.push_back({&e, e.start});
                continue;
            }
            // Plays at start + k * repeat for k in [0, count).
            const double kFirst = std::max(0.0, std::floor((t0 - e.start) / e.repeat) + 1.0);
            for (double k = kFirst;; k += 1.0)
            {
                if (e.count > 0 && k >= e.count)
                    break;
                const double when = e.start + k * e.repeat;
                if (when > t1)
                    break;
                if (when > t0)
                    events.push_back({&e, when});
            }
        }
        std::sort(events.begin(), events.end(), [](const EffectEvent& a, const EffectEvent& b) { return a.time < b.time; });
        return events;
    }

    // ---------------------------------------------------------------- directions

    namespace
    {
        double YawTowards(const Vec3& from, const Vec3& to, double fallback)
        {
            const Vec3 d = to - from;
            if (std::abs(d.x) < 1e-6 && std::abs(d.y) < 1e-6)
                return fallback;
            return std::atan2(d.y, d.x) * kRadToDeg;
        }

        struct Builder
        {
            CompiledDirections out;
            double t;
            Vec3 pos;
            double yaw;
            EulerAngles angles;
            int clipCounter = 0;

            void Key(Interpolation interp = Interpolation::Linear)
            {
                Transform tr;
                tr.position = pos;
                tr.angles = {angles.pitch, yaw, angles.roll};
                out.track.SetKey(t, tr, interp);
            }

            void Clip(const std::string& anim, double start, double duration)
            {
                if (anim.empty() || duration <= 0.0)
                    return;
                out.clips.push_back({"clip-" + std::to_string(++clipCounter), anim, start, duration});
            }

            // Turn on the spot to `target` yaw at the turn rate.
            void Turn(double targetYaw, double rate, const std::string& idle)
            {
                const double to = UnwrapDegreesNear(targetYaw, yaw);
                const double delta = std::abs(to - yaw);
                if (delta < 0.5)
                    return;
                const double dur = std::max(delta / std::max(rate, 1.0), 0.1);
                Key();
                Clip(idle, t, dur);
                t += dur;
                yaw = to;
                Key();
            }
        };
    }  // namespace

    CompiledDirections CompileDirections(const ActorDef& actor, const DirectionContext& ctx)
    {
        Builder b{CompiledDirections{}, actor.actionsStart, actor.transform.position, actor.transform.angles.yaw,
                  actor.transform.angles};
        auto& w = b.out.warnings;
        auto pick = [](const std::string& override_, const std::string& role) {
            return override_.empty() ? role : override_;
        };
        b.Key();
        for (const auto& a : actor.actions)
        {
            switch (a.kind)
            {
                case ActionKind::Wait:
                {
                    const double d = std::max(a.duration, 0.0);
                    b.Clip(pick(a.anim, ctx.roles.idle), b.t, d);
                    b.t += d;
                    b.Key();
                    break;
                }
                case ActionKind::WalkTo:
                case ActionKind::RunTo:
                {
                    const bool run = a.kind == ActionKind::RunTo;
                    const double speed = a.speed > 0 ? a.speed : (run ? ctx.runSpeed : ctx.walkSpeed);
                    const Vec3 target{a.target.x, a.target.y, a.target.z};
                    const double dist = Distance(b.pos, target);
                    if (dist < 1.0)
                    {
                        w.push_back("A move action targets the actor's current position and was skipped.");
                        break;
                    }
                    b.Turn(YawTowards(b.pos, target, b.yaw), ctx.turnRate, ctx.roles.idle);
                    const double dur = dist / std::max(speed, 1.0);
                    b.Key();
                    b.Clip(pick(a.anim, run ? ctx.roles.run : ctx.roles.walk), b.t, dur);
                    b.t += dur;
                    b.pos = target;
                    b.Key();
                    break;
                }
                case ActionKind::TurnTo:
                    b.Turn(YawTowards(b.pos, a.target, b.yaw), ctx.turnRate, pick(a.anim, ctx.roles.idle));
                    break;
                case ActionKind::FaceActor:
                {
                    const auto p = ctx.actorPosition ? ctx.actorPosition(a.targetActor, b.t) : std::nullopt;
                    if (!p)
                    {
                        w.push_back("\"Turn towards another actor\" names an actor that does not exist.");
                        break;
                    }
                    b.Turn(YawTowards(b.pos, *p, b.yaw), ctx.turnRate, pick(a.anim, ctx.roles.idle));
                    break;
                }
                case ActionKind::FaceCamera:
                {
                    const auto p = ctx.cameraPosition ? ctx.cameraPosition(b.t) : std::nullopt;
                    if (!p)
                    {
                        w.push_back("\"Turn towards the camera\" needs a shot camera.");
                        break;
                    }
                    b.Turn(YawTowards(b.pos, *p, b.yaw), ctx.turnRate, pick(a.anim, ctx.roles.idle));
                    break;
                }
                case ActionKind::PlayAnimation:
                {
                    if (a.anim.empty())
                    {
                        w.push_back("\"Play an animation\" has no animation selected.");
                        break;
                    }
                    const double d = std::max(a.duration, 0.05);
                    b.Clip(a.anim, b.t, d);
                    b.t += d;
                    b.Key();
                    break;
                }
                case ActionKind::Crouch:
                {
                    const double d = std::max(a.duration, 0.05);
                    b.Clip(pick(a.anim, ctx.roles.crouch), b.t, d);
                    b.t += d;
                    b.Key();
                    break;
                }
                case ActionKind::AimAt:
                {
                    b.Turn(YawTowards(b.pos, a.target, b.yaw), ctx.turnRate, ctx.roles.idle);
                    const double d = std::max(a.duration, 0.05);
                    b.Clip(pick(a.anim, ctx.roles.aim), b.t, d);
                    b.t += d;
                    b.Key();
                    break;
                }
                case ActionKind::Enter:
                case ActionKind::Exit:
                {
                    ScalarKey k;
                    k.time = b.t;
                    k.value = a.kind == ActionKind::Enter ? 1.0 : 0.0;
                    k.interpolation = Interpolation::Hold;
                    if (a.kind == ActionKind::Enter && b.out.visibility.Empty() && b.t > actor.actionsStart)
                    {
                        // Hidden until it enters.
                        ScalarKey before = k;
                        before.time = actor.actionsStart;
                        before.value = 0.0;
                        b.out.visibility.AddKey(before);
                    }
                    if (a.kind == ActionKind::Exit && b.out.visibility.Empty() && b.t > actor.actionsStart)
                    {
                        // Visible until it leaves (a curve holds its first key before it).
                        ScalarKey before = k;
                        before.time = actor.actionsStart;
                        before.value = 1.0;
                        b.out.visibility.AddKey(before);
                    }
                    b.out.visibility.AddKey(k);
                    break;
                }
                case ActionKind::FollowActor:
                {
                    if (!ctx.actorPosition || !ctx.actorPosition(a.targetActor, b.t))
                    {
                        w.push_back("\"Follow another actor\" names an actor that does not exist.");
                        break;
                    }
                    const double d = std::max(a.duration, 0.1);
                    const double step = 0.25;
                    const double start = b.t;
                    Vec3 prevTarget = *ctx.actorPosition(a.targetActor, start);
                    for (double s = step; s <= d + 1e-9; s += step)
                    {
                        const Vec3 target = *ctx.actorPosition(a.targetActor, start + s);
                        const Vec3 motion = target - prevTarget;
                        Vec3 away = b.pos - target;
                        away.z = 0;
                        // Stay `followDistance` behind the target.
                        Vec3 goal = target + (Length(away) > 1e-6 ? Normalize(away) : Normalize(Vec3{-motion.x, -motion.y, 0})) *
                                                 ctx.followDistance;
                        if (Distance(b.pos, target) < ctx.followDistance)
                            goal = b.pos;
                        b.t = start + s;
                        b.yaw = UnwrapDegreesNear(YawTowards(b.pos, target, b.yaw), b.yaw);
                        b.pos = goal;
                        b.Key();
                        prevTarget = target;
                    }
                    b.Clip(pick(a.anim, ctx.roles.walk), start, d);
                    b.t = start + d;
                    break;
                }
            }
        }
        b.out.end = b.t;
        return b.out;
    }
}  // namespace iris
