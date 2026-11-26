#include "iris/moments.hpp"
#include "iris/sync.hpp"
#include "test.hpp"

#include <cmath>
#include <cstdlib>

using namespace iris;

namespace
{
    KillFeed Feed(std::vector<FeedKill> kills, std::vector<ServerSpan> seen)
    {
        KillFeed f;
        f.kills = std::move(kills);
        f.seen = std::move(seen);
        return f;
    }

    // One match as two clients recorded it: B's copy with its own clock
    // offset and receive jitter.
    KillFeed Shifted(const KillFeed& a, int offset, int jitter)
    {
        KillFeed b = a;
        int i = 0;
        for (auto& k : b.kills)
            k.serverTime += offset + ((i++ % 2) ? jitter : -jitter);
        for (auto& s : b.seen)
        {
            s.first += offset;
            s.last += offset;
        }
        return b;
    }

    const KillFeed kMatch = Feed({{10000, "Bob", "Alice"},
                                  {14000, "Carl", "Dana"},
                                  {21000, "Alice", "Carl"},
                                  {33000, "Bob", "Dana"},
                                  {47000, "Dana", "Bob"}},
                                 {{5000, 60000}});
}

TEST_CASE("sync: two recordings of one server session share the clock")
{
    const auto s = SuggestSync(kMatch, Shifted(kMatch, 0, 50));
    CHECK(s.found);
    CHECK(std::abs(s.offsetMs) <= 50);
    CHECK(s.matched == 5);
    CHECK(s.missing == 0);
    CHECK(std::abs(s.confidence - 1.0) < 1e-9);
    CHECK(s.reason.find("Same server clock") != std::string::npos);
}

TEST_CASE("sync: a shifted server clock is found from the kills")
{
    const auto s = SuggestSync(kMatch, Shifted(kMatch, 125000, 40));
    CHECK(s.found);
    CHECK(std::abs(s.offsetMs - 125000) <= 40);
    CHECK(s.matched == 5);
    // The reverse direction gives the negated offset.
    const auto r = SuggestSync(Shifted(kMatch, 125000, 40), kMatch);
    CHECK(r.found);
    CHECK(std::abs(r.offsetMs + 125000) <= 40);
}

TEST_CASE("sync: repeated killer / victim pairs do not pull the offset away")
{
    auto a = Feed({{10000, "Bob", "Alice"}, {20000, "Bob", "Alice"}, {30000, "Bob", "Alice"}, {40000, "Carl", "Bob"}},
                  {{0, 50000}});
    const auto s = SuggestSync(a, Shifted(a, 3000, 0));
    CHECK(s.found);
    CHECK(s.offsetMs == 3000);  // not 13000 or -7000 from cross pairings
    CHECK(s.matched == 4);
}

TEST_CASE("sync: recordings of different matches get no suggestion")
{
    auto other = Feed({{10000, "Xavier", "Yan"}, {20000, "Yan", "Zoe"}, {30000, "Zoe", "Xavier"}}, {{5000, 60000}});
    const auto s = SuggestSync(kMatch, other);
    CHECK(!s.found);
    CHECK(s.reason.find("no kill in common") != std::string::npos);
}

TEST_CASE("sync: two coincidental matches among disagreeing kills are rejected")
{
    // Same names twice at a common offset, but four more kills in the time
    // both cover that the other demo does not show: not the same match.
    auto other = Feed({{10000, "Bob", "Alice"}, {14000, "Carl", "Dana"}, {18000, "Eve", "Finn"},
                       {26000, "Finn", "Eve"}, {38000, "Gus", "Eve"}, {52000, "Eve", "Gus"}},
                      {{5000, 60000}});
    const auto s = SuggestSync(kMatch, other);
    CHECK(!s.found);
    CHECK(s.matched == 2);
    CHECK(s.missing > 2);
    CHECK(s.confidence < 0.6);
}

TEST_CASE("sync: kills outside the other demo's coverage are not counted against it")
{
    // The other demo starts later (joined mid-match) and skipped a part.
    auto b = Shifted(kMatch, 0, 0);
    b.kills.erase(b.kills.begin(), b.kills.begin() + 2);  // 10 s and 14 s: before it started
    b.seen = {{18000, 36000}, {45000, 60000}};
    const auto s = SuggestSync(kMatch, b);
    CHECK(s.found);
    CHECK(s.offsetMs == 0);
    CHECK(s.matched == 3);
    CHECK(s.missing == 0);
    CHECK(s.sharedFirst == 18000);
    CHECK(s.sharedLast == 60000);
    CHECK(s.sharedMs == (36000 - 18000) + (60000 - 45000));
}

TEST_CASE("sync: too few kills to compare")
{
    auto one = Feed({{10000, "Bob", "Alice"}}, {{0, 20000}});
    const auto s = SuggestSync(one, one);
    CHECK(!s.found);
    CHECK(s.reason.find("Too few kills") != std::string::npos);
}

TEST_CASE("sync: kill feed of a moment log: confirmed kills and the spans seen")
{
    auto P = [](int slot, const char* name) {
        PlayerObservation o;
        o.clientNum = slot;
        o.name = name;
        o.inRoster = true;
        o.visible = true;
        return o;
    };
    MomentLog log;
    for (int t = 1000; t <= 3000; t += 50)
        log.Add({t, {P(0, "Bob"), P(1, "Alice")}, t == 2000 ? std::vector<KillObservation>{{0, 1, 5, 1}}
                                                              : std::vector<KillObservation>{}});
    // A seek forward: nothing between 3 s and 9 s.
    for (int t = 9000; t <= 9500; t += 50)
        log.Add({t, {P(0, "Bob"), P(1, "Alice")}, {}});
    const auto feed = KillFeedOf(log);
    CHECK(feed.kills.size() == 1);
    CHECK(feed.kills[0].killer == "Bob");
    CHECK(feed.kills[0].victim == "Alice");
    CHECK(feed.kills[0].serverTime == 2000);
    CHECK(feed.seen.size() == 2);
    CHECK(feed.seen[0] == (ServerSpan{1000, 3000}));
    CHECK(feed.seen[1] == (ServerSpan{9000, 9500}));
    CHECK(feed.Covers(2500));
    CHECK(!feed.Covers(5000));
    CHECK(feed.Covers(3100, 200));
}

TEST_CASE("sync: scene-time shift of an angle")
{
    // The angle started recording 2 s after the main demo, same clock.
    CHECK(std::abs(AngleSceneShift(1000, 3000, 0) - 2.0) < 1e-9);
    // Its clock runs 125 s ahead: the same instant.
    CHECK(std::abs(AngleSceneShift(1000, 128000, 125000) - 2.0) < 1e-9);
    // An angle that started earlier has a negative shift.
    CHECK(std::abs(AngleSceneShift(5000, 1000, 0) + 4.0) < 1e-9);
}

// ---------------------------------------------------------------- angles in projects

#include "iris/preflight.hpp"
#include "iris/project.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace
{
    Project TwoAngleProject()
    {
        Project p = MakeDefaultProject("Angles");
        p.demo = DemoRef{};
        p.demo->path = "main.dm_13";
        p.demo->name = "main";
        p.demo->mapName = "mp_rust";
        p.demo->gametype = "dm";
        p.demo->firstServerTime = 1000;
        p.demo->killFeed = kMatch;
        DemoAngle a;
        a.id = "angle-1";
        a.name = "Bob's view";
        a.demo.path = "bob.dm_13";
        a.demo.name = "bob";
        a.demo.mapName = "MP_RUST";
        a.demo.gametype = "dm";
        a.demo.firstServerTime = 3500;
        a.demo.killFeed = Shifted(kMatch, 0, 50);
        a.recorder = "Bob";
        a.offsetMs = 25;
        a.sync = SyncState::Confirmed;
        a.confidence = 1.0;
        p.angles.push_back(a);
        p.useCameraCuts = true;
        p.cuts.AddCut({0.0, "cam-1"});
        p.cuts.AddCut({2.0, "cam-3", "angle-1"});
        p.cuts.AddCut({3.0, "cam-3", "angle-1"});
        p.cuts.AddCut({5.0, "cam-2"});
        return p;
    }

    bool HasIssue(const std::vector<Issue>& issues, const char* text, Issue::Severity severity)
    {
        for (const auto& i : issues)
            if (i.severity == severity && i.message.find(text) != std::string::npos)
                return true;
        return false;
    }
}

TEST_CASE("angles: round trip of angles, kill feeds and cut angles")
{
    const auto p = TwoAngleProject();
    const auto r = ParseProject(SerializeProject(p));
    CHECK(r.project.has_value());
    CHECK(r.warnings.empty());
    CHECK(r.project->angles == p.angles);
    CHECK(r.project->demo == p.demo);
    CHECK(r.project->cuts.Cuts() == p.cuts.Cuts());
    CHECK(r.project->cuts.Cuts()[1].angle == "angle-1");
    CHECK(r.project->FindAngle("angle-1") != nullptr);
    CHECK(r.project->FindAngle("nope") == nullptr);
    // A project without angles writes none (older files stay as they were).
    CHECK(!nlohmann::json::parse(SerializeProject(MakeDefaultProject("x"))).contains("angles"));
}

TEST_CASE("angles: unknown sync state, duplicate ids and missing names are repaired with a warning")
{
    auto text = SerializeProject(TwoAngleProject());
    const auto at = text.find("\"confirmed\"");
    CHECK(at != std::string::npos);
    text.replace(at, 11, "\"perfect\"");
    const auto r = ParseProject(text);
    CHECK(r.project.has_value());
    CHECK(r.project->angles[0].sync == SyncState::Unsynced);
    CHECK(r.warnings.size() == 1);
}

TEST_CASE("angles: segments merge consecutive cuts in one angle")
{
    const auto p = TwoAngleProject();
    const auto segs = AngleSegments(p, 1.0, 8.0);
    CHECK(segs.size() == 3);
    CHECK(segs[0] == (AngleSegment{"", 1.0, 2.0}));
    CHECK(segs[1] == (AngleSegment{"angle-1", 2.0, 5.0}));
    CHECK(segs[2] == (AngleSegment{"", 5.0, 8.0}));
    // Entirely inside the angle.
    const auto inner = AngleSegments(p, 2.5, 4.0);
    CHECK(inner.size() == 1);
    CHECK(inner[0].angle == "angle-1");
    // Without cuts in use, all of it is the main demo.
    auto q = p;
    q.useCameraCuts = false;
    CHECK(AngleSegments(q, 0.0, 8.0) == (std::vector<AngleSegment>{{"", 0.0, 8.0}}));
    CHECK(AngleSegments(q, 3.0, 3.0).empty());
}

TEST_CASE("angles: compatibility with the main demo")
{
    DemoRef main, other;
    main.mapName = "mp_rust";
    main.gametype = "dm";
    other.mapName = "mp_rust";
    other.gametype = "dm";
    CHECK(!AngleIncompatibility(main, other));
    other.mapName = "mp_terminal";
    CHECK(AngleIncompatibility(main, other).has_value());
    other.mapName = "";  // unknown: not a reason
    CHECK(!AngleIncompatibility(main, other));
    other.gametype = "sd";
    CHECK(AngleIncompatibility(main, other).has_value());
    other = main;
    other.path = main.path = "same.dm_13";
    CHECK(AngleIncompatibility(main, other).has_value());
}

TEST_CASE("angles: preflight refuses unsynchronised, removed and foreign angles; warns on unchecked suggestions")
{
    auto p = TwoAngleProject();
    CHECK(!HasIssue(PreflightProject(p, 0.0, 8.0, {}), "Angle", Issue::Severity::Error));

    p.angles[0].sync = SyncState::Suggested;
    p.angles[0].confidence = 0.8;
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "suggested sync (80%", Issue::Severity::Warning));
    // Outside the angle's cuts it does not matter.
    CHECK(!HasIssue(PreflightProject(p, 5.5, 8.0, {}), "suggested sync", Issue::Severity::Warning));

    p.angles[0].sync = SyncState::Unsynced;
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "not synchronised", Issue::Severity::Error));

    p.angles[0].sync = SyncState::Manual;
    p.angles[0].demo.mapName = "mp_terminal";
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "another map", Issue::Severity::Error));

    p.angles.clear();
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "angle that was removed", Issue::Severity::Error));
}

TEST_CASE("angles: saved paths are relative and missing angle demos are reported")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "iw4iris-test-angles";
    fs::remove_all(dir);
    fs::create_directories(dir / "demos");
    { std::ofstream(dir / "demos" / "main.dm_13") << "main"; }
    { std::ofstream(dir / "demos" / "bob.dm_13") << "bob"; }
    auto p = TwoAngleProject();
    p.demo->path = (dir / "demos" / "main.dm_13").string();
    p.angles[0].demo.path = (dir / "demos" / "bob.dm_13").string();
    p.angles[0].demo.fileSize = 3;
    const fs::path file = dir / "angles.iw4iris";
    CHECK(SaveProjectFile(file, p));
    const auto r = LoadProjectFile(file);
    CHECK(r.project.has_value());
    CHECK(r.project->angles[0].demo.path == "demos/bob.dm_13");
    CHECK(FindMissingReferences(*r.project, file).empty());
    fs::remove(dir / "demos" / "bob.dm_13");
    const auto missing = FindMissingReferences(*r.project, file);
    CHECK(missing.size() == 1);
    CHECK(missing[0].find("Bob's view") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("sync: merging kill feeds unites what two partial plays saw")
{
    auto first = Feed({{10000, "Bob", "Alice"}, {14000, "Carl", "Dana"}}, {{5000, 16000}});
    auto second = Feed({{14040, "Carl", "Dana"}, {33000, "Bob", "Dana"}}, {{12000, 20000}, {30000, 40000}});
    const auto m = MergeKillFeeds(first, second);
    CHECK(m.kills.size() == 3);
    CHECK(m.kills[1].serverTime == 14000);  // the earlier copy is kept
    CHECK(m.seen.size() == 2);
    CHECK(m.seen[0] == (ServerSpan{5000, 20000}));
    CHECK(m.seen[1] == (ServerSpan{30000, 40000}));
    CHECK(MergeKillFeeds(m, m) == m);
}

TEST_CASE("angles: render jobs keep the angle they need")
{
    auto p = TwoAngleProject();
    RenderJob job;
    job.id = "job-1";
    job.name = "Bob part";
    job.angle = "angle-1";
    p.renderQueue.push_back(job);
    const auto r = ParseProject(SerializeProject(p));
    CHECK(r.project->renderQueue.size() == 1);
    CHECK(r.project->renderQueue[0].angle == "angle-1");
}

TEST_CASE("angles: a range across angles becomes one render job per segment")
{
    auto p = TwoAngleProject();
    RecordingSettings rec;
    rec.range = RangeMode::Custom;
    rec.customStart = 1.0;
    rec.customEnd = 8.0;
    rec.format = OutputFormat::PngSequence;
    rec.namePattern = "{project}_{date}_{pass}";
    const auto jobs = PlanAngleRenders(p, rec, "20261126-1200");
    CHECK(jobs.size() == 3);
    CHECK(jobs[0].angle.empty());
    CHECK(jobs[1].angle == "angle-1");
    CHECK(jobs[1].settings.range == RangeMode::Custom);
    CHECK(jobs[1].settings.customStart == 2.0);
    CHECK(jobs[1].settings.customEnd == 5.0);
    CHECK(jobs[1].name.find("Bob's view") != std::string::npos);
    // One image sequence: the same name for every part, the date fixed.
    CHECK(jobs[0].settings.namePattern == "{project}_20261126-1200_{pass}");
    CHECK(jobs[2].settings.namePattern == jobs[0].settings.namePattern);
    CHECK(jobs[0].id != jobs[1].id);
    CHECK(jobs[1].id != jobs[2].id);
    // The parts' frames do not overlap and cover the range.
    const auto a = FramesInRange(jobs[0].settings.customStart, jobs[0].settings.customEnd, rec.frameRate);
    const auto b = FramesInRange(jobs[1].settings.customStart, jobs[1].settings.customEnd, rec.frameRate);
    const auto c = FramesInRange(jobs[2].settings.customStart, jobs[2].settings.customEnd, rec.frameRate);
    CHECK(a.first + a.count == b.first);
    CHECK(b.first + b.count == c.first);
    CHECK(c.first + c.count == FramesInRange(1.0, 8.0, rec.frameRate).first + FramesInRange(1.0, 8.0, rec.frameRate).count);
    // A video gives one file per part.
    rec.format = OutputFormat::H264;
    const auto videos = PlanAngleRenders(p, rec, "x");
    CHECK(videos[0].settings.namePattern != videos[1].settings.namePattern);
}

TEST_CASE("angles: a recording's coverage of the match, and preflight on parts it does not cover")
{
    auto p = TwoAngleProject();
    // Unknown until played.
    CHECK(!RecordingCoverage(p, "angle-1"));
    CHECK(!RecordingCoverage(p, ""));
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "was not played yet", Issue::Severity::Warning));

    p.demo->durationSeconds = 60.0;
    // Bob's demo starts 2.5 s after the main one (offset 25 ms) and is 4 s long.
    p.angles[0].demo.durationSeconds = 4.0;
    const auto cover = RecordingCoverage(p, "angle-1");
    CHECK(cover.has_value());
    CHECK(std::abs(cover->start - 2.475) < 1e-9);
    CHECK(std::abs(cover->end - 6.475) < 1e-9);
    CHECK(RecordingCoverage(p, "") == (SceneSpan{0.0, 60.0}));

    // The angle's part is 2 s to 5 s: it starts before Bob's demo does.
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "does not cover all of 2.00 s to 5.00 s", Issue::Severity::Error));
    // Moving the cut inside the coverage fixes it.
    auto cuts = p.cuts.Cuts();
    cuts[1].time = 2.5;
    cuts[2].time = 3.0;
    p.cuts.SetCuts(cuts);
    CHECK(!HasIssue(PreflightProject(p, 0.0, 8.0, {}), "does not cover", Issue::Severity::Error));
    // The main demo's own length counts too.
    p.demo->durationSeconds = 6.0;
    CHECK(HasIssue(PreflightProject(p, 0.0, 8.0, {}), "The main demo does not cover", Issue::Severity::Error));
    // The length round-trips.
    const auto r = ParseProject(SerializeProject(p));
    CHECK(r.project->demo->durationSeconds == p.demo->durationSeconds);
    CHECK(r.project->angles[0].demo.durationSeconds == 4.0);
}
