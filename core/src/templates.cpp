// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/templates.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>

namespace iris
{
    namespace
    {
        Vec3 Forward(double yaw) { return {std::cos(yaw * kDegToRad), std::sin(yaw * kDegToRad), 0}; }
        Vec3 Left(double yaw) { return {-std::sin(yaw * kDegToRad), std::cos(yaw * kDegToRad), 0}; }

        const CharacterEntry* PickCharacter(const TemplateAssets& a, const std::vector<std::string>& categories,
                                            const std::vector<std::string>& tokens = {}, std::size_t skip = 0)
        {
            // Within a category, characters dressed for the map come first:
            // every map loads the ghillie suits of all environments, so the
            // first one by name (arctic) ended up on desert maps.
            const std::string_view environment = MapEnvironment(a);
            auto fit = [&](const CharacterEntry* c) {
                const auto e = CharacterEnvironment(*c);
                return e == environment ? 2 : e.empty() ? 1 : 0;
            };
            std::vector<const CharacterEntry*> ranked;
            for (const auto& cat : categories)
            {
                const std::size_t from = ranked.size();
                for (const auto& c : a.characters)
                    if (c.category == cat &&
                        std::all_of(tokens.begin(), tokens.end(), [&](const std::string& t) { return MatchesSearch(c, t); }))
                        ranked.push_back(&c);
                std::stable_sort(ranked.begin() + static_cast<std::ptrdiff_t>(from), ranked.end(),
                                 [&](const CharacterEntry* x, const CharacterEntry* y) { return fit(x) > fit(y); });
            }
            for (const auto& c : a.characters)
                if (std::find(ranked.begin(), ranked.end(), &c) == ranked.end())
                    ranked.push_back(&c);
            if (ranked.empty())
                return nullptr;
            return ranked[std::min(skip, ranked.size() - 1)];
        }

        void SetActive(Project& p, const std::string& camId)
        {
            p.activeCameraId = camId;
        }

        void AddCut(Project& p, double t, const std::string& camId)
        {
            p.cuts.AddCut({t, camId});
            p.useCameraCuts = true;
        }

        TemplateResult Missing(const std::string& what)
        {
            TemplateResult r;
            r.warnings.push_back(what);
            return r;
        }
    }  // namespace

    std::string_view CharacterEnvironment(const CharacterEntry& character)
    {
        static constexpr std::array<std::string_view, 5> kEnvironments{"arctic", "desert", "forest", "woodland", "urban"};
        std::string text = character.body + " " + character.label;
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        for (const auto e : kEnvironments)
            if (text.find(e) != std::string::npos)
                return e;
        return {};
    }

    std::string_view MapEnvironment(const TemplateAssets& assets)
    {
        // The map's own soldiers tell its environment ("Desert TF141",
        // "OpFor Arctic"); ghillie suits are loaded for every environment
        // and do not count. City maps' soldiers carry no such word.
        std::map<std::string_view, int> votes;
        for (const auto& c : assets.characters)
        {
            std::string body = c.body;
            std::transform(body.begin(), body.end(), body.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (body.find("ghillie") != std::string::npos)
                continue;
            if (const auto e = CharacterEnvironment(c); !e.empty())
                ++votes[e];
        }
        std::string_view best = "urban";
        int most = 0;
        for (const auto& [e, n] : votes)
            if (n > most)
            {
                best = e;
                most = n;
            }
        return best;
    }

    std::string PickWeapon(const std::vector<std::string>& weapons, const std::vector<std::string>& preferences,
                           const std::string& category)
    {
        for (const auto& p : preferences)
            if (std::find(weapons.begin(), weapons.end(), p) != weapons.end())
                return p;
        for (const auto& p : preferences)
        {
            const auto base = p.substr(0, p.find('_'));
            for (const auto& w : weapons)
                if (w.rfind(base + "_", 0) == 0 && !ClassifyWeapon(w).hidden)
                    return w;
        }
        if (!category.empty())
            for (const auto& w : weapons)
            {
                const auto e = ClassifyWeapon(w);
                if (!e.hidden && e.category == category)
                    return w;
            }
        return {};
    }

    AnimationRoles FindAnimationRoles(const std::vector<std::string>& anims)
    {
        AnimationRoles r;
        r.idle = FindAnimation(anims, {{"stand", "alert"}, {"stand", "idle"}, {"stand"}, {"idle"}});
        r.walk = FindAnimation(anims, {{"walk", "forward"}, {"walk", "fwd"}, {"walk"}});
        r.run = FindAnimation(anims, {{"run", "forward"}, {"run", "fwd"}, {"run"}, {"sprint"}});
        r.crouch = FindAnimation(anims, {{"crouch", "alert"}, {"crouch", "idle"}, {"crouch"}, {"crch"}});
        r.aim = FindAnimation(anims, {{"stand", "ads"}, {"ads"}, {"aim"}, {"stand", "alert"}});
        if (r.aim.empty())
            r.aim = r.idle;
        if (r.crouch.empty())
            r.crouch = r.idle;
        return r;
    }

    std::string AddActor(Project& p, const CharacterEntry& c, const Vec3& pos, double yaw, const TemplateAssets& assets,
                         const std::string& weapon)
    {
        ActorDef a;
        a.id = MakeUniqueId(p, "actor");
        a.name = c.label.empty() ? "Actor" : c.label;
        a.character = c.label;
        a.bodyModel = c.body;
        a.headModel = c.head;
        a.idleAnim = FindAnimationRoles(assets.animations).idle;
        a.weapon = weapon;
        a.transform.position = pos;
        a.transform.angles = {0, yaw, 0};
        const std::string id = a.id;
        p.scene.actors.push_back(std::move(a));
        return id;
    }

    std::string AddActorShot(Project& p, const ActorDef& actor, ShotType type, double start, double duration,
                             const ActorDef* secondary)
    {
        ShotSubject subject;
        subject.position = actor.transform.position;
        subject.yaw = actor.transform.angles.yaw;
        subject.height = kActorHeight;
        if (!actor.track.Empty())
        {
            const ActorDef copy = actor;
            subject.positionAt = [copy](double t) { return copy.track.Evaluate(t, copy.transform).position; };
            subject.yaw = actor.track.Evaluate(start, actor.transform).angles.yaw;
            subject.position = subject.positionAt(start);
        }
        std::optional<ShotSubject> second;
        if (secondary)
        {
            ShotSubject s;
            s.position = secondary->track.Evaluate(start, secondary->transform).position;
            s.height = kActorHeight;
            second = s;
        }
        ShotSettings st;
        st.start = start;
        st.duration = duration;
        auto shot = MakeShot(type, subject, st, second);
        shot.camera.id = MakeUniqueId(p, "cam");
        shot.camera.name = ShotLabel(type) + " - " + actor.name;
        const std::string id = shot.camera.id;
        p.cameras.push_back(std::move(shot.camera));
        return id;
    }

    std::string AddGraphicOrbit(Project& p, const GraphicDef& g, const Bounds3D& bounds, double start, double duration)
    {
        ShotSubject subject;
        const Vec3 size = bounds.Valid() ? bounds.Size() : Vec3{100, 40, 10};
        // The graphic faces along its forward axis; frame its height (graphic y)
        // and keep the whole width in view.
        subject.height = std::max(size.y, size.x * 9.0 / 16.0) * 1.4;
        subject.position = g.transform.position - Vec3{0, 0, subject.height * 0.5 - size.y * 0.5};
        subject.yaw = g.transform.angles.yaw;
        ShotSettings st;
        st.start = start;
        st.duration = duration;
        auto shot = MakeShot(ShotType::Orbit, subject, st);
        shot.camera.id = MakeUniqueId(p, "cam");
        shot.camera.name = "Orbit - " + g.name;
        const std::string id = shot.camera.id;
        p.cameras.push_back(std::move(shot.camera));
        return id;
    }

    const std::vector<TemplateInfo>& SceneTemplates()
    {
        static const std::vector<TemplateInfo> t{
            {"empty", "Empty film set", "The map with a free camera and a dolly camera, nothing else."},
            {"sniperShowcase", "Sniper showcase", "A ghillie sniper with a sniper rifle, aiming; close-up, orbit and cuts."},
            {"trickshot", "Trickshot sequence", "A player in motion with a slow-motion ramp and an orbit."},
            {"confrontation", "Two-character confrontation", "Two characters facing each other, over-the-shoulder shots."},
            {"patrol", "Military patrol", "Two soldiers walking a path, tracking shot."},
            {"squadIntro", "Squad introduction", "Three characters in a line, dolly past them with cuts."},
            {"rooftop", "Rooftop cinematic", "A character on the spot with a crane shot and golden-hour look."},
            {"slowMotion", "Slow-motion action", "A running character with a speed ramp into slow motion."},
            {"weaponShowcase", "Weapon showcase", "A character holding a weapon, weapon showcase and close-up."},
            {"flythrough", "Map flythrough", "A wide camera path through the map, no actors."},
        };
        return t;
    }

    TemplateResult ApplySceneTemplate(Project& p, std::string_view id, const Vec3& c, double yaw, const TemplateAssets& a,
                                      double start)
    {
        TemplateResult r;
        const Vec3 f = Forward(yaw), l = Left(yaw);
        const AnimationRoles roles = FindAnimationRoles(a.animations);
        auto needCharacter = [&]() -> bool {
            if (a.characters.empty())
            {
                r.warnings.push_back("This map has no character models loaded; the template only adds cameras.");
                return false;
            }
            return true;
        };
        // The returned pointer is valid until the next actor is added.
        auto actor = [&](const std::vector<std::string>& cats, const Vec3& pos, double actorYaw, const std::string& weapon,
                         std::size_t skip = 0, const std::vector<std::string>& tokens = {}) -> ActorDef* {
            const CharacterEntry* ch = PickCharacter(a, cats, tokens, skip);
            if (!ch)
                return nullptr;
            const std::string aid = AddActor(p, *ch, pos, actorYaw, a, weapon);
            r.created.push_back(aid);
            return p.scene.FindActor(aid);
        };
        auto shot = [&](ActorDef* subject, ShotType type, double s, double d, const ActorDef* second = nullptr) {
            if (!subject)
                return std::string();
            const auto camId = AddActorShot(p, *subject, type, s, d, second);
            r.created.push_back(camId);
            return camId;
        };
        const std::string rifle = PickWeapon(a.weapons, {"m4_mp", "scar_mp", "ak47_mp", "masada_mp"}, "Assault rifles");
        const std::string sniper = PickWeapon(a.weapons, {"cheytac_mp", "barrett_mp", "wa2000_mp", "m21_mp"}, "Sniper rifles");
        if (!a.weapons.empty() && sniper.empty())
            r.warnings.push_back("No sniper rifle is available on this map; actors stay unarmed or use a rifle.");

        if (id == "empty")
        {
            CameraDef cam;
            cam.id = MakeUniqueId(p, "cam");
            cam.name = "Dolly";
            cam.mode = CameraMode::Dolly;
            p.cameras.push_back(cam);
            r.created.push_back(cam.id);
            return r;
        }
        if (id == "flythrough")
        {
            CameraDef cam;
            cam.id = MakeUniqueId(p, "cam");
            cam.name = "Flythrough";
            cam.mode = CameraMode::Dolly;
            cam.lens.focalLengthMm = 20;
            const double dur = 12.0;
            for (int i = 0; i <= 6; ++i)
            {
                CameraKey k;
                k.time = start + dur * i / 6.0;
                const double ang = yaw + 180.0 + 60.0 * i / 6.0;
                k.position = c + Forward(ang) * (900.0 - 80.0 * i) + Vec3{0, 0, 420.0 - 40.0 * i};
                k.angles = LookAt(k.position, c + Vec3{0, 0, 80});
                k.horizontalFov = cam.lens.HorizontalFov();
                cam.path.AddKey(k);
            }
            cam.path.SetConstantSpeed(true);
            p.cameras.push_back(cam);
            SetActive(p, cam.id);
            r.created.push_back(cam.id);
            return r;
        }
        if (!needCharacter())
        {
            auto fallback = ApplySceneTemplate(p, "empty", c, yaw, a, start);
            fallback.warnings.insert(fallback.warnings.begin(), r.warnings.begin(), r.warnings.end());
            return fallback;
        }

        if (id == "sniperShowcase")
        {
            ActorDef* s = actor({"Ghillie suits"}, c, yaw, sniper.empty() ? rifle : sniper, 0, {});
            if (s && !roles.aim.empty())
                s->clips.push_back({"clip-1", roles.aim, start, 6.0});
            const auto c1 = shot(s, ShotType::CloseUp, start, 3.0);
            const auto c2 = shot(s, ShotType::Orbit, start + 3.0, 3.0);
            if (!c1.empty())
            {
                AddCut(p, start, c1);
                AddCut(p, start + 3.0, c2);
                SetActive(p, c1);
            }
            Marker m;
            m.id = MakeUniqueId(p, "marker");
            m.time = start;
            m.name = "Sniper reveal";
            p.markers.push_back(m);
            return r;
        }
        if (id == "trickshot" || id == "slowMotion")
        {
            ActorDef* s = actor({"Task Force 141", "Rangers", "Navy SEALs"}, c, yaw, sniper.empty() ? rifle : sniper);
            if (s)
            {
                s->actionsStart = start;
                ActorAction wait;
                wait.kind = ActionKind::Wait;
                wait.duration = 0.6;
                s->actions.push_back(wait);
                ActorAction run;
                run.kind = ActionKind::RunTo;
                run.target = c + f * 380.0;
                s->actions.push_back(run);
                DirectionContext ctx;
                ctx.roles = roles;
                const auto compiled = CompileDirections(*s, ctx);
                s->track = compiled.track;
                s->clips = compiled.clips;
                for (const auto& w : compiled.warnings)
                    r.warnings.push_back(w);
            }
            const auto camId = shot(s, id == "trickshot" ? ShotType::Orbit : ShotType::Tracking, start, 4.0);
            if (!camId.empty())
                SetActive(p, camId);
            // Speed ramp: real time, down to 25 % at the moment, back up.
            auto& speed = p.timeRemap.Speed();
            for (const auto& [t, v] : {std::pair{start, 1.0}, std::pair{start + 1.2, 0.25}, std::pair{start + 2.4, 0.25},
                                       std::pair{start + 3.2, 1.0}})
            {
                ScalarKey k;
                k.time = t;
                k.value = v;
                speed.AddKey(k);
            }
            speed.UpdateAutoHandles();
            return r;
        }
        if (id == "confrontation")
        {
            // Pointers into scene.actors are only valid until the next actor
            // is added (the vector may reallocate): keep ids, look them up.
            const ActorDef* first = actor({"Task Force 141", "Rangers"}, c - f * 90.0, yaw, rifle, 0);
            const std::string id1 = first ? first->id : std::string();
            const ActorDef* second = actor({"Spetsnaz", "OpFor", "Militia"}, c + f * 90.0, yaw + 180.0, rifle, 0);
            const std::string id2 = second ? second->id : std::string();
            ActorDef* a1 = id1.empty() ? nullptr : p.scene.FindActor(id1);
            ActorDef* a2 = id2.empty() ? nullptr : p.scene.FindActor(id2);
            if (a1 && a2)
            {
                const auto c1 = shot(a1, ShotType::OverShoulder, start, 3.0, a2);
                a1 = p.scene.FindActor(id1);
                a2 = p.scene.FindActor(id2);
                const auto c2 = shot(a2, ShotType::OverShoulder, start + 3.0, 3.0, a1);
                AddCut(p, start, c1);
                AddCut(p, start + 3.0, c2);
                SetActive(p, c1);
            }
            return r;
        }
        if (id == "patrol")
        {
            for (int i = 0; i < 2; ++i)
            {
                ActorDef* s = actor({"Task Force 141", "Rangers", "Navy SEALs"}, c - f * (60.0 * i) + l * (30.0 * i), yaw,
                                    rifle, static_cast<std::size_t>(i));
                if (!s)
                    continue;
                s->actionsStart = start;
                ActorAction walk;
                walk.kind = ActionKind::WalkTo;
                walk.target = s->transform.position + f * 400.0;
                s->actions.push_back(walk);
                DirectionContext ctx;
                ctx.roles = roles;
                const auto compiled = CompileDirections(*s, ctx);
                s->track = compiled.track;
                s->clips = compiled.clips;
            }
            if (!r.created.empty())
                if (ActorDef* lead = p.scene.FindActor(r.created[0]))
                {
                    const auto camId = shot(lead, ShotType::Tracking, start, 5.0);
                    SetActive(p, camId);
                }
            return r;
        }
        if (id == "squadIntro")
        {
            std::vector<std::string> ids;
            for (int i = 0; i < 3; ++i)
                if (ActorDef* s = actor({"Task Force 141", "Navy SEALs", "Rangers"}, c + l * (60.0 * (i - 1)), yaw, rifle,
                                        static_cast<std::size_t>(i)))
                    ids.push_back(s->id);
            double t = start;
            bool first = true;
            for (const auto& aid : ids)
            {
                const auto camId = shot(p.scene.FindActor(aid), ShotType::Medium, t, 2.0);
                AddCut(p, t, camId);
                if (first)
                    SetActive(p, camId);
                first = false;
                t += 2.0;
            }
            return r;
        }
        if (id == "rooftop")
        {
            ActorDef* s = actor({"Ghillie suits", "Task Force 141"}, c, yaw, sniper.empty() ? rifle : sniper);
            const auto camId = shot(s, ShotType::Crane, start, 6.0);
            if (!camId.empty())
                SetActive(p, camId);
            p.visuals.filmOverride = true;
            p.visuals.filmLightTint = {1.12, 0.98, 0.82};
            p.visuals.filmDarkTint = {0.95, 0.9, 1.05};
            p.visuals.filmContrast = 1.15;
            return r;
        }
        if (id == "weaponShowcase")
        {
            ActorDef* s = actor({"Task Force 141", "Rangers"}, c, yaw, sniper.empty() ? rifle : sniper);
            const auto c1 = shot(s, ShotType::WeaponShowcase, start, 3.0);
            const auto c2 = shot(s, ShotType::CloseUp, start + 3.0, 2.0);
            if (!c1.empty())
            {
                AddCut(p, start, c1);
                AddCut(p, start + 3.0, c2);
                SetActive(p, c1);
            }
            return r;
        }
        return Missing("Unknown template.");
    }

    const std::vector<TemplateInfo>& TitleTemplates()
    {
        static const std::vector<TemplateInfo> t{
            {"montageTitle", "Montage title", "Large extruded title, letters fly in one after another."},
            {"playerIntro", "Player introduction", "Name rises letter by letter, then floats."},
            {"teamIntro", "Team introduction", "Metallic title that assembles from pieces."},
            {"mapTitle", "Map title", "Concrete title, typewriter reveal."},
            {"episodeTitle", "Episode title", "Clean white title with a fade and a slow drift."},
            {"chapter", "Cinematic chapter", "Small title that pops in and flickers."},
            {"trickshotHighlight", "Trickshot highlight", "Neon title that pulses."},
            {"floatingName", "Floating player name", "A name that floats above the subject and faces the camera."},
            {"openingCredits", "Opening credits", "Two lines that fade in, hold and fade out."},
            {"endingCredits", "Ending credits", "Lines that rise and fade out."},
        };
        return t;
    }

    TemplateResult ApplyTitleTemplate(Project& p, std::string_view id, const std::string& text, const Vec3& pos, double yaw,
                                      const std::string& font, double start)
    {
        TemplateResult r;
        GraphicDef g;
        g.id = MakeUniqueId(p, "gfx");
        g.kind = GraphicKind::Text;
        g.text = text.empty() ? "IW4IRIS" : text;
        g.name = g.text;
        g.font = font;
        g.transform.position = pos;
        g.transform.angles = {0, yaw, 0};
        GraphicAnimation in;
        in.start = start;
        auto material = [&](const char* preset) {
            g.materials.front = MakeMaterial(preset);
        };
        if (id == "montageTitle")
        {
            g.textSettings.size = 48;
            g.geometry.depth = 10;
            material("polished_metal");
            in.preset = "flyIn";
            in.duration = 1.2;
            in.stagger = 0.06;
            in.direction = {0, 0, 1};
        }
        else if (id == "playerIntro")
        {
            g.textSettings.size = 28;
            material("matte_white");
            in.preset = "rise";
            in.duration = 0.8;
            in.stagger = 0.05;
            GraphicAnimation fl;
            fl.preset = "float";
            fl.start = start + 1.0;
            fl.duration = 3.0;
            fl.intensity = 0.6;
            g.animations.push_back(fl);
        }
        else if (id == "teamIntro")
        {
            g.textSettings.size = 56;
            g.geometry.depth = 12;
            material("gold");
            in.preset = "assemble";
            in.duration = 1.8;
            in.stagger = 0.03;
        }
        else if (id == "mapTitle")
        {
            g.textSettings.size = 64;
            g.geometry.depth = 16;
            material("concrete");
            in.preset = "typewriter";
            in.duration = 0.1;
            in.stagger = 0.09;
        }
        else if (id == "episodeTitle")
        {
            g.textSettings.size = 36;
            material("matte_white");
            in.preset = "fadeIn";
            in.duration = 1.5;
            in.stagger = 0.0;
            GraphicAnimation fl;
            fl.preset = "float";
            fl.start = start;
            fl.duration = 6.0;
            fl.intensity = 0.4;
            g.animations.push_back(fl);
        }
        else if (id == "chapter")
        {
            g.textSettings.size = 20;
            material("painted_steel");
            in.preset = "popIn";
            in.duration = 0.5;
            in.stagger = 0.03;
            GraphicAnimation fk;
            fk.preset = "flicker";
            fk.start = start + 0.6;
            fk.duration = 0.6;
            g.animations.push_back(fk);
        }
        else if (id == "trickshotHighlight")
        {
            g.textSettings.size = 40;
            material("neon");
            in.preset = "scaleIn";
            in.duration = 0.4;
            in.easing = "easeOutBack";
            GraphicAnimation pu;
            pu.preset = "pulse";
            pu.start = start + 0.4;
            pu.duration = 0.5;
            g.animations.push_back(pu);
        }
        else if (id == "floatingName")
        {
            g.textSettings.size = 10;
            g.geometry.depth = 1.5;
            g.geometry.bevel = 0.15;
            material("matte_white");
            g.tracking.mode = TrackingMode::Billboard;
            in.preset = "fadeIn";
            in.duration = 0.5;
            in.stagger = 0.0;
        }
        else if (id == "openingCredits" || id == "endingCredits")
        {
            g.textSettings.size = 18;
            g.geometry.depth = 2;
            material("matte_white");
            if (g.text.find('\n') == std::string::npos)
                g.text += "\nA cinematic by";
            in.preset = id == "openingCredits" ? "fadeIn" : "rise";
            in.duration = 1.0;
            in.stagger = id == "openingCredits" ? 0.0 : 0.02;
            GraphicAnimation out;
            out.preset = "fadeOut";
            out.start = start + 4.0;
            out.duration = 1.0;
            out.stagger = 0.0;
            g.animations.push_back(out);
        }
        else
            return Missing("Unknown title template.");
        g.animations.insert(g.animations.begin(), in);
        r.created.push_back(g.id);
        p.scene.graphics.push_back(std::move(g));
        return r;
    }
}  // namespace iris
