// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "scene_json.hpp"

#include "json_util.hpp"

namespace iris::detail
{
    namespace
    {
        json TransformJson(const Transform& t)
        {
            json j{{"position", ToJson(t.position)}, {"angles", ToJson(t.angles)}};
            if (t.scale != Vec3{1, 1, 1})
                j["scale"] = ToJson(t.scale);
            return j;
        }

        Transform TransformFrom(const json& j)
        {
            Transform t;
            if (!j.is_object())
                return t;
            t.position = Vec3From(j.value("position", json()));
            t.angles = AnglesFrom(j.value("angles", json()));
            t.scale = Vec3From(j.value("scale", json()), {1, 1, 1});
            return t;
        }

        json TrackJson(const TransformTrack& tr)
        {
            json j = json::object();
            auto put = [&](const char* name, const ScalarCurve& c) {
                if (!c.Empty())
                    j[name] = CurveJson(c);
            };
            put("x", tr.x);
            put("y", tr.y);
            put("z", tr.z);
            put("pitch", tr.pitch);
            put("yaw", tr.yaw);
            put("roll", tr.roll);
            put("scaleX", tr.scaleX);
            put("scaleY", tr.scaleY);
            put("scaleZ", tr.scaleZ);
            return j;
        }

        TransformTrack TrackFrom(const json& j)
        {
            TransformTrack tr;
            if (!j.is_object())
                return tr;
            // Automatic handles are derived data (not stored); recompute them
            // as TransformTrack::SetKey does.
            auto get = [&](const char* name, ScalarCurve& c, double def) {
                if (const auto it = j.find(name); it != j.end())
                {
                    c = CurveFrom(*it, def);
                    c.UpdateAutoHandles();
                }
            };
            get("x", tr.x, 0);
            get("y", tr.y, 0);
            get("z", tr.z, 0);
            get("pitch", tr.pitch, 0);
            get("yaw", tr.yaw, 0);
            get("roll", tr.roll, 0);
            get("scaleX", tr.scaleX, 1);
            get("scaleY", tr.scaleY, 1);
            get("scaleZ", tr.scaleZ, 1);
            return tr;
        }

        json MaterialJson(const MaterialDef& m)
        {
            return {{"preset", m.preset},       {"color", ToJson(m.color)},
                    {"opacity", m.opacity},     {"specular", m.specular},
                    {"gloss", m.gloss},         {"metallic", m.metallic},
                    {"emission", ToJson(m.emission)}, {"emissionStrength", m.emissionStrength}};
        }

        MaterialDef MaterialFrom(const json& j)
        {
            MaterialDef m;
            if (!j.is_object())
                return m;
            m.preset = Get<std::string>(j, "preset", m.preset);
            m.color = Vec3From(j.value("color", json()), m.color);
            m.opacity = Clamp(GetNumber(j, "opacity", m.opacity), 0.0, 1.0);
            m.specular = Clamp(GetNumber(j, "specular", m.specular), 0.0, 1.0);
            m.gloss = Clamp(GetNumber(j, "gloss", m.gloss), 1.0, 256.0);
            m.metallic = Clamp(GetNumber(j, "metallic", m.metallic), 0.0, 1.0);
            m.emission = Vec3From(j.value("emission", json()), m.emission);
            m.emissionStrength = Clamp(GetNumber(j, "emissionStrength", m.emissionStrength), 0.0, 10.0);
            return m;
        }

        json ActorJson(const ActorDef& a)
        {
            json clips = json::array();
            for (const auto& c : a.clips)
                clips.push_back({{"id", c.id}, {"anim", c.anim}, {"start", c.start}, {"duration", c.duration}});
            json attachments = json::array();
            for (const auto& at : a.attachments)
                attachments.push_back({{"model", at.model}, {"tag", at.tag}, {"camo", at.camo}});
            json actions = json::array();
            for (const auto& ac : a.actions)
            {
                json j{{"kind", ToString(ac.kind)}, {"duration", ac.duration}};
                if (ac.target != Vec3{})
                    j["target"] = ToJson(ac.target);
                if (!ac.targetActor.empty())
                    j["targetActor"] = ac.targetActor;
                if (!ac.anim.empty())
                    j["anim"] = ac.anim;
                if (ac.speed > 0)
                    j["speed"] = ac.speed;
                actions.push_back(j);
            }
            return {{"id", a.id},
                    {"name", a.name},
                    {"character", a.character},
                    {"body", a.bodyModel},
                    {"head", a.headModel},
                    {"idleAnim", a.idleAnim},
                    {"weapon", a.weapon},
                    {"weaponTag", a.weaponTag},
                    {"camo", a.camo},
                    {"attachments", attachments},
                    {"transform", TransformJson(a.transform)},
                    {"track", TrackJson(a.track)},
                    {"visibility", CurveJson(a.visibility)},
                    {"clips", clips},
                    {"actions", actions},
                    {"actionsStart", a.actionsStart},
                    {"visible", a.visible},
                    {"locked", a.locked},
                    {"group", a.group}};
        }

        ActorDef ActorFrom(const json& j, std::vector<std::string>& w)
        {
            ActorDef a;
            a.id = Get<std::string>(j, "id", "");
            a.name = Get<std::string>(j, "name", a.id);
            a.character = Get<std::string>(j, "character", "");
            a.bodyModel = Get<std::string>(j, "body", "");
            a.headModel = Get<std::string>(j, "head", "");
            a.idleAnim = Get<std::string>(j, "idleAnim", "");
            a.weapon = Get<std::string>(j, "weapon", "");
            a.weaponTag = Get<std::string>(j, "weaponTag", a.weaponTag);
            a.camo = static_cast<int>(GetNumber(j, "camo", 0));
            if (const auto it = j.find("attachments"); it != j.end() && it->is_array())
                for (const auto& at : *it)
                    if (at.is_object())
                        a.attachments.push_back({Get<std::string>(at, "model", ""), Get<std::string>(at, "tag", "tag_weapon_left"),
                                                 static_cast<int>(GetNumber(at, "camo", 0))});
            a.transform = TransformFrom(j.value("transform", json()));
            a.track = TrackFrom(j.value("track", json()));
            a.visibility = CurveFrom(j.value("visibility", json()), 1.0);
            if (const auto it = j.find("clips"); it != j.end() && it->is_array())
                for (const auto& c : *it)
                    if (c.is_object())
                        a.clips.push_back({Get<std::string>(c, "id", ""), Get<std::string>(c, "anim", ""),
                                           GetNumber(c, "start", 0.0), std::max(GetNumber(c, "duration", 1.0), 0.0)});
            if (const auto it = j.find("actions"); it != j.end() && it->is_array())
                for (const auto& c : *it)
                {
                    if (!c.is_object())
                        continue;
                    const auto kind = ActionKindFromString(Get<std::string>(c, "kind", ""));
                    if (!kind)
                    {
                        w.push_back("Actor '" + a.name + "' has an unknown direction '" + Get<std::string>(c, "kind", "") +
                                    "'; it was skipped.");
                        continue;
                    }
                    ActorAction ac;
                    ac.kind = *kind;
                    ac.duration = GetNumber(c, "duration", ac.duration);
                    ac.target = Vec3From(c.value("target", json()));
                    ac.targetActor = Get<std::string>(c, "targetActor", "");
                    ac.anim = Get<std::string>(c, "anim", "");
                    ac.speed = GetNumber(c, "speed", 0.0);
                    a.actions.push_back(ac);
                }
            a.actionsStart = GetNumber(j, "actionsStart", 0.0);
            a.visible = Get<bool>(j, "visible", true);
            a.locked = Get<bool>(j, "locked", false);
            a.group = Get<std::string>(j, "group", "");
            return a;
        }

        json GraphicJson(const GraphicDef& g)
        {
            const auto& ts = g.textSettings;
            json anims = json::array();
            for (const auto& a : g.animations)
                anims.push_back({{"preset", a.preset},     {"start", a.start},     {"duration", a.duration},
                                 {"stagger", a.stagger},   {"intensity", a.intensity},
                                 {"direction", ToJson(a.direction)}, {"easing", a.easing},
                                 {"seed", a.seed},         {"enabled", a.enabled}});
            return {{"id", g.id},
                    {"name", g.name},
                    {"kind", ToString(g.kind)},
                    {"text", g.text},
                    {"font", g.font},
                    {"textSettings",
                     {{"size", ts.size},
                      {"letterSpacing", ts.letterSpacing},
                      {"wordSpacing", ts.wordSpacing},
                      {"lineSpacing", ts.lineSpacing},
                      {"align", static_cast<int>(ts.align)},
                      {"verticalAlign", static_cast<int>(ts.verticalAlign)},
                      {"layout", static_cast<int>(ts.layout)},
                      {"radius", ts.radius},
                      {"kerning", ts.kerning}}},
                    {"separateWords", g.separateWords},
                    {"source", g.source},
                    {"logoMode", g.logoMode == LogoMode::Flat ? "flat" : "extruded"},
                    {"width", g.width},
                    {"alphaThreshold", g.alphaThreshold},
                    {"simplify", g.simplify},
                    {"smoothing", g.smoothing},
                    {"useLogoColors", g.useLogoColors},
                    {"modelScale", g.modelScale},
                    {"modelUp", g.modelUp == ModelUpAxis::Z ? "z" : "y"},
                    {"shape", ToString(g.shape)},
                    {"shapeSize", ToJson(g.shapeSize)},
                    {"geometry",
                     {{"depth", g.geometry.depth},
                      {"bevel", g.geometry.bevel},
                      {"bevelSegments", g.geometry.bevelSegments},
                      {"detail", g.geometry.detail}}},
                    {"materials",
                     {{"front", MaterialJson(g.materials.front)},
                      {"side", MaterialJson(g.materials.side)},
                      {"bevel", MaterialJson(g.materials.bevel)},
                      {"back", MaterialJson(g.materials.back)},
                      {"separate", g.materials.separate}}},
                    {"doubleSided", g.doubleSided},
                    {"transform", TransformJson(g.transform)},
                    {"track", TrackJson(g.track)},
                    {"opacity", CurveJson(g.opacity)},
                    {"tracking",
                     {{"mode", ToString(g.tracking.mode)},
                      {"actor", g.tracking.actor},
                      {"attach", g.tracking.attach},
                      {"inheritRotation", g.tracking.inheritRotation},
                      {"yawOnly", g.tracking.yawOnly},
                      {"distance", g.tracking.distance}}},
                    {"renderMode", ToString(g.renderMode)},
                    {"animations", anims},
                    {"visible", g.visible},
                    {"locked", g.locked},
                    {"castShadow", g.castShadow},
                    {"group", g.group},
                    {"parent", g.parent}};
        }

        GraphicDef GraphicFrom(const json& j, std::vector<std::string>& w)
        {
            GraphicDef g;
            g.id = Get<std::string>(j, "id", "");
            g.name = Get<std::string>(j, "name", g.id);
            const std::string kind = Get<std::string>(j, "kind", "text");
            if (const auto k = GraphicKindFromString(kind))
                g.kind = *k;
            else
                w.push_back("Graphic '" + g.name + "' has an unknown kind '" + kind + "'; it is treated as text.");
            g.text = Get<std::string>(j, "text", g.text);
            g.font = Get<std::string>(j, "font", "");
            if (const auto it = j.find("textSettings"); it != j.end() && it->is_object())
            {
                auto& ts = g.textSettings;
                const auto& t = *it;
                ts.size = std::max(GetNumber(t, "size", ts.size), 0.01);
                ts.letterSpacing = GetNumber(t, "letterSpacing", ts.letterSpacing);
                ts.wordSpacing = GetNumber(t, "wordSpacing", ts.wordSpacing);
                ts.lineSpacing = GetNumber(t, "lineSpacing", ts.lineSpacing);
                ts.align = static_cast<TextAlign>(Clamp(static_cast<int>(GetNumber(t, "align", 1)), 0, 2));
                ts.verticalAlign = static_cast<TextVerticalAlign>(Clamp(static_cast<int>(GetNumber(t, "verticalAlign", 1)), 0, 3));
                ts.layout = static_cast<TextLayoutKind>(Clamp(static_cast<int>(GetNumber(t, "layout", 0)), 0, 2));
                ts.radius = GetNumber(t, "radius", ts.radius);
                ts.kerning = Get<bool>(t, "kerning", true);
            }
            g.separateWords = Get<bool>(j, "separateWords", false);
            g.source = Get<std::string>(j, "source", "");
            g.logoMode = Get<std::string>(j, "logoMode", "extruded") == "flat" ? LogoMode::Flat : LogoMode::Extruded;
            g.width = std::max(GetNumber(j, "width", g.width), 0.1);
            g.alphaThreshold = Clamp(static_cast<int>(GetNumber(j, "alphaThreshold", 128)), 1, 255);
            g.simplify = GetNumber(j, "simplify", g.simplify);
            g.smoothing = Clamp(static_cast<int>(GetNumber(j, "smoothing", 1)), 0, 4);
            g.useLogoColors = Get<bool>(j, "useLogoColors", true);
            g.modelScale = GetNumber(j, "modelScale", 1.0);
            g.modelUp = Get<std::string>(j, "modelUp", "y") == "z" ? ModelUpAxis::Z : ModelUpAxis::Y;
            g.shape = ShapeKindFromString(Get<std::string>(j, "shape", "box")).value_or(ShapeKind::Box);
            g.shapeSize = Vec3From(j.value("shapeSize", json()), g.shapeSize);
            if (const auto it = j.find("geometry"); it != j.end() && it->is_object())
            {
                g.geometry.depth = std::max(GetNumber(*it, "depth", g.geometry.depth), 0.0);
                g.geometry.bevel = std::max(GetNumber(*it, "bevel", g.geometry.bevel), 0.0);
                g.geometry.bevelSegments = Clamp(static_cast<int>(GetNumber(*it, "bevelSegments", 3)), 1, 8);
                g.geometry.detail = Clamp(GetNumber(*it, "detail", g.geometry.detail), 0.005, 5.0);
            }
            if (const auto it = j.find("materials"); it != j.end() && it->is_object())
            {
                g.materials.front = MaterialFrom(it->value("front", json()));
                g.materials.side = MaterialFrom(it->value("side", json()));
                g.materials.bevel = MaterialFrom(it->value("bevel", json()));
                g.materials.back = MaterialFrom(it->value("back", json()));
                g.materials.separate = Get<bool>(*it, "separate", false);
            }
            g.doubleSided = Get<bool>(j, "doubleSided", false);
            g.transform = TransformFrom(j.value("transform", json()));
            g.track = TrackFrom(j.value("track", json()));
            g.opacity = CurveFrom(j.value("opacity", json()), 1.0);
            if (const auto it = j.find("tracking"); it != j.end() && it->is_object())
            {
                g.tracking.mode = TrackingModeFromString(Get<std::string>(*it, "mode", "world")).value_or(TrackingMode::World);
                g.tracking.actor = Get<std::string>(*it, "actor", "");
                g.tracking.attach = Get<std::string>(*it, "attach", "origin");
                g.tracking.inheritRotation = Get<bool>(*it, "inheritRotation", false);
                g.tracking.yawOnly = Get<bool>(*it, "yawOnly", true);
                g.tracking.distance = GetNumber(*it, "distance", 120.0);
            }
            g.renderMode = GraphicRenderModeFromString(Get<std::string>(j, "renderMode", "depth")).value_or(GraphicRenderMode::Depth);
            if (const auto it = j.find("animations"); it != j.end() && it->is_array())
                for (const auto& a : *it)
                {
                    if (!a.is_object())
                        continue;
                    GraphicAnimation an;
                    an.preset = Get<std::string>(a, "preset", an.preset);
                    an.start = GetNumber(a, "start", 0.0);
                    an.duration = std::max(GetNumber(a, "duration", 1.0), 0.001);
                    an.stagger = GetNumber(a, "stagger", an.stagger);
                    an.intensity = GetNumber(a, "intensity", 1.0);
                    an.direction = Vec3From(a.value("direction", json()), an.direction);
                    an.easing = Get<std::string>(a, "easing", an.easing);
                    an.seed = static_cast<std::uint32_t>(GetNumber(a, "seed", 1));
                    an.enabled = Get<bool>(a, "enabled", true);
                    g.animations.push_back(an);
                }
            g.visible = Get<bool>(j, "visible", true);
            g.locked = Get<bool>(j, "locked", false);
            g.castShadow = Get<bool>(j, "castShadow", false);
            g.group = Get<std::string>(j, "group", "");
            g.parent = Get<std::string>(j, "parent", "");
            return g;
        }
    }  // namespace

    json SceneToJson(const Scene& s)
    {
        json actors = json::array(), props = json::array(), effects = json::array(), graphics = json::array();
        for (const auto& a : s.actors)
            actors.push_back(ActorJson(a));
        for (const auto& p : s.props)
            props.push_back({{"id", p.id},
                             {"name", p.name},
                             {"model", p.model},
                             {"transform", TransformJson(p.transform)},
                             {"track", TrackJson(p.track)},
                             {"visibility", CurveJson(p.visibility)},
                             {"attachActor", p.attachActor},
                             {"attachTag", p.attachTag},
                             {"visible", p.visible},
                             {"locked", p.locked},
                             {"group", p.group}});
        for (const auto& e : s.effects)
            effects.push_back({{"id", e.id},
                               {"name", e.name},
                               {"fx", e.fx},
                               {"transform", TransformJson(e.transform)},
                               {"start", e.start},
                               {"repeat", e.repeat},
                               {"count", e.count},
                               {"attachActor", e.attachActor},
                               {"attachTag", e.attachTag},
                               {"enabled", e.enabled}});
        for (const auto& g : s.graphics)
            graphics.push_back(GraphicJson(g));
        json filmSet{{"gametype", s.filmSet.gametype}};
        if (!s.filmSet.modFolder.empty())
            filmSet["modFolder"] = s.filmSet.modFolder;
        return {{"filmSet", filmSet},
                {"actors", actors},
                {"props", props},
                {"effects", effects},
                {"graphics", graphics}};
    }

    Scene SceneFromJson(const json& j, std::vector<std::string>& w)
    {
        Scene s;
        if (!j.is_object())
            return s;
        if (const auto it = j.find("filmSet"); it != j.end() && it->is_object())
        {
            s.filmSet.gametype = Get<std::string>(*it, "gametype", "dm");
            s.filmSet.modFolder = Get<std::string>(*it, "modFolder", "");
            if (!IsValidModFolder(s.filmSet.modFolder))
            {
                w.push_back("filmSet.modFolder \"" + s.filmSet.modFolder + "\" is not a mod folder; the standard game is used");
                s.filmSet.modFolder.clear();
            }
        }
        if (const auto it = j.find("actors"); it != j.end() && it->is_array())
            for (const auto& a : *it)
                if (a.is_object())
                    s.actors.push_back(ActorFrom(a, w));
        if (const auto it = j.find("props"); it != j.end() && it->is_array())
            for (const auto& p : *it)
                if (p.is_object())
                {
                    PropDef d;
                    d.id = Get<std::string>(p, "id", "");
                    d.name = Get<std::string>(p, "name", d.id);
                    d.model = Get<std::string>(p, "model", "");
                    d.transform = TransformFrom(p.value("transform", json()));
                    d.track = TrackFrom(p.value("track", json()));
                    d.visibility = CurveFrom(p.value("visibility", json()), 1.0);
                    d.attachActor = Get<std::string>(p, "attachActor", "");
                    d.attachTag = Get<std::string>(p, "attachTag", "");
                    d.visible = Get<bool>(p, "visible", true);
                    d.locked = Get<bool>(p, "locked", false);
                    d.group = Get<std::string>(p, "group", "");
                    s.props.push_back(std::move(d));
                }
        if (const auto it = j.find("effects"); it != j.end() && it->is_array())
            for (const auto& e : *it)
                if (e.is_object())
                {
                    EffectDef d;
                    d.id = Get<std::string>(e, "id", "");
                    d.name = Get<std::string>(e, "name", d.id);
                    d.fx = Get<std::string>(e, "fx", "");
                    d.transform = TransformFrom(e.value("transform", json()));
                    d.start = GetNumber(e, "start", 0.0);
                    d.repeat = std::max(GetNumber(e, "repeat", 0.0), 0.0);
                    d.count = std::max(static_cast<int>(GetNumber(e, "count", 1)), 0);
                    d.attachActor = Get<std::string>(e, "attachActor", "");
                    d.attachTag = Get<std::string>(e, "attachTag", "");
                    d.enabled = Get<bool>(e, "enabled", true);
                    s.effects.push_back(std::move(d));
                }
        if (const auto it = j.find("graphics"); it != j.end() && it->is_array())
            for (const auto& g : *it)
                if (g.is_object())
                    s.graphics.push_back(GraphicFrom(g, w));
        return s;
    }
}  // namespace iris::detail
