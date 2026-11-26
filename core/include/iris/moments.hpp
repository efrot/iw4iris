// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Moments of a demo: kills, deaths, respawns, players joining and leaving,
// and trickshot candidates, found from what the demo's snapshots contain
// while it plays (normally, or in a fast analysis pass).
//
// Kills come from the game's own obituary events (confirmed). Deaths,
// respawns, joins and leaves are inferred from changes between observed
// frames; when frames were skipped (a seek) their time is marked
// approximate. Trickshot candidates are a deterministic local analysis of
// the shooter's recorded movement around a kill (airborne, spin, scope)
// with the evidence and a confidence shown; nothing is invented, and
// nothing leaves the computer.
//
// Frames can arrive in any order and more than once (seeking back replays
// the demo): everything is keyed by server time and de-duplicated.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iris/players.hpp"

namespace iris
{
    // A kill as the game announced it (obituary event).
    struct KillObservation
    {
        int attacker = -1;  // client slot (-1: the world / unknown)
        int victim = -1;
        int weapon = 0;
        int means = 0;      // means of death
    };

    struct FrameObservation
    {
        int serverTime = 0;
        std::vector<PlayerObservation> players;
        std::vector<KillObservation> kills;  // obituaries in this snapshot
    };

    enum class MomentKind
    {
        Kill,
        Death,
        Respawn,
        Joined,
        Left,
        Trickshot,  // a candidate, with a confidence
    };
    std::string_view ToString(MomentKind kind);

    struct Moment
    {
        MomentKind kind = MomentKind::Kill;
        int serverTime = 0;         // when it happened (the kill, for a trickshot)
        std::string player;         // who (the killer, the one who died / joined ...)
        std::string other;          // the victim of a kill
        int weapon = 0;
        bool confirmed = true;      // from a game event, not inferred
        bool approximate = false;   // inferred across skipped frames: the time is only near
        double confidence = 1.0;    // trickshot candidates: 0..1
        std::string detail;         // human-readable evidence
        int rangeStart = 0;         // a suggested clip (server times)
        int rangeEnd = 0;
    };

    struct TrickshotSettings
    {
        double minSpinDegrees = 180.0;   // or
        double minAirSeconds = 0.6;      // to count as a candidate at all
        int lookBackMs = 3000;           // how far before the kill the jump may start
        int landedGraceMs = 250;         // a kill this soon after landing still counts as airborne
    };

    class MomentLog
    {
    public:
        void Reset();
        void Add(const FrameObservation& frame);

        // Kills, deaths, respawns, joins, leaves, sorted by time.
        const std::vector<Moment>& Moments() const { return moments_; }
        // Trickshot candidates, recomputed from the recorded movement;
        // highest confidence first.
        std::vector<Moment> Trickshots(const TrickshotSettings& settings = {}) const;

        // Server times observed (first / last), for a coverage display.
        std::optional<int> FirstTime() const;
        std::optional<int> LastTime() const;
        // The server-time spans observed: frames closer than maxGapMs
        // together form one span (a seek forward leaves a gap).
        std::vector<std::pair<int, int>> SeenSpans(int maxGapMs = 1000) const;
        std::size_t FrameCount() const { return frames_; }

    private:
        struct Sample
        {
            double yaw = 0.0;
            bool airborne = false;
            bool ads = false;
            bool visible = false;
        };
        struct PlayerTrack
        {
            std::map<int, Sample> samples;   // by server time
            std::map<int, bool> alive;       // by server time: in view and alive
            std::map<int, bool> connected;   // by server time
        };

        void AddMoment(Moment m);
        void Derive(const std::string& name, const PlayerTrack& track);

        std::map<std::string, PlayerTrack> tracks_;  // by player name
        std::vector<Moment> moments_;
        std::vector<Moment> kills_;                  // confirmed kills (also in moments_)
        std::size_t frames_ = 0;
        std::map<int, bool> times_;                  // server times seen
    };
}  // namespace iris
