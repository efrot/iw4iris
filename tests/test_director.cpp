// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Director Mode and motion graphics: projection, graphics evaluation and
// geometry, scene evaluation, directions, the asset library, shots and
// templates.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include "iris/assets.hpp"
#include "iris/graphics.hpp"
#include "iris/projection.hpp"
#include "iris/scene.hpp"
#include "iris/shots.hpp"
#include "iris/templates.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    CameraState Camera(const Vec3& pos, const EulerAngles& ang, double fov = 90.0)
    {
        CameraState c;
        c.position = pos;
        c.angles = ang;
        c.horizontalFov = fov;
        return c;
    }

    FontResolver Rubik()
    {
        return [](const std::string&, std::string* error) {
            return Font::Load(std::filesystem::path(IW4IRIS_SOURCE_DIR) / "resources/fonts/Rubik-SemiBold.ttf", error);
        };
    }

    TemplateAssets FakeAssets()
    {
        TemplateAssets a;
        a.characters = BuildCharacters({"mp_body_ghillie_urban_sniper", "head_ghillie_urban", "mp_body_tf141_lmg",
                                        "head_tf141_lmg", "mp_body_opforce_russian_smg", "head_opforce_russian_a",
                                        "mp_body_us_army_assault_a", "head_us_army_a"});
        a.animations = {"pb_stand_alert", "pb_walk_forward_mg", "pb_run_forward", "pb_crouch_alert", "pb_stand_ads",
                        "pb_stand_death_chest_blowback"};
        a.weapons = {"m4_mp", "cheytac_mp", "cheytac_acog_mp", "ak47_mp", "turret_minigun_mp"};
        return a;
    }
}  // namespace

TEST_CASE("projection: points ahead, left and up land where the view puts them")
{
    const auto view = MakeViewParams(Camera({0, 0, 0}, {0, 0, 0}), 16.0 / 9.0);
    const auto c = ProjectToScreen(view, {100, 0, 0}, 1920, 1080);
    CHECK(c.has_value());
    CHECK_NEAR(c->x, 960.0, 1e-9);
    CHECK_NEAR(c->y, 540.0, 1e-9);
    CHECK_NEAR(c->viewDepth, 100.0, 1e-9);
    const auto left = ProjectToScreen(view, {100, 50, 0}, 1920, 1080);
    CHECK(left->x < 960.0);
    CHECK_NEAR(left->x, 960.0 - 0.5 * 960.0, 1e-6);  // 90 degree FOV: tan 45 = 1
    const auto up = ProjectToScreen(view, {100, 0, 20}, 1920, 1080);
    CHECK(up->y < 540.0);
    CHECK(!ProjectToScreen(view, {-10, 0, 0}, 1920, 1080));
    CHECK(!ProjectToScreen(view, {2, 0, 0}, 1920, 1080));  // inside the near plane
}

TEST_CASE("projection: screen rays and D3D matrices agree with the projection")
{
    const auto cam = Camera({100, -50, 64}, {12, 35, 5}, 70);
    const auto view = MakeViewParams(cam, 16.0 / 9.0, 4.0);
    const Vec3 p{400, 120, 30};
    const auto s = ProjectToScreen(view, p, 1920, 1080);
    CHECK(s.has_value());
    const Ray ray = ScreenRay(view, s->x, s->y, 1920, 1080);
    const Vec3 toP = Normalize(p - cam.position);
    CHECK_VEC_NEAR(ray.direction, toP, 1e-9);
    for (const auto conv : {DepthConvention::StandardInfinite, DepthConvention::ReversedInfinite})
    {
        const auto vp = Multiply(D3DViewMatrix(view), D3DProjectionMatrix(view, conv));
        double w = 0;
        const Vec3 ndc = TransformPoint(vp, p, &w);
        CHECK_NEAR((ndc.x * 0.5 + 0.5) * 1920, s->x, 0.05);
        CHECK_NEAR((0.5 - ndc.y * 0.5) * 1080, s->y, 0.05);
        CHECK_NEAR(ndc.z, DepthValue(conv, s->viewDepth, 4.0), 1e-5);
        CHECK_NEAR(w, s->viewDepth, 1e-3);
    }
    CHECK_NEAR(DepthValue(DepthConvention::StandardInfinite, 4.0, 4.0), 0.0, 1e-12);
    CHECK_NEAR(DepthValue(DepthConvention::ReversedInfinite, 4.0, 4.0), 1.0, 1e-12);
}

TEST_CASE("projection: graphic space maps front to forward and right to the object's left axis")
{
    const Mat3 axis = AnglesToAxis({0, 90, 0});  // facing +Y
    const Vec3 front = GraphicToWorld({0, 0, 1}, {10, 0, 0}, axis, {1, 1, 1});
    CHECK_VEC_NEAR(front, Vec3(10, 1, 0), 1e-12);
    const Vec3 right = GraphicToWorld({1, 0, 0}, {}, AnglesToAxis({}), {2, 2, 2});
    CHECK_VEC_NEAR(right, Vec3(0, 2, 0), 1e-12);
    const auto m = GraphicWorldMatrix({5, 6, 7}, AnglesToAxis({10, 20, 30}), {2, 3, 4});
    const Vec3 local{1, -2, 0.5};
    CHECK_VEC_NEAR(TransformPoint(m, local), GraphicToWorld(local, {5, 6, 7}, AnglesToAxis({10, 20, 30}), {2, 3, 4}), 1e-4);
    // A viewer in front of the graphic sees its right on their right.
    const auto view = MakeViewParams(Camera({100, 0, 0}, {0, 180, 0}), 1.0);
    const auto r = ProjectToScreen(view, GraphicToWorld({10, 0, 0}, {}, AnglesToAxis({}), {1, 1, 1}), 1000, 1000);
    CHECK(r->x > 500.0);
}

TEST_CASE("projection: rays hit oriented boxes and lines")
{
    Bounds3D b{{-10, -10, -10}, {10, 10, 10}};
    const Ray ray{{-100, 0, 0}, {1, 0, 0}};
    const auto t = IntersectRayOrientedBox(ray, b, {0, 0, 0}, AnglesToAxis({}), {1, 1, 1});
    CHECK(t.has_value());
    CHECK_NEAR(*t, 90.0, 1e-9);
    CHECK(!IntersectRayOrientedBox({{-100, 50, 0}, {1, 0, 0}}, b, {}, AnglesToAxis({}), {1, 1, 1}));
    CHECK(IntersectRayPlane(ray, {5, 0, 0}, {1, 0, 0}).has_value());
    CHECK_NEAR(DistanceToSegment2D(0, 5, -10, 0, 10, 0), 5.0, 1e-12);
    CHECK_NEAR(ClosestOnLine({{0, 0, 0}, {0, 1, 0}}, {0, 10, 0}, {1, 0, 0}), 0.0, 1e-9);
}

TEST_CASE("graphics: easing curves start at 0, end at 1, overshoot where they should")
{
    for (const auto& e : EasingNames())
    {
        CHECK_NEAR(EaseValue(e, 0.0), 0.0, 1e-9);
        CHECK_NEAR(EaseValue(e, 1.0), 1.0, 1e-9);
    }
    CHECK(EaseValue("easeOutBack", 0.6) > 1.0);
    CHECK(EaseValue("easeOutElastic", 0.2) > 1.0);
    CHECK_NEAR(EaseValue("linear", 0.3), 0.3, 1e-12);
    CHECK_NEAR(EaseValue("hold", 0.99), 0.0, 0);
}

TEST_CASE("graphics: animation presets are deterministic and staggered")
{
    GraphicDef g;
    GraphicAnimation fly;
    fly.preset = "flyIn";
    fly.start = 1.0;
    fly.duration = 1.0;
    fly.stagger = 0.2;
    g.animations.push_back(fly);
    const auto before = EvaluateGraphic(g, 0.5, 3);
    CHECK_NEAR(before.elements[0].opacity, 0.0, 1e-12);
    CHECK(Length(before.elements[0].offset) > 100.0);
    const auto mid = EvaluateGraphic(g, 1.5, 3);
    CHECK(Length(mid.elements[1].offset) > Length(mid.elements[0].offset));  // the second letter lags
    const auto after = EvaluateGraphic(g, 3.0, 3);
    for (const auto& e : after.elements)
    {
        CHECK_NEAR(Length(e.offset), 0.0, 1e-9);
        CHECK_NEAR(e.opacity, 1.0, 1e-12);
    }
    const auto again = EvaluateGraphic(g, 1.5, 3);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK_VEC_NEAR(again.elements[i].offset, mid.elements[i].offset, 0);

    // Seeded randomness: same seed, same frames; another seed differs somewhere.
    GraphicDef f;
    GraphicAnimation fl;
    fl.preset = "assemble";
    fl.duration = 2.0;
    fl.seed = 3;
    f.animations.push_back(fl);
    const auto a = EvaluateGraphic(f, 0.5, 5);
    const auto b = EvaluateGraphic(f, 0.5, 5);
    f.animations[0].seed = 4;
    const auto c = EvaluateGraphic(f, 0.5, 5);
    bool differs = false;
    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK_VEC_NEAR(a.elements[i].offset, b.elements[i].offset, 0);
        differs |= Length(a.elements[i].offset - c.elements[i].offset) > 1e-6;
    }
    CHECK(differs);

    GraphicDef t;
    GraphicAnimation tw;
    tw.preset = "typewriter";
    tw.start = 0.0;
    tw.stagger = 0.1;
    t.animations.push_back(tw);
    const auto tp = EvaluateGraphic(t, 0.15, 4);
    CHECK(tp.elements[0].opacity == 1.0 && tp.elements[1].opacity == 1.0 && tp.elements[2].opacity == 0.0);

    GraphicDef r;
    GraphicAnimation rot;
    rot.preset = "rotate";
    rot.duration = 4.0;
    r.animations.push_back(rot);
    CHECK_NEAR(EvaluateGraphic(r, 1.0, 1).world.angles.yaw, 90.0, 1e-9);
    for (const auto& p : AnimationPresets())
    {
        GraphicDef any;
        GraphicAnimation an;
        an.preset = p.id;
        any.animations.push_back(an);
        const auto pose = EvaluateGraphic(any, 0.5, 2);
        CHECK(pose.elements.size() == 2);
        for (const auto& e : pose.elements)
            CHECK(std::isfinite(e.opacity) && std::isfinite(e.scale) && std::isfinite(e.offset.x));
    }
}

TEST_CASE("graphics: tracking follows actors, faces the camera, locks to the lens")
{
    GraphicDef g;
    g.tracking.mode = TrackingMode::Actor;
    g.tracking.actor = "actor-1";
    g.tracking.attach = "head";
    g.transform.position = {0, 0, 12};
    TrackingContext ctx;
    ctx.attachPoint = [](std::string_view actor, std::string_view attach) -> std::optional<Transform> {
        if (actor != "actor-1" || attach != "head")
            return std::nullopt;
        Transform t;
        t.position = {100, 200, 64};
        t.angles = {0, 90, 0};
        return t;
    };
    auto pose = EvaluateGraphic(g, 0.0, 1, ctx);
    CHECK(pose.visible);
    CHECK_VEC_NEAR(pose.world.position, Vec3(100, 200, 76), 1e-12);
    g.tracking.inheritRotation = true;
    g.transform.position = {10, 0, 0};
    pose = EvaluateGraphic(g, 0.0, 1, ctx);
    CHECK_VEC_NEAR(pose.world.position, Vec3(100, 210, 64), 1e-9);  // rotated with the actor (facing +Y)
    g.tracking.actor = "missing";
    CHECK(!EvaluateGraphic(g, 0.0, 1, ctx).visible);

    GraphicDef bb;
    bb.tracking.mode = TrackingMode::Billboard;
    bb.transform.position = {0, 0, 0};
    TrackingContext camCtx;
    camCtx.camera = Camera({0, 100, 0}, {});
    const auto bp = EvaluateGraphic(bb, 0.0, 1, camCtx);
    CHECK_VEC_NEAR(AnglesToForward(bp.world.angles), Vec3(0, 1, 0), 1e-9);

    GraphicDef lens;
    lens.tracking.mode = TrackingMode::Camera;
    lens.tracking.distance = 50;
    camCtx.camera = Camera({10, 0, 5}, {0, 0, 0});
    const auto lp = EvaluateGraphic(lens, 0.0, 1, camCtx);
    CHECK_VEC_NEAR(lp.world.position, Vec3(60, 0, 5), 1e-9);
    CHECK_VEC_NEAR(AnglesToForward(lp.world.angles), Vec3(-1, 0, 0), 1e-9);  // its front faces the lens
}

TEST_CASE("graphics: geometry for text, shapes, logos and anchors")
{
    GraphicDef text;
    text.kind = GraphicKind::Text;
    text.text = "oG 141";
    text.textSettings.size = 30;
    text.geometry.detail = 0.2;
    const auto geo = BuildGraphicGeometry(text, Rubik(), {});
    CHECK_MSG(geo, geo.error);
    CHECK(geo.elements.size() == 5);  // spaces have no element
    for (const auto& e : geo.elements)
    {
        CHECK(ValidateMesh(e.mesh));
        CHECK(IsWatertight(e.mesh));
    }
    CHECK(geo.bounds.Valid());
    CHECK_NEAR(geo.bounds.Size().z, text.geometry.depth, 1e-4);
    text.separateWords = true;
    CHECK(BuildGraphicGeometry(text, Rubik(), {}).elements.size() == 2);
    // The geometry key changes with what the mesh depends on, not with the transform.
    const auto key = GeometryKey(text);
    text.transform.position = {1, 2, 3};
    CHECK(GeometryKey(text) == key);
    text.text = "oG";
    CHECK(GeometryKey(text) != key);
    GraphicDef none = text;
    CHECK(!BuildGraphicGeometry(none, FontResolver{}, {}));

    GraphicDef box;
    box.kind = GraphicKind::Shape;
    box.shape = ShapeKind::Box;
    const auto boxGeo = BuildGraphicGeometry(box, {}, {});
    CHECK(boxGeo.elements.size() == 1);
    CHECK_NEAR(boxGeo.bounds.Size().x, 40.0, 1e-5);

    GraphicDef anchor;
    anchor.kind = GraphicKind::Anchor;
    const auto ag = BuildGraphicGeometry(anchor, {}, {});
    CHECK(ag && ag.elements.empty() && ag.bounds.Valid());

    const auto dir = std::filesystem::temp_directory_path();
    std::ofstream(dir / "iw4iris-logo.svg") << R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 50">
      <rect fill="#d4af37" x="0" y="0" width="40" height="50"/><rect fill="#ffffff" x="60" y="0" width="40" height="50"/></svg>)";
    GraphicDef logo;
    logo.kind = GraphicKind::Logo;
    logo.source = "iw4iris-logo.svg";  // relative to the project folder
    logo.width = 200;
    const auto lg = BuildGraphicGeometry(logo, {}, dir);
    CHECK_MSG(lg, lg.error);
    CHECK(lg.elements.size() == 2);
    CHECK(lg.elements[0].hasColor);
    CHECK_NEAR(lg.bounds.Size().x, 200.0, 0.1);
    logo.source = "missing.svg";
    CHECK(!BuildGraphicGeometry(logo, {}, dir));
    CHECK(MaterialPresets().size() >= 18);
    CHECK(MakeMaterial("gold").metallic > 0.5);
}

TEST_CASE("scene: actors play the clip that covers t, idle otherwise")
{
    ActorDef a;
    a.idleAnim = "pb_stand_alert";
    a.clips.push_back({"c1", "pb_walk", 1.0, 2.0});
    a.clips.push_back({"c2", "pb_run", 2.5, 1.0});
    CHECK(EvaluateActor(a, 0.5).anim == "pb_stand_alert");
    CHECK(EvaluateActor(a, 1.5).anim == "pb_walk");
    const auto s = EvaluateActor(a, 2.7);
    CHECK(s.anim == "pb_run" && s.clipId == "c2" && s.animStart == 2.5);
    const auto idle = EvaluateActor(a, 4.0);
    CHECK(idle.anim == "pb_stand_alert");
    CHECK_NEAR(idle.animStart, 3.5, 1e-12);
    ScalarKey hide;
    hide.time = 5.0;
    hide.value = 0.0;
    hide.interpolation = Interpolation::Hold;
    ScalarKey show = hide;
    show.time = 0.0;
    show.value = 1.0;
    a.visibility.AddKey(show);
    a.visibility.AddKey(hide);
    CHECK(EvaluateActor(a, 4.9).visible);
    CHECK(!EvaluateActor(a, 5.1).visible);
}

TEST_CASE("scene: directions compile into moves, turns and clips")
{
    ActorDef a;
    a.transform.position = {0, 0, 0};
    a.transform.angles = {0, 0, 0};
    a.actionsStart = 1.0;
    ActorAction wait;
    wait.kind = ActionKind::Wait;
    wait.duration = 2.0;
    ActorAction walk;
    walk.kind = ActionKind::WalkTo;
    walk.target = {0, 160, 0};  // to the left: a 90 degree turn first
    ActorAction face;
    face.kind = ActionKind::FaceActor;
    face.targetActor = "actor-2";
    ActorAction aim;
    aim.kind = ActionKind::AimAt;
    aim.target = {100, 160, 0};
    aim.duration = 3.0;
    ActorAction exit;
    exit.kind = ActionKind::Exit;
    a.actions = {wait, walk, face, aim, exit};
    DirectionContext ctx;
    ctx.roles = FindAnimationRoles(FakeAssets().animations);
    ctx.walkSpeed = 80;
    ctx.turnRate = 180;
    ctx.actorPosition = [](std::string_view id, double) -> std::optional<Vec3> {
        if (id == "actor-2")
            return Vec3{-100, 160, 0};
        return std::nullopt;
    };
    const auto c = CompileDirections(a, ctx);
    CHECK(c.warnings.empty());
    // Wait 1..3, turn 90 deg at 180 deg/s 3..3.5, walk 160 units at 80 u/s 3.5..5.5,
    // turn to actor-2 (90 deg, it is to the west) 5.5..6, turn to the aim point (180) 6..7, aim 7..10.
    CHECK_NEAR(c.end, 10.0, 1e-9);
    const auto pAt = [&](double t) { return c.track.Evaluate(t, a.transform); };
    CHECK_VEC_NEAR(pAt(2.0).position, Vec3(0, 0, 0), 1e-9);
    CHECK_NEAR(pAt(3.5).angles.yaw, 90.0, 1e-9);
    CHECK_VEC_NEAR(pAt(4.5).position, Vec3(0, 80, 0), 1e-9);
    CHECK_VEC_NEAR(pAt(5.5).position, Vec3(0, 160, 0), 1e-9);
    CHECK_NEAR(std::fmod(pAt(6.0).angles.yaw + 720.0, 360.0), 180.0, 1e-6);
    CHECK_NEAR(std::fmod(pAt(8.0).angles.yaw + 720.0, 360.0), 0.0, 1e-6);
    ActorDef compiled = a;
    compiled.track = c.track;
    compiled.clips = c.clips;
    compiled.visibility = c.visibility;
    compiled.idleAnim = ctx.roles.idle;
    CHECK(EvaluateActor(compiled, 2.0).anim == "pb_stand_alert");
    CHECK(EvaluateActor(compiled, 4.0).anim == "pb_walk_forward_mg");
    CHECK(EvaluateActor(compiled, 9.0).anim == "pb_stand_ads");
    CHECK(EvaluateActor(compiled, 9.9).visible);
    CHECK(!EvaluateActor(compiled, 10.1).visible);
    // Same input, same output.
    const auto again = CompileDirections(a, ctx);
    CHECK(again.track == c.track);
    CHECK(again.clips == c.clips);
    // Unknown actor gives a readable warning rather than a crash.
    ActorDef b;
    ActorAction bad;
    bad.kind = ActionKind::FaceActor;
    bad.targetActor = "nobody";
    b.actions = {bad};
    CHECK(!CompileDirections(b, ctx).warnings.empty());
}

TEST_CASE("scene: effects fire once or repeatedly between frames")
{
    Scene s;
    EffectDef once;
    once.id = "fx-1";
    once.fx = "smoke/thin_black_smoke_m";
    once.start = 1.0;
    EffectDef rep = once;
    rep.id = "fx-2";
    rep.repeat = 0.5;
    rep.count = 3;
    s.effects = {once, rep};
    CHECK(EffectEventsBetween(s, 0.0, 0.99).empty());
    CHECK(EffectEventsBetween(s, 0.99, 1.0).size() == 2);
    CHECK(EffectEventsBetween(s, 1.0, 5.0).size() == 2);  // the repeats at 1.5 and 2.0
    CHECK(EffectEventsBetween(s, 0.0, 5.0).size() == 4);
    CHECK(EffectEventsBetween(s, 2.0, 1.0).empty());
    s.effects[1].count = 0;
    CHECK(EffectEventsBetween(s, 0.0, 3.0).size() == 1 + 5);
}

TEST_CASE("scene: referenced assets are collected for precaching")
{
    Scene s;
    ActorDef a;
    a.id = "actor-1";
    a.bodyModel = "mp_body_tf141_lmg";
    a.headModel = "head_tf141_lmg";
    a.idleAnim = "pb_stand_alert";
    a.weapon = "cheytac_mp";
    a.attachments.push_back({"m4_mp", "tag_stowed_back", 0});
    a.attachments.push_back({"com_plasticcase_beige_big", "j_spine4", 0});
    a.clips.push_back({"c", "pb_run_forward", 0, 1});
    s.actors.push_back(a);
    PropDef p;
    p.id = "prop-1";
    p.model = "com_barrel_benzin";
    s.props.push_back(p);
    const auto models = s.ReferencedModels();
    CHECK(models.size() == 4);
    CHECK(s.ReferencedAnimations().size() == 2);
    const auto weapons = s.ReferencedWeapons();
    CHECK(weapons.size() == 2 && weapons[0] == "cheytac_mp");
    CHECK(s.Contains("prop-1") && !s.Contains("prop-2"));
    CHECK_NEAR(s.ContentEnd(), 1.0, 0);
}

TEST_CASE("assets: names become readable, categorised entries")
{
    auto body = ClassifyModel("mp_body_tf141_lmg");
    CHECK(body.kind == AssetKind::Body);
    CHECK(body.category == "Task Force 141");
    CHECK(body.label == "TF141 LMG");
    CHECK(ClassifyModel("mp_body_ghillie_urban_sniper").category == "Ghillie suits");
    CHECK(ClassifyModel("mp_body_opforce_russian_smg").category == "Spetsnaz");
    CHECK(ClassifyModel("mp_body_us_army_assault_a").category == "Rangers");
    CHECK(ClassifyModel("head_tf141_lmg").kind == AssetKind::Head);
    CHECK(ClassifyModel("com_plasticcase_beige_big").category == "Military equipment");
    CHECK(ClassifyModel("vehicle_pavelow").category == "Helicopters");
    CHECK(ClassifyModel("vehicle_little_bird_armed").category == "Helicopters");
    CHECK(ClassifyModel("vehicle_humvee").category == "Vehicles");
    CHECK(ClassifyModel("tag_origin").hidden);
    CHECK(ClassifyAnimation("pb_stand_alert").category == "Standing");
    CHECK(ClassifyAnimation("pb_crouch_walk_forward").category == "Crouching");
    CHECK(ClassifyAnimation("pb_sprint").category == "Sprinting");
    CHECK(ClassifyAnimation("pb_stand_death_chest_blowback").category == "Deaths");
    CHECK(WeaponDisplayName("cheytac_mp") == "Intervention");
    const auto w = ClassifyWeapon("cheytac_acog_fmj_mp");
    CHECK(w.label == "Intervention (ACOG, FMJ)");
    CHECK(w.category == "Sniper rifles");
    CHECK(ClassifyWeapon("turret_minigun_mp").hidden);
    CHECK(ClassifyEffect("smoke/thin_black_smoke_m").category == "Smoke");
    CHECK(ClassifyEffect("explosions/large_vehicle_explosion").category == "Explosions");
    CHECK(ClassifyEffect("fire/firelp_med_pm").category == "Fire");
    CHECK(HumanizeAssetName("weird_thing_42") == "Weird Thing 42");
}

TEST_CASE("assets: characters pair bodies with matching heads, search finds them")
{
    const auto chars = FakeAssets().characters;
    CHECK(chars.size() == 4);
    for (const auto& c : chars)
        CHECK(!c.head.empty());
    const auto it = std::find_if(chars.begin(), chars.end(), [](const auto& c) { return c.body == "mp_body_tf141_lmg"; });
    CHECK(it != chars.end() && it->head == "head_tf141_lmg");
    CHECK(MatchesSearch(*it, "141 lmg"));
    CHECK(!MatchesSearch(*it, "ghillie"));
    CHECK(MatchesSearch(ClassifyWeapon("cheytac_mp"), "intervention"));
    CHECK(FindAnimation(FakeAssets().animations, {{"walk"}}) == "pb_walk_forward_mg");
    CHECK(FindAnimation(FakeAssets().animations, {{"swim"}, {"run"}}) == "pb_run_forward");
    CHECK(PickWeapon(FakeAssets().weapons, {"barrett_mp", "cheytac_mp"}) == "cheytac_mp");
    CHECK(PickWeapon(FakeAssets().weapons, {"wa2000_mp"}, "Sniper rifles") == "cheytac_mp");
    CHECK(PickWeapon({"turret_minigun_mp"}, {}, "Other weapons").empty());
}

TEST_CASE("shots: framing puts the subject in the centre at the right distance")
{
    const double d = FramingDistance(72, 90, 16.0 / 9.0);
    CHECK_NEAR(d * std::tan(45 * kDegToRad) / (16.0 / 9.0), 36.0, 1e-9);
    ShotSubject subject;
    subject.position = {500, 200, 0};
    subject.yaw = 30;
    for (const auto type : AllShotTypes())
    {
        ShotSettings st;
        st.start = 2.0;
        st.duration = 4.0;
        const auto shot = MakeShot(type, subject, st);
        CHECK_MSG(shot.camera.path.Size() >= 2, ShotLabel(type));
        for (const auto& k : shot.camera.path.Keys())
            CHECK(std::isfinite(k.position.x) && std::isfinite(k.angles.yaw) && k.horizontalFov > 1.0);
        CHECK_NEAR(shot.camera.path.StartTime(), 2.0, 1e-9);
        CHECK_NEAR(shot.camera.path.EndTime(), 6.0, 1e-9);
        CHECK(ShotTypeFromString(ToString(type)) == type);
    }
    // A close-up looks straight at the head.
    const auto cu = MakeShot(ShotType::CloseUp, subject, ShotSettings{});
    const auto& k = cu.camera.path.Keys().front();
    const auto view = MakeViewParams(Camera(k.position, k.angles, k.horizontalFov), 16.0 / 9.0);
    const auto head = ProjectToScreen(view, subject.position + Vec3{0, 0, 72 * 0.88}, 1920, 1080);
    CHECK(head.has_value());
    CHECK_NEAR(head->x, 960.0, 0.5);
    CHECK_NEAR(head->y, 540.0, 0.5);
    // The orbit keeps its distance to the subject's axis.
    const auto orbit = MakeShot(ShotType::Orbit, subject, ShotSettings{});
    const double r0 = std::hypot(orbit.camera.path.Keys()[0].position.x - 500, orbit.camera.path.Keys()[0].position.y - 200);
    for (const auto& ok : orbit.camera.path.Keys())
        CHECK_NEAR(std::hypot(ok.position.x - 500, ok.position.y - 200), r0, 1e-6);
    CHECK(!MakeShot(ShotType::OverShoulder, subject, ShotSettings{}).warnings.empty());
    CHECK(MakeShot(ShotType::Handheld, subject, ShotSettings{}).camera.shake.enabled);
}

TEST_CASE("templates: scene templates build editable actors, cameras and cuts")
{
    const auto assets = FakeAssets();
    for (const auto& t : SceneTemplates())
    {
        Project p = MakeDefaultProject("t");
        const auto before = p.cameras.size();
        const auto r = ApplySceneTemplate(p, t.id, {100, 200, 0}, 45, assets, 0.0);
        CHECK_MSG(p.cameras.size() > before || !p.scene.actors.empty(), t.id);
        std::set<std::string> ids;
        for (const auto& c : p.cameras)
            CHECK_MSG(ids.insert(c.id).second, c.id);
        for (const auto& a : p.scene.actors)
        {
            CHECK_MSG(ids.insert(a.id).second, a.id);
            CHECK(!a.bodyModel.empty());
        }
        for (const auto& w : r.warnings)
            CHECK(!w.empty());
    }
    Project sniper = MakeDefaultProject("s");
    ApplySceneTemplate(sniper, "sniperShowcase", {0, 0, 0}, 0, assets);
    CHECK(sniper.scene.actors.size() == 1);
    CHECK(sniper.scene.actors[0].weapon == "cheytac_mp");
    CHECK(sniper.scene.actors[0].bodyModel == "mp_body_ghillie_urban_sniper");
    CHECK(sniper.useCameraCuts && sniper.cuts.Cuts().size() == 2);
    Project trick = MakeDefaultProject("t");
    ApplySceneTemplate(trick, "trickshot", {0, 0, 0}, 0, assets);
    CHECK(trick.timeRemap.Speed().Size() == 4);
    CHECK(!trick.scene.actors[0].clips.empty());
    Project nobody = MakeDefaultProject("n");
    const auto r = ApplySceneTemplate(nobody, "sniperShowcase", {0, 0, 0}, 0, TemplateAssets{});
    CHECK(!r.warnings.empty());
    CHECK(nobody.scene.actors.empty());
}

TEST_CASE("templates: characters match the map's environment (desert ghillie on a desert map)")
{
    // Every MW2 map loads the ghillie suits of all environments; the map's
    // own soldiers say which one fits (seen on mp_afghan: Desert TF141).
    const std::vector<std::string> ghillies{"mp_body_ally_ghillie_arctic_sniper", "mp_body_ally_ghillie_desert_sniper",
                                            "mp_body_ally_ghillie_forest_sniper", "mp_body_ally_ghillie_urban_sniper"};
    auto assetsWith = [&](std::vector<std::string> soldiers) {
        TemplateAssets a;
        soldiers.insert(soldiers.end(), ghillies.begin(), ghillies.end());
        a.characters = BuildCharacters(soldiers);
        a.animations = {"pb_stand_alert"};
        a.weapons = {"cheytac_mp"};
        return a;
    };
    auto sniperBody = [](const TemplateAssets& a) {
        Project p = MakeDefaultProject("s");
        ApplySceneTemplate(p, "sniperShowcase", {0, 0, 0}, 0, a);
        return p.scene.actors.empty() ? std::string() : p.scene.actors[0].bodyModel;
    };

    const auto desert = assetsWith({"mp_body_tf141_desert_assault_a", "mp_body_tf141_desert_lmg", "mp_body_opforce_arab_smg"});
    CHECK(MapEnvironment(desert) == "desert");
    CHECK(sniperBody(desert) == "mp_body_ally_ghillie_desert_sniper");

    const auto arctic = assetsWith({"mp_body_tf141_arctic_assault_a", "mp_body_opforce_arctic_lmg"});
    CHECK(MapEnvironment(arctic) == "arctic");
    CHECK(sniperBody(arctic) == "mp_body_ally_ghillie_arctic_sniper");

    // City maps: soldiers without an environment word.
    const auto city = assetsWith({"mp_body_us_army_assault_a", "mp_body_airborne_lmg"});
    CHECK(MapEnvironment(city) == "urban");
    CHECK(sniperBody(city) == "mp_body_ally_ghillie_urban_sniper");

    CharacterEntry plain;
    plain.body = "mp_body_us_army_assault_a";
    CHECK(CharacterEnvironment(plain).empty());
}

TEST_CASE("templates: title templates create animated graphics")
{
    for (const auto& t : TitleTemplates())
    {
        Project p = MakeDefaultProject("t");
        const auto r = ApplyTitleTemplate(p, t.id, "oGinStyle", {0, 0, 100}, 90, "font.ttf", 1.0);
        CHECK_MSG(r.created.size() == 1, t.id);
        CHECK(p.scene.graphics.size() == 1);
        CHECK(!p.scene.graphics[0].animations.empty());
        CHECK_NEAR(p.scene.graphics[0].animations[0].start, 1.0, 0);
    }
    Project p = MakeDefaultProject("o");
    GraphicDef g;
    g.id = "gfx-1";
    g.name = "Logo";
    g.transform.position = {0, 0, 300};
    const auto camId = AddGraphicOrbit(p, g, {{-50, -20, -5}, {50, 20, 5}}, 0, 6);
    CHECK(p.FindCamera(camId) != nullptr);
    CHECK(p.FindCamera(camId)->path.Size() == 9);
}
