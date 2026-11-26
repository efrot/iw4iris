// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/sync.hpp"

#include "iris/moments.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace iris
{
    namespace
    {
        // The parts of the reference's spans the other recording also
        // covers, with the other's spans moved onto the reference's clock.
        std::vector<ServerSpan> SharedSpans(const KillFeed& reference, const KillFeed& other, int offsetMs)
        {
            std::vector<ServerSpan> out;
            for (const auto& a : reference.seen)
                for (const auto& b : other.seen)
                {
                    const int first = std::max(a.first, b.first - offsetMs);
                    const int last = std::min(a.last, b.last - offsetMs);
                    if (first <= last)
                        out.push_back({first, last});
                }
            std::sort(out.begin(), out.end(), [](const ServerSpan& x, const ServerSpan& y) { return x.first < y.first; });
            return out;
        }

        bool InSpans(const std::vector<ServerSpan>& spans, int t)
        {
            return std::any_of(spans.begin(), spans.end(), [t](const ServerSpan& s) { return t >= s.first && t <= s.last; });
        }

        bool SameKill(const FeedKill& a, const FeedKill& b)
        {
            return a.killer == b.killer && a.victim == b.victim;
        }

        std::string Seconds(int ms)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%+.2f s", ms / 1000.0);
            return text;
        }

        struct Evaluation
        {
            int matched = 0;
            int missing = 0;
            std::vector<ServerSpan> shared;
        };

        // Kills in the time both cover, paired one to one at the offset.
        Evaluation Evaluate(const KillFeed& reference, const KillFeed& other, int offsetMs, int toleranceMs)
        {
            Evaluation e;
            e.shared = SharedSpans(reference, other, offsetMs);
            std::vector<bool> used(other.kills.size(), false);
            for (const auto& r : reference.kills)
            {
                if (!InSpans(e.shared, r.serverTime))
                    continue;
                bool found = false;
                for (std::size_t i = 0; i < other.kills.size() && !found; ++i)
                {
                    const auto& o = other.kills[i];
                    if (!used[i] && SameKill(r, o) && std::abs(o.serverTime - offsetMs - r.serverTime) <= toleranceMs)
                    {
                        used[i] = true;
                        found = true;
                    }
                }
                if (found)
                    ++e.matched;
                else
                    ++e.missing;
            }
            for (std::size_t i = 0; i < other.kills.size(); ++i)
                if (!used[i] && InSpans(e.shared, other.kills[i].serverTime - offsetMs))
                    ++e.missing;
            return e;
        }
    }  // namespace

    bool KillFeed::Covers(int serverTime, int slackMs) const
    {
        return std::any_of(seen.begin(), seen.end(), [&](const ServerSpan& s) {
            return serverTime >= s.first - slackMs && serverTime <= s.last + slackMs;
        });
    }

    KillFeed KillFeedOf(const MomentLog& log)
    {
        KillFeed feed;
        for (const auto& [first, last] : log.SeenSpans())
            feed.seen.push_back({first, last});
        for (const auto& m : log.Moments())
            if (m.kind == MomentKind::Kill && m.confirmed && !m.other.empty())
                feed.kills.push_back({m.serverTime, m.player, m.other});
        std::stable_sort(feed.kills.begin(), feed.kills.end(),
                         [](const FeedKill& a, const FeedKill& b) { return a.serverTime < b.serverTime; });
        return feed;
    }

    KillFeed MergeKillFeeds(const KillFeed& a, const KillFeed& b, int toleranceMs)
    {
        KillFeed out;
        std::vector<ServerSpan> spans = a.seen;
        spans.insert(spans.end(), b.seen.begin(), b.seen.end());
        std::sort(spans.begin(), spans.end(), [](const ServerSpan& x, const ServerSpan& y) { return x.first < y.first; });
        for (const auto& span : spans)
            if (!out.seen.empty() && span.first <= out.seen.back().last)
                out.seen.back().last = std::max(out.seen.back().last, span.last);
            else
                out.seen.push_back(span);
        out.kills = a.kills;
        for (const auto& k : b.kills)
        {
            const bool known = std::any_of(a.kills.begin(), a.kills.end(), [&](const FeedKill& o) {
                return SameKill(o, k) && std::abs(o.serverTime - k.serverTime) <= toleranceMs;
            });
            if (!known)
                out.kills.push_back(k);
        }
        std::stable_sort(out.kills.begin(), out.kills.end(),
                         [](const FeedKill& x, const FeedKill& y) { return x.serverTime < y.serverTime; });
        return out;
    }

    SyncSuggestion SuggestSync(const KillFeed& reference, const KillFeed& other, const SyncLimits& limits)
    {
        SyncSuggestion s;
        const int tol = std::max(0, limits.toleranceMs);
        const auto minMatches = static_cast<std::size_t>(std::max(1, limits.minMatches));
        if (reference.kills.size() < minMatches || other.kills.size() < minMatches)
        {
            s.reason = "Too few kills seen to compare (" + std::to_string(reference.kills.size()) + " and " +
                       std::to_string(other.kills.size()) + "; at least " + std::to_string(minMatches) +
                       " each): play or analyse both demos further.";
            return s;
        }

        // Every pairing of the same killer and victim proposes an offset;
        // the offset most pairings agree on (within the tolerance) wins.
        std::vector<int> deltas;
        for (const auto& r : reference.kills)
            for (const auto& o : other.kills)
                if (SameKill(r, o))
                    deltas.push_back(o.serverTime - r.serverTime);
        if (deltas.empty())
        {
            s.reason = "The two demos have no kill in common (same killer and victim): they do not look like "
                       "recordings of one match.";
            return s;
        }
        std::sort(deltas.begin(), deltas.end());
        std::size_t bestBegin = 0, bestEnd = 0;
        for (std::size_t i = 0, j = 0; i < deltas.size(); ++i)
        {
            while (j < deltas.size() && deltas[j] - deltas[i] <= 2 * tol)
                ++j;
            const std::size_t count = j - i, best = bestEnd - bestBegin;
            const auto median = [&](std::size_t a, std::size_t b) { return std::abs(deltas[a + (b - a) / 2]); };
            if (count > best || (count == best && median(i, j) < median(bestBegin, bestEnd)))
            {
                bestBegin = i;
                bestEnd = j;
            }
        }
        const int offset = deltas[bestBegin + (bestEnd - bestBegin) / 2];

        const auto e = Evaluate(reference, other, offset, tol);
        s.offsetMs = offset;
        s.matched = e.matched;
        s.missing = e.missing;
        s.confidence = e.matched + e.missing > 0 ? static_cast<double>(e.matched) / (e.matched + e.missing) : 0.0;
        if (!e.shared.empty())
        {
            s.sharedFirst = e.shared.front().first;
            s.sharedLast = e.shared.back().last;
            for (const auto& span : e.shared)
                s.sharedMs += span.last - span.first;
        }
        const std::string evidence = std::to_string(e.matched) + " kill(s) match, " + std::to_string(e.missing) +
                                     " in the shared time only one demo shows";
        if (e.shared.empty())
        {
            s.reason = "At the best offset (" + Seconds(offset) + ") the demos cover no common time.";
            return s;
        }
        if (static_cast<std::size_t>(e.matched) < minMatches)
        {
            s.reason = "Only " + evidence + ": not enough to synchronise. Set the offset by hand.";
            return s;
        }
        if (s.confidence < limits.minConfidence)
        {
            s.reason = "Best offset " + Seconds(offset) + ", but " + evidence + ": the demos disagree too much.";
            return s;
        }
        s.found = true;
        s.reason = (std::abs(offset) <= tol ? std::string("Same server clock (") + Seconds(offset) + "): "
                                            : "Offset " + Seconds(offset) + ": ") +
                   evidence + ".";
        return s;
    }

    double AngleSceneShift(int mainFirstServerTime, int angleFirstServerTime, int offsetMs)
    {
        return (static_cast<double>(angleFirstServerTime) - offsetMs - mainFirstServerTime) / 1000.0;
    }
}  // namespace iris
