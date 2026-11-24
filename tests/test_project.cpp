#include <filesystem>
#include <fstream>

#include "iris/project.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    Project MakeRichProject()
    {
        Project p = MakeDefaultProject("Rust Trickshot");
        auto& dolly = p.cameras[0];
        for (int i = 0; i < 4; ++i)
        {
            CameraKey k;
            k.time = i * 1.5;
            k.position = {100.0 * i, -50.0 * i, 30.0 + i};
            k.angles = {5.0 * i, 30.0 * i, i == 2 ? 10.0 : 0.0};
            k.horizontalFov = 60.0 + i;
            k.interpolation = i == 1 ? Interpolation::Bezier : Interpolation::Smooth;
            k.inTangent = {-10, 0, 0};
            k.outTangent = {10, 0, 5};
            k.ease = {0.25, 0.5};
            dolly.path.AddKey(k);
        }
        dolly.path.SetConstantSpeed(true);
        dolly.lens.focalLengthMm = 85;
        dolly.shake.enabled = true;
        dolly.target = {TargetKind::Player, 3, {}, {0, 0, 60}};
        p.cuts.AddCut({0.0, "cam-1"});
        p.cuts.AddCut({2.0, "cam-2"});
        p.useCameraCuts = true;
        p.markers.push_back({"marker-1", 2.25, "Collateral", "#FF0000", "kill"});
        PropertyTrack t;
        t.property = "film.brightness";
        ScalarKey a;
        a.time = 0;
        a.value = 0.1;
        ScalarKey b;
        b.time = 2;
        b.value = -0.05;
        b.interpolation = Interpolation::Bezier;
        b.tangentMode = TangentMode::Free;
        b.inHandle = {-0.5, 0.02};
        t.curve.AddKey(a);
        t.curve.AddKey(b);
        p.tracks.push_back(t);
        p.trackStates["cam-1"] = {true, false, true, "Cameras"};
        ScalarKey s0;
        s0.time = 1;
        s0.value = 0.25;
        p.timeRemap.Speed().AddKey(s0);
        p.timeRemap.sceneOffset = 12.5;
        p.visuals.filmOverride = true;
        p.visuals.filmContrast = 1.3;
        p.visuals.sunOverride = true;
        p.visuals.sunColor = {1.0, 0.6, 0.3};
        p.recording.format = OutputFormat::ProRes422HQ;
        p.recording.frameRate = {120, 1};
        p.recording.passes = {{PassKind::Beauty, true, "#00FF00"}, {PassKind::Greenscreen, false, "#00FF00"}};
        RenderJob job;
        job.id = "job-1";
        job.name = "Main";
        job.settings = p.recording;
        p.renderQueue.push_back(job);
        p.lights.push_back({"light-1", "Key", "spot", {1, 2, 3}, {0, 90, 0}, {1, 0.5, 0.2}, 2.0, 300, 30, 0.4, 3200});
        ActorDef actor;
        actor.id = "actor-1";
        actor.name = "Sniper";
        actor.character = "Ghillie sniper";
        actor.bodyModel = "mp_body_ghillie_test";
        actor.headModel = "head_ghillie_test";
        actor.idleAnim = "pb_stand_alert";
        actor.weapon = "cheytac_mp";
        actor.camo = 2;
        actor.attachments.push_back({"cheytac_mp", "tag_stowed_back", 1});
        actor.transform.position = {10, 20, 30};
        actor.transform.angles = {0, 45, 0};
        actor.track.SetKey(0.0, actor.transform, Interpolation::Linear);
        actor.track.SetKey(2.0, Transform{{110, 20, 30}, {0, 90, 0}}, Interpolation::Linear);
        actor.clips.push_back({"clip-1", "pb_walk_forward", 0.0, 2.0});
        ActorAction walk;
        walk.kind = ActionKind::WalkTo;
        walk.target = {110, 20, 30};
        actor.actions.push_back(walk);
        ScalarKey hide;
        hide.time = 5.0;
        hide.value = 0.0;
        hide.interpolation = Interpolation::Hold;
        actor.visibility.AddKey(hide);
        p.scene.actors.push_back(actor);
        PropDef prop;
        prop.id = "prop-1";
        prop.name = "Crate";
        prop.model = "com_plasticcase_beige_big";
        prop.transform.position = {5, 5, 0};
        prop.attachActor = "actor-1";
        prop.attachTag = "j_spine4";
        p.scene.props.push_back(prop);
        EffectDef fx;
        fx.id = "fx-1";
        fx.name = "Smoke";
        fx.fx = "smoke/thin_black_smoke_m";
        fx.start = 1.5;
        fx.repeat = 2.0;
        fx.count = 3;
        p.scene.effects.push_back(fx);
        GraphicDef title;
        title.id = "gfx-1";
        title.name = "Title";
        title.text = "oGinStyle";
        title.font = "fonts/Rubik-SemiBold.ttf";
        title.textSettings.size = 32;
        title.textSettings.layout = TextLayoutKind::Circle;
        title.materials.front = MakeMaterial("chrome");
        title.materials.separate = true;
        title.materials.side = MakeMaterial("matte_black");
        title.tracking.mode = TrackingMode::Actor;
        title.tracking.actor = "actor-1";
        title.tracking.attach = "head";
        title.renderMode = GraphicRenderMode::XRay;
        GraphicAnimation fly;
        fly.preset = "flyIn";
        fly.start = 1.0;
        fly.duration = 1.5;
        fly.direction = {0, 0, 1};
        fly.easing = "easeOutBack";
        fly.seed = 7;
        title.animations.push_back(fly);
        title.transform.position = {0, 0, 20};
        title.track.SetKey(1.0, title.transform, Interpolation::Smooth, true);
        p.scene.graphics.push_back(title);
        p.scene.filmSet.gametype = "war";
        DemoRef demo;
        demo.path = "demos/rust_trickshot.dm_13";
        demo.name = "rust_trickshot";
        demo.mapName = "mp_rust";
        demo.fileSize = 1234;
        p.demo = demo;
        p.timeline.end = 30;
        p.timeline.outPoint = 30;
        p.editorLayout = "[Window][Timeline]\nPos=0,0\n";
        return p;
    }
}  // namespace

TEST_CASE("project: serialize and parse round-trip")
{
    const Project p = MakeRichProject();
    const auto text = SerializeProject(p);
    const auto r = ParseProject(text);
    CHECK(r.project.has_value());
    CHECK(r.error.empty());
    CHECK(!r.migrated);
    const Project& q = *r.project;
    CHECK(q.name == p.name);
    CHECK(q.cameras == p.cameras);
    CHECK(q.activeCameraId == p.activeCameraId);
    CHECK(q.cuts.Cuts() == p.cuts.Cuts());
    CHECK(q.useCameraCuts);
    CHECK(q.markers == p.markers);
    CHECK(q.tracks == p.tracks);
    CHECK(q.trackStates == p.trackStates);
    CHECK(q.visuals == p.visuals);
    CHECK(q.lights == p.lights);
    CHECK(q.scene == p.scene);
    CHECK(q.recording == p.recording);
    CHECK(q.renderQueue == p.renderQueue);
    CHECK(q.demo == p.demo);
    CHECK(q.timeline == p.timeline);
    CHECK(q.timeRemap.Speed().Keys() == p.timeRemap.Speed().Keys());
    CHECK_NEAR(q.timeRemap.sceneOffset, 12.5, 0);
    CHECK(q.editorLayout == p.editorLayout);
    // A second round trip is byte-identical.
    CHECK(SerializeProject(q) == text);
}

TEST_CASE("project: rejects what it cannot read")
{
    CHECK(!ParseProject("not json").project);
    CHECK(!ParseProject("[]").project);
    CHECK(!ParseProject("{\"name\": \"x\"}").project);
    const auto newer = ParseProject("{\"format\": \"iw4iris-project\", \"schemaVersion\": 99}");
    CHECK(!newer.project);
    CHECK(newer.error.find("newer") != std::string::npos);
    CHECK(!ParseProject("{\"format\": \"something-else\", \"schemaVersion\": 1}").project);
}

TEST_CASE("project: migrates schema 0 files")
{
    const char* v0 = R"({
        "version": 0, "name": "Old", "fps": 23.976,
        "campath": [
            {"tickMs": 0, "position": [0, 0, 0], "angles": [0, 0, 0], "fov": 80},
            {"tickMs": 1500, "position": [100, 0, 0], "angles": [0, 90, 0], "fov": 60}
        ]})";
    const auto r = ParseProject(v0);
    CHECK(r.project.has_value());
    CHECK(r.migrated);
    CHECK(r.originalSchemaVersion == 0);
    const auto& p = *r.project;
    CHECK(p.cameras.size() == 1);
    CHECK(p.cameras[0].path.Size() == 2);
    CHECK_NEAR(p.cameras[0].path.Keys()[1].time, 1.5, 1e-12);
    CHECK(p.cameras[0].path.GetRotationMode() == RotationMode::Euler);
    CHECK(p.timeline.frameRate == (FrameRate{24000, 1001}));
    CHECK(p.activeCameraId == "cam-1");
}

TEST_CASE("project: repairs ids and drops dangling cuts")
{
    const char* doc = R"({"format": "iw4iris-project", "schemaVersion": 1,
        "cameras": [{"id": "a", "name": "A"}, {"id": "a", "name": "B"}, {"name": "C", "mode": "warp"}],
        "activeCamera": "missing",
        "cuts": [{"t": 0, "camera": "a"}, {"t": 1, "camera": "ghost"}],
        "renderQueue": [{"id": "j", "status": "rendering"}]})";
    const auto r = ParseProject(doc);
    CHECK(r.project.has_value());
    const auto& p = *r.project;
    CHECK(p.cameras.size() == 3);
    CHECK(p.cameras[0].id != p.cameras[1].id);
    CHECK(!p.cameras[2].id.empty());
    CHECK(p.activeCameraId == "a");
    CHECK(p.cuts.Cuts().size() == 1);
    CHECK(p.renderQueue[0].status == "queued");
    CHECK(r.warnings.size() >= 3);
}

TEST_CASE("project: queries")
{
    Project p = MakeRichProject();
    CHECK(p.CameraAt(1.0)->id == "cam-1");
    CHECK(p.CameraAt(2.5)->id == "cam-2");
    p.useCameraCuts = false;
    CHECK(p.CameraAt(2.5)->id == p.activeCameraId);
    CHECK_NEAR(p.PropertyValue("film.brightness", 0.0, 9.0), 0.1, 1e-12);
    CHECK_NEAR(p.PropertyValue("missing.track", 0.0, 9.0), 9.0, 0);
    CHECK(MakeUniqueId(p, "cam") == "cam-4");
    CHECK(p.ContentEnd() >= 4.5);
}

TEST_CASE("project: save and load files atomically with relative demo paths")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "iw4iris-test-project";
    fs::remove_all(dir);
    fs::create_directories(dir / "demos");
    { std::ofstream(dir / "demos" / "rust_trickshot.dm_13") << "demo"; }

    Project p = MakeRichProject();
    p.demo->path = (dir / "demos" / "rust_trickshot.dm_13").string();
    p.demo->fileSize = 4;
    const fs::path file = dir / "edit.iw4iris";
    std::string error;
    CHECK(SaveProjectFile(file, p, &error));
    CHECK(error.empty());
    CHECK(!fs::exists(dir / "edit.iw4iris.tmp"));
    const auto r = LoadProjectFile(file);
    CHECK(r.project.has_value());
    CHECK(r.project->demo->path == "demos/rust_trickshot.dm_13");
    const auto resolved = ResolveDemoPath(*r.project, file);
    CHECK(resolved.has_value());
    CHECK(fs::exists(*resolved));
    CHECK(FindMissingReferences(*r.project, file).empty());
    CHECK(!r.project->modifiedUtc.empty());

    fs::remove(dir / "demos" / "rust_trickshot.dm_13");
    CHECK(FindMissingReferences(*r.project, file).size() == 1);

    CHECK(!IsAutosaveNewer(file));
    CHECK(SaveProjectFile(AutosavePathFor(file), p));
    CHECK(fs::exists(AutosavePathFor(file)));
    CHECK(!LoadProjectFile(dir / "missing.iw4iris").project);
    fs::remove_all(dir);
}

TEST_CASE("project: recent list")
{
    std::vector<std::string> recent;
    recent = PushRecent(recent, "a");
    recent = PushRecent(recent, "b");
    recent = PushRecent(recent, "a");
    CHECK(recent.size() == 2);
    CHECK(recent.front() == "a");
    for (int i = 0; i < 20; ++i)
        recent = PushRecent(recent, std::to_string(i), 5);
    CHECK(recent.size() == 5);
}

TEST_CASE("project: imports IWXMVM / T6Iris campaths")
{
    const char* doc = R"({"game": "IW3", "demo": "x", "properties": [
        {"property": "CampathCamera", "keyframes": [
            {"tick": 5000, "value": {"type": "CameraData", "values": [1, 2, 3, 10, 20, 0, 65]}},
            {"tick": 7500, "value": {"type": "CameraData", "values": [4, 5, 6, 0, 90, 5, 80]}}]}]})";
    std::string error;
    const auto path = ImportIwxmvmCampath(doc, &error);
    CHECK(path.has_value());
    CHECK(path->Size() == 2);
    CHECK_NEAR(path->Keys()[0].time, 0.0, 0);
    CHECK_NEAR(path->Keys()[1].time, 2.5, 1e-12);
    CHECK_NEAR(path->Keys()[1].angles.yaw, 90.0, 0);
    CHECK_NEAR(path->Keys()[1].horizontalFov, 80.0, 0);
    CHECK(!ImportIwxmvmCampath("{\"properties\": []}", &error).has_value());
    CHECK(!error.empty());
}
