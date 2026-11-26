#include "iris/moments.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    PlayerObservation P(int slot, const char* name, double yaw = 0, bool airborne = false, bool dead = false, bool visible = true)
    {
        PlayerObservation o;
        o.clientNum = slot;
        o.name = name;
        o.inRoster = true;
        o.visible = visible;
        o.dead = dead;
        o.airborne = airborne;
        o.angles = {0, yaw, 0};
        return o;
    }

    FrameObservation F(int t, std::vector<PlayerObservation> players, std::vector<KillObservation> kills = {})
    {
        return {t, std::move(players), std::move(kills)};
    }

    int Count(const MomentLog& log, MomentKind kind)
    {
        int n = 0;
        for (const auto& m : log.Moments())
            n += m.kind == kind;
        return n;
    }
}

TEST_CASE("moments: kills are confirmed once; deaths and respawns are inferred")
{
    MomentLog log;
    log.Add(F(1000, {P(0, "Me"), P(1, "Foe")}));
    // The obituary stays in three consecutive snapshots.
    for (int t : {1050, 1100, 1150})
        log.Add(F(t, {P(0, "Me"), P(1, "Foe", 0, false, true)}, {{0, 1, 7, 1}}));
    log.Add(F(3000, {P(0, "Me"), P(1, "Foe")}));
    CHECK(Count(log, MomentKind::Kill) == 1);
    CHECK(Count(log, MomentKind::Death) == 1);
    CHECK(Count(log, MomentKind::Respawn) == 1);
    const auto& kill = *std::find_if(log.Moments().begin(), log.Moments().end(),
                                     [](const Moment& m) { return m.kind == MomentKind::Kill; });
    CHECK(kill.player == "Me");
    CHECK(kill.other == "Foe");
    CHECK(kill.confirmed);
    CHECK(kill.weapon == 7);
    const auto& death = *std::find_if(log.Moments().begin(), log.Moments().end(),
                                      [](const Moment& m) { return m.kind == MomentKind::Death; });
    CHECK(death.serverTime == 1050);
    CHECK(!death.confirmed);
    CHECK(!death.approximate);
    const auto& respawn = *std::find_if(log.Moments().begin(), log.Moments().end(),
                                        [](const Moment& m) { return m.kind == MomentKind::Respawn; });
    CHECK(respawn.approximate);  // 1150 -> 3000: frames were skipped
    // Sorted by time.
    for (std::size_t i = 1; i < log.Moments().size(); ++i)
        CHECK(log.Moments()[i - 1].serverTime <= log.Moments()[i].serverTime);
}

TEST_CASE("moments: replaying frames (seeking back) adds nothing twice")
{
    MomentLog log;
    auto play = [&] {
        log.Add(F(1000, {P(0, "Me"), P(1, "Foe")}));
        log.Add(F(1050, {P(0, "Me"), P(1, "Foe", 0, false, true)}, {{0, 1, 7, 1}}));
        log.Add(F(1100, {P(0, "Me")}));  // Foe left
    };
    play();
    const auto first = log.Moments().size();
    play();
    play();
    CHECK(log.Moments().size() == first);
    CHECK(Count(log, MomentKind::Left) == 1);
    CHECK(log.FirstTime() == 1000);
    CHECK(log.LastTime() == 1100);
}

TEST_CASE("moments: frames out of order give the same result")
{
    MomentLog a, b;
    const std::vector<FrameObservation> frames = {F(1000, {P(0, "Me"), P(1, "Foe")}),
                                                  F(1050, {P(0, "Me"), P(1, "Foe", 0, false, true)}),
                                                  F(1100, {P(0, "Me"), P(1, "Foe", 0, false, true)}),
                                                  F(1150, {P(0, "Me"), P(1, "Foe")})};
    for (const auto& f : frames)
        a.Add(f);
    for (auto it = frames.rbegin(); it != frames.rend(); ++it)
        b.Add(*it);
    CHECK(a.Moments().size() == b.Moments().size());
    for (std::size_t i = 0; i < a.Moments().size(); ++i)
    {
        CHECK(a.Moments()[i].kind == b.Moments()[i].kind);
        CHECK(a.Moments()[i].serverTime == b.Moments()[i].serverTime);
    }
}

TEST_CASE("moments: a spinning airborne kill is a trickshot candidate, a ground kill is not")
{
    MomentLog log;
    // Ground kill at 1000.
    log.Add(F(950, {P(0, "Me", 0), P(1, "A")}));
    log.Add(F(1000, {P(0, "Me", 5), P(1, "A", 0, false, true)}, {{0, 1, 3, 1}}));
    // A jump from 2000, 30 degrees per 50 ms frame (a 540 degree spin), kill at 2900 in the air.
    double yaw = 0;
    for (int t = 1900; t < 2000; t += 50)
        log.Add(F(t, {P(0, "Me", yaw), P(2, "B")}));
    for (int t = 2000; t <= 2900; t += 50)
    {
        yaw += 30;
        std::vector<KillObservation> kills;
        if (t == 2900)
            kills.push_back({0, 2, 9, 1});
        log.Add(F(t, {P(0, "Me", yaw, true), P(2, "B", 0, false, t == 2900)}, kills));
    }
    const auto ts = log.Trickshots();
    CHECK(ts.size() == 1);
    CHECK(ts[0].player == "Me");
    CHECK(ts[0].other == "B");
    CHECK(!ts[0].confirmed);
    CHECK(ts[0].confidence > 0.6);
    CHECK(ts[0].confidence <= 1.0);
    CHECK(ts[0].detail.find("540 deg") != std::string::npos);
    CHECK(ts[0].rangeStart < 2000);
    CHECK(ts[0].rangeEnd > 2900);

    // A kill right after landing still counts; a long time after does not.
    TrickshotSettings strict;
    strict.landedGraceMs = 0;
    CHECK(!log.Trickshots(strict).empty());  // this one was in the air
}

TEST_CASE("moments: kills without movement data are not guessed as trickshots")
{
    MomentLog log;
    log.Add(F(1000, {P(0, "Me", 0, true, false, false), P(1, "A")}, {{0, 1, 3, 1}}));  // shooter not in view
    log.Add(F(1050, {P(3, "X")}, {{5, 3, 3, 1}}));  // attacker slot unknown
    CHECK(log.Trickshots().empty());
    log.Reset();
    CHECK(log.Moments().empty());
    CHECK(!log.FirstTime());
}
