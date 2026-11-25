#include "iris/timeline_view.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("timeline view: typed times")
{
    const FrameRate r30{30, 1};
    CHECK_NEAR(*ParseTime("12.5", r30), 12.5, 1e-9);
    CHECK_NEAR(*ParseTime(" 12.5s ", r30), 12.5, 1e-9);
    CHECK_NEAR(*ParseTime(".5", r30), 0.5, 1e-9);
    CHECK_NEAR(*ParseTime("90f", r30), 3.0, 1e-9);
    CHECK_NEAR(*ParseTime("1:05.25", r30), 65.25, 1e-9);
    CHECK_NEAR(*ParseTime("1:02:03", r30), 3723.0, 1e-9);
    CHECK_NEAR(*ParseTime("00:01:02:15", r30), 62.5, 1e-9);
    CHECK_NEAR(*ParseTime("-2", r30), -2.0, 1e-9);
    // What FormatTime shows parses back to the same frame.
    for (double t : {0.0, 1.0 / 30, 61.5, 3599.9})
        CHECK_NEAR(*ParseTime(FormatTime(t, r30, TimeDisplay::Timecode), r30), r30.SnapSeconds(t), 1e-6);
    CHECK_NEAR(*ParseTime(FormatTime(2.0, r30, TimeDisplay::Frames), r30, TimeDisplay::Frames), 2.0, 1e-9);
    CHECK_NEAR(*ParseTime(FormatTime(1.25, r30, TimeDisplay::Seconds), r30, TimeDisplay::Seconds), 1.25, 1e-9);
    // Rejected.
    for (const char* bad : {"", "  ", "abc", "1:60", "1:2:3:4:5", "00:00:00:30", "1..2", "f", "-", "1,5", "12x"})
        CHECK_MSG(!ParseTime(bad, r30), bad);
    CHECK(!ParseTime("1", FrameRate{0, 1}));
}

TEST_CASE("timeline view: zoom keeps the pivot and respects limits")
{
    const TimeWindowLimits limits{0.0, 100.0, 0.1, 1000.0};
    const TimeWindow w{10.0, 20.0};
    const auto z = ZoomWindow(w, 15.0, 0.5, limits);
    CHECK_NEAR(z.start, 12.5, 1e-9);
    CHECK_NEAR(z.end, 17.5, 1e-9);
    // Pivot at the left edge stays there.
    const auto z2 = ZoomWindow(w, 10.0, 2.0, limits);
    CHECK_NEAR(z2.start, 10.0, 1e-9);
    CHECK_NEAR(z2.end, 30.0, 1e-9);
    // Minimum and maximum span.
    CHECK_NEAR(ZoomWindow(w, 15.0, 1e-9, limits).Span(), 0.1, 1e-9);
    CHECK_NEAR(ZoomWindow(w, 15.0, 1e9, limits).Span(), 1000.0, 1e-6);
    // Bad factors change nothing.
    CHECK(ZoomWindow(w, 15.0, 0.0, limits) == w);
    CHECK(ZoomWindow(w, 15.0, -1.0, limits) == w);
}

TEST_CASE("timeline view: panning cannot lose the timeline")
{
    const TimeWindowLimits limits{0.0, 60.0, 0.1, 1000.0};
    const TimeWindow w{0.0, 10.0};
    CHECK(PanWindow(w, 5.0, limits) == (TimeWindow{5.0, 15.0}));
    // Far to the right: at least a quarter of the window still overlaps.
    const auto right = PanWindow(w, 1e6, limits);
    CHECK_NEAR(right.start, 57.5, 1e-9);
    CHECK_NEAR(right.Span(), 10.0, 1e-9);
    const auto left = PanWindow(w, -1e6, limits);
    CHECK_NEAR(left.end, 2.5, 1e-9);
    // Garbage windows recover.
    const auto bad = ClampWindow({std::nan(""), 5.0}, limits);
    CHECK(std::isfinite(bad.start));
    CHECK(bad.end > bad.start);
    const auto swapped = ClampWindow({20.0, 10.0}, limits);
    CHECK_NEAR(swapped.start, 10.0, 1e-9);
    CHECK_NEAR(swapped.end, 20.0, 1e-9);
}

TEST_CASE("timeline view: following the playhead pages the window")
{
    const TimeWindowLimits limits{0.0, 100.0, 0.1, 1000.0};
    const TimeWindow w{0.0, 10.0};
    CHECK(FollowTime(w, 5.0, limits) == w);
    const auto paged = FollowTime(w, 9.9, limits);  // inside the right margin
    CHECK_NEAR(paged.start, 9.4, 1e-9);
    CHECK_NEAR(paged.Span(), 10.0, 1e-9);
    const auto back = FollowTime({50.0, 60.0}, 20.0, limits);
    CHECK_NEAR(back.start, 19.5, 1e-9);
    const auto frame = WindowAround(10.0, 20.0, limits, 0.1);
    CHECK_NEAR(frame.start, 9.0, 1e-9);
    CHECK_NEAR(frame.end, 21.0, 1e-9);
}

TEST_CASE("timeline view: magnetic snapping and edit points")
{
    const std::vector<double> points{1.0, 2.0, 5.0};
    bool snapped = false;
    CHECK_NEAR(SnapToNearest(2.04, points, 0.05, &snapped), 2.0, 1e-12);
    CHECK(snapped);
    CHECK_NEAR(SnapToNearest(3.0, points, 0.05, &snapped), 3.0, 1e-12);
    CHECK(!snapped);
    CHECK_NEAR(SnapToNearest(1.52, points, 1.0), 2.0, 1e-12);  // the nearest wins
    CHECK_NEAR(*PreviousPoint(2.0, points), 1.0, 1e-12);
    CHECK_NEAR(*NextPoint(2.0, points), 5.0, 1e-12);
    CHECK(!PreviousPoint(1.0, points));
    CHECK(!NextPoint(5.0, points));
    CHECK(!NextPoint(0.0, {}));
}

TEST_CASE("timeline view: moving the in/out range keeps its length")
{
    CHECK(MoveRange({2.0, 4.0}, 3.0, 0.0, 10.0) == (InOutRange{5.0, 7.0}));
    CHECK(MoveRange({2.0, 4.0}, 100.0, 0.0, 10.0) == (InOutRange{8.0, 10.0}));
    CHECK(MoveRange({2.0, 4.0}, -100.0, 0.0, 10.0) == (InOutRange{0.0, 2.0}));
    CHECK(MoveRange({4.0, 2.0}, 0.0, 0.0, 10.0) == (InOutRange{2.0, 4.0}));
    // A range longer than the timeline is cut to it.
    CHECK(MoveRange({0.0, 30.0}, 1.0, 0.0, 10.0) == (InOutRange{0.0, 10.0}));
}
