// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Synchronising several recordings of one match ("angles").
//
// When several players recorded the same match, each client demo holds its
// recorder's exact first-person view. Their snapshots carry the server's
// time, so two demos of one server session normally share a clock; a map
// restart or a different session shifts it. The strongest evidence two
// recordings have in common is the kill feed: an obituary is an event the
// server sends to every client, with the killer's and the victim's names.
//
// SuggestSync compares two kill feeds and proposes the offset at which they
// agree, with the evidence (kills matched, kills only one of them shows in
// the time both cover) and a confidence. It never decides on its own: a
// suggestion is shown, applied by the user and can be confirmed or
// corrected by hand. Recordings it cannot match get no suggestion.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace iris
{
    class MomentLog;

    struct FeedKill
    {
        int serverTime = 0;
        std::string killer;   // empty: the world (a fall, a suicide by the map)
        std::string victim;
        bool operator==(const FeedKill&) const = default;
    };

    struct ServerSpan
    {
        int first = 0;
        int last = 0;
        bool operator==(const ServerSpan&) const = default;
    };

    // The kills a demo was seen to contain, and the server-time spans that
    // were looked at (outside them, a missing kill says nothing: a seek
    // skips frames, a demo may not have been played to its end).
    struct KillFeed
    {
        std::vector<ServerSpan> seen;  // sorted, not overlapping
        std::vector<FeedKill> kills;   // sorted by time
        bool Empty() const { return kills.empty(); }
        bool Covers(int serverTime, int slackMs = 0) const;
        bool operator==(const KillFeed&) const = default;
    };

    // The confirmed kills of a moment log (the game's obituaries) and the
    // span it observed. Inferred moments are not used.
    KillFeed KillFeedOf(const MomentLog& log);

    // Both feeds in one: the spans seen are united, kills seen in both (same
    // names within toleranceMs) are kept once. A demo played again in parts
    // adds to what was seen before instead of replacing it.
    KillFeed MergeKillFeeds(const KillFeed& a, const KillFeed& b, int toleranceMs = 150);

    struct SyncLimits
    {
        int toleranceMs = 150;      // two clients may receive one event a snapshot apart
        int minMatches = 2;         // fewer is a coincidence, not a sync
        double minConfidence = 0.6;
    };

    struct SyncSuggestion
    {
        bool found = false;
        int offsetMs = 0;           // the other recording's server time minus the reference's, at one instant
        int matched = 0;            // kills both show at that offset
        int missing = 0;            // kills in the span both cover that only one of them shows
        double confidence = 0.0;    // matched / (matched + missing)
        int sharedFirst = 0;        // the time both cover, in the reference's server time
        int sharedLast = 0;
        int sharedMs = 0;           // how much of it both cover (gaps excluded)
        std::string reason;         // human-readable: why, or why not
    };

    SyncSuggestion SuggestSync(const KillFeed& reference, const KillFeed& other, const SyncLimits& limits = {});

    // Scene-time shift of an angle: the angle's scene time (seconds since its
    // first snapshot) plus the shift is the main demo's scene time at the
    // same instant. offsetMs is the angle's server time minus the main
    // demo's.
    double AngleSceneShift(int mainFirstServerTime, int angleFirstServerTime, int offsetMs);
}  // namespace iris
