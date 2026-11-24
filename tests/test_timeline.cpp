#include "iris/timeline.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("timeline: frame rates convert exactly")
{
    for (const auto& rate : StandardFrameRates())
        for (std::int64_t f : {0LL, 1LL, 59LL, 1001LL, 123456LL, 2000000LL})
            CHECK(rate.SecondsToFrame(rate.FrameToSeconds(f)) == f);
    const FrameRate ntsc{24000, 1001};
    CHECK_NEAR(ntsc.FrameToSeconds(24000), 1001.0, 1e-9);
    CHECK(FrameRate::FromFps(23.976) == (FrameRate{24000, 1001}));
    CHECK(FrameRate::FromFps(59.94) == (FrameRate{60000, 1001}));
    CHECK(FrameRate::FromFps(30.0) == (FrameRate{30, 1}));
    CHECK(FrameRate::FromFps(12.5) == (FrameRate{25, 2}));
    CHECK(FrameRate{24000, 1001}.ToString() == "23.976");
    CHECK(FrameRate{60, 1}.ToString() == "60");
}

TEST_CASE("timeline: time formatting")
{
    const FrameRate r30{30, 1};
    CHECK(FormatTime(61.5, r30, TimeDisplay::Timecode) == "00:01:01:15");
    CHECK(FormatTime(2.0, r30, TimeDisplay::Frames) == "60");
    CHECK(FormatTime(1.25, r30, TimeDisplay::Seconds) == "1.250s");
    CHECK(FormatTime(3661.0, FrameRate{120, 1}, TimeDisplay::Timecode) == "01:01:01:000");
}

TEST_CASE("timeline: frames of a range")
{
    auto r = FramesInRange(0.0, 1.0, FrameRate{60, 1});
    CHECK(r.first == 0);
    CHECK(r.count == 60);
    r = FramesInRange(0.5, 1.0, FrameRate{24, 1});
    CHECK(r.first == 12);
    CHECK(r.count == 12);
    r = FramesInRange(0.01, 0.05, FrameRate{60, 1});
    CHECK(r.first == 1);
    CHECK(r.count == 2);  // frames starting at 1/60 and 2/60
    CHECK(FramesInRange(1.0, 1.0, FrameRate{60, 1}).count == 0);
}

TEST_CASE("timeline: time remap integrates the speed curve")
{
    TimeRemap remap;
    CHECK_NEAR(remap.SceneTime(3.0), 3.0, 1e-12);
    remap.sceneOffset = 10.0;
    CHECK_NEAR(remap.SceneTime(2.0), 12.0, 1e-12);

    ScalarKey a;
    a.time = 0;
    a.value = 1.0;
    a.interpolation = Interpolation::Linear;
    ScalarKey b = a;
    b.time = 2;
    b.value = 0.0;  // decelerate to a freeze
    remap.Speed().AddKey(a);
    remap.Speed().AddKey(b);
    CHECK_NEAR(remap.SceneTime(2.0), 11.0, 1e-9);
    CHECK_NEAR(remap.SceneTime(5.0), 11.0, 1e-9);  // frozen after the ramp
    CHECK_NEAR(remap.CinematicTime(10.75, 10.0), 1.0, 1e-6);
    CHECK_NEAR(remap.SpeedAt(1.0), 0.5, 1e-12);
}

TEST_CASE("timeline: camera cuts")
{
    CameraCutTrack cuts;
    CHECK(!cuts.CameraAt(0).has_value());
    cuts.AddCut({2.0, "b"});
    cuts.AddCut({0.0, "a"});
    cuts.AddCut({5.0, "c"});
    CHECK(cuts.CameraAt(-1.0) == "a");
    CHECK(cuts.CameraAt(1.99) == "a");
    CHECK(cuts.CameraAt(2.0) == "b");
    CHECK(cuts.CameraAt(9.0) == "c");
    cuts.AddCut({2.0, "z"});
    CHECK(cuts.Cuts().size() == 3);
    CHECK(cuts.CameraAt(3.0) == "z");
}

TEST_CASE("timeline: playback advance loops and clamps")
{
    TimelineSettings t;
    t.start = 0;
    t.end = 10;
    t.loop = {true, 2.0, 4.0};
    CHECK_NEAR(t.Advance(3.9, 0.2), 2.1, 1e-9);
    CHECK_NEAR(t.Advance(1.0, 0.5), 1.5, 1e-12);
    t.loop.enabled = false;
    CHECK_NEAR(t.Advance(9.9, 1.0), 10.0, 1e-12);
    t.inPoint = 12;
    t.outPoint = -3;
    t.Normalize();
    CHECK(t.inPoint <= t.outPoint);
    CHECK(t.inPoint >= t.start);
    CHECK(t.outPoint <= t.end);
}
