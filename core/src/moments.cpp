// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/moments.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace iris
{
    namespace
    {
        constexpr int kGapMs = 250;           // frames further apart than this make an inferred time approximate
        constexpr int kKillDedupeMs = 700;    // an obituary stays in a few consecutive snapshots

        bool Inferred(MomentKind k)
        {
            return k == MomentKind::Death || k == MomentKind::Respawn || k == MomentKind::Joined || k == MomentKind::Left;
        }

        double WrapYaw(double d)
        {
            d = std::fmod(d, 360.0);
            if (d > 180.0)
                d -= 360.0;
            if (d < -180.0)
                d += 360.0;
            return d;
        }

        // Transitions of a time-ordered boolean series: (time, new value,
        // approximate).
        struct Transition
        {
            int time;
            bool value;
            bool approximate;
        };
        std::vector<Transition> Transitions(const std::map<int, bool>& series)
        {
            std::vector<Transition> out;
            const std::pair<const int, bool>* prev = nullptr;
            for (const auto& entry : series)
            {
                if (prev && prev->second != entry.second)
                    out.push_back({entry.first, entry.second, entry.first - prev->first > kGapMs});
                prev = &entry;
            }
            return out;
        }
    }  // namespace

    std::string_view ToString(MomentKind kind)
    {
        switch (kind)
        {
            case MomentKind::Kill: return "kill";
            case MomentKind::Death: return "death";
            case MomentKind::Respawn: return "respawn";
            case MomentKind::Joined: return "joined";
            case MomentKind::Left: return "left";
            case MomentKind::Trickshot: return "trickshot";
        }
        return "moment";
    }

    void MomentLog::Reset()
    {
        tracks_.clear();
        moments_.clear();
        kills_.clear();
        frames_ = 0;
        times_.clear();
    }

    std::optional<int> MomentLog::FirstTime() const
    {
        if (times_.empty())
            return std::nullopt;
        return times_.begin()->first;
    }

    std::optional<int> MomentLog::LastTime() const
    {
        if (times_.empty())
            return std::nullopt;
        return times_.rbegin()->first;
    }

    std::vector<std::pair<int, int>> MomentLog::SeenSpans(int maxGapMs) const
    {
        std::vector<std::pair<int, int>> spans;
        for (const auto& [t, seen] : times_)
        {
            (void)seen;
            if (!spans.empty() && t - spans.back().second <= maxGapMs)
                spans.back().second = t;
            else
                spans.emplace_back(t, t);
        }
        return spans;
    }

    void MomentLog::AddMoment(Moment m)
    {
        moments_.push_back(std::move(m));
        std::stable_sort(moments_.begin(), moments_.end(),
                         [](const Moment& a, const Moment& b) { return a.serverTime < b.serverTime; });
    }

    // Re-derives one player's inferred moments from their whole history
    // (cheap enough: only called when a frame lands next to a change).
    void MomentLog::Derive(const std::string& name, const PlayerTrack& track)
    {
        std::erase_if(moments_, [&](const Moment& m) { return Inferred(m.kind) && m.player == name; });
        for (const auto& t : Transitions(track.connected))
        {
            Moment m;
            m.kind = t.value ? MomentKind::Joined : MomentKind::Left;
            m.serverTime = t.time;
            m.player = name;
            m.confirmed = false;
            m.approximate = t.approximate;
            m.rangeStart = t.time - 2000;
            m.rangeEnd = t.time + 2000;
            moments_.push_back(std::move(m));
        }
        for (const auto& t : Transitions(track.alive))
        {
            Moment m;
            m.kind = t.value ? MomentKind::Respawn : MomentKind::Death;
            m.serverTime = t.time;
            m.player = name;
            m.confirmed = false;
            m.approximate = t.approximate;
            m.rangeStart = t.time - 3000;
            m.rangeEnd = t.time + 1500;
            moments_.push_back(std::move(m));
        }
        std::stable_sort(moments_.begin(), moments_.end(),
                         [](const Moment& a, const Moment& b) { return a.serverTime < b.serverTime; });
    }

    void MomentLog::Add(const FrameObservation& frame)
    {
        const int t = frame.serverTime;
        ++frames_;
        times_[t] = true;

        std::map<int, std::string> slotName;
        for (const auto& o : frame.players)
            if (o.clientNum >= 0 && !o.name.empty())
                slotName[o.clientNum] = o.name;

        // Insert a value into a series; true when it can change transitions
        // (it differs from a neighbour).
        auto put = [t](std::map<int, bool>& series, bool value) {
            const auto [it, inserted] = series.insert_or_assign(t, value);
            (void)inserted;
            bool changes = false;
            if (it != series.begin() && std::prev(it)->second != value)
                changes = true;
            if (const auto next = std::next(it); next != series.end() && next->second != value)
                changes = true;
            return changes;
        };

        std::map<std::string, bool> present;
        for (const auto& o : frame.players)
        {
            if (o.name.empty() || o.clientNum < 0)
                continue;
            present[o.name] = true;
            auto& track = tracks_[o.name];
            bool changed = put(track.connected, o.inRoster || o.visible || o.recording);
            if (o.visible)
            {
                track.samples[t] = {o.angles.yaw, o.airborne, o.ads, true};
                changed |= put(track.alive, !o.dead);
            }
            if (changed)
                Derive(o.name, track);
        }
        // Known players missing from this frame are not connected now.
        for (auto& [name, track] : tracks_)
            if (!present.contains(name) && put(track.connected, false))
                Derive(name, track);

        // Kills: the game's obituaries (confirmed), once per kill.
        for (const auto& k : frame.kills)
        {
            const auto attacker = slotName.find(k.attacker);
            const auto victim = slotName.find(k.victim);
            Moment m;
            m.kind = MomentKind::Kill;
            m.serverTime = t;
            m.player = attacker != slotName.end() ? attacker->second : std::string();
            m.other = victim != slotName.end() ? victim->second : std::string();
            m.weapon = k.weapon;
            m.confirmed = true;
            m.rangeStart = t - 3000;
            m.rangeEnd = t + 1500;
            const bool duplicate = std::any_of(kills_.begin(), kills_.end(), [&](const Moment& o) {
                return o.player == m.player && o.other == m.other && std::abs(o.serverTime - t) <= kKillDedupeMs;
            });
            if (duplicate)
                continue;
            m.detail = (m.player.empty() ? std::string("The world") : m.player) + " killed " +
                       (m.other.empty() ? std::string("a player") : m.other);
            kills_.push_back(m);
            AddMoment(std::move(m));
        }
    }

    std::vector<Moment> MomentLog::Trickshots(const TrickshotSettings& settings) const
    {
        std::vector<Moment> out;
        for (const auto& kill : kills_)
        {
            const auto it = tracks_.find(kill.player);
            if (kill.player.empty() || it == tracks_.end())
                continue;
            const auto& samples = it->second.samples;
            // The shooter's state at the kill.
            auto at = samples.upper_bound(kill.serverTime);
            if (at == samples.begin())
                continue;
            --at;
            if (kill.serverTime - at->first > kGapMs)
                continue;  // no movement recorded near the kill
            // Airborne at the kill, or landed just before it.
            int lastAir = -1;
            for (auto s = at;; --s)
            {
                if (kill.serverTime - s->first > settings.landedGraceMs)
                    break;
                if (s->second.airborne)
                {
                    lastAir = s->first;
                    break;
                }
                if (s == samples.begin())
                    break;
            }
            if (lastAir < 0)
                continue;
            // Back to the takeoff.
            int takeoff = lastAir;
            auto s = samples.find(lastAir);
            while (s != samples.begin())
            {
                auto p = std::prev(s);
                if (!p->second.airborne || kill.serverTime - p->first > settings.lookBackMs)
                    break;
                takeoff = p->first;
                s = p;
            }
            // The turn in the air.
            double spin = 0.0;
            const Sample* prev = nullptr;
            for (auto i = samples.lower_bound(takeoff); i != samples.end() && i->first <= kill.serverTime; ++i)
            {
                if (prev)
                    spin += std::abs(WrapYaw(i->second.yaw - prev->yaw));
                prev = &i->second;
            }
            const double air = (kill.serverTime - takeoff) / 1000.0;
            if (spin < settings.minSpinDegrees && air < settings.minAirSeconds)
                continue;
            const bool scoped = at->second.ads;
            Moment m = kill;
            m.kind = MomentKind::Trickshot;
            m.confirmed = false;
            m.confidence = std::clamp(0.35 + 0.35 * std::min(spin / 360.0, 1.5) / 1.5 + 0.2 * std::min(air / 1.0, 1.0) +
                                          (scoped ? 0.0 : 0.1),
                                      0.0, 1.0);
            char buf[200];
            std::snprintf(buf, sizeof(buf), "%s on %s in the air: %.0f deg turn, %.1f s airborne, %s", kill.player.c_str(),
                          kill.other.empty() ? "a player" : kill.other.c_str(), spin, air,
                          scoped ? "scoped" : "not scoped");
            m.detail = buf;
            m.rangeStart = takeoff - 1500;
            m.rangeEnd = kill.serverTime + 2000;
            out.push_back(std::move(m));
        }
        std::stable_sort(out.begin(), out.end(), [](const Moment& a, const Moment& b) { return a.confidence > b.confidence; });
        return out;
    }
}  // namespace iris
