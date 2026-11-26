// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/players.hpp"

#include <algorithm>

namespace iris
{
    double EyeHeight(Stance stance)
    {
        switch (stance)
        {
            case Stance::Crouch: return 40.0;
            case Stance::Prone: return 11.0;
            case Stance::Stand:
            default: return 60.0;
        }
    }

    std::string_view ToString(PlayerAvailability a)
    {
        switch (a)
        {
            case PlayerAvailability::Visible: return "in view";
            case PlayerAvailability::Dead: return "dead";
            case PlayerAvailability::NotInView: return "not in view";
            case PlayerAvailability::Left: return "not connected";
        }
        return "unknown";
    }

    void PlayerRoster::Reset()
    {
        players_.clear();
        nextId_ = 1;
    }

    void PlayerRoster::Update(int serverTime, const std::vector<PlayerObservation>& observations)
    {
        // Everyone starts as "not connected" this frame; observations bring
        // them back.
        std::vector<bool> matched(players_.size(), false);
        for (auto& p : players_)
        {
            p.availability = PlayerAvailability::Left;
            p.latest.reset();
        }

        // Pass 1: the same name in the same slot (the common case), then
        // pass 2: the same name in another slot (a reconnect), then new.
        std::vector<const PlayerObservation*> pending;
        for (const auto& o : observations)
        {
            if (o.clientNum < 0 || (!o.inRoster && !o.visible && !o.recording))
                continue;
            bool found = false;
            for (std::size_t i = 0; i < players_.size(); ++i)
                if (!matched[i] && players_[i].name == o.name && players_[i].clientNum == o.clientNum)
                {
                    matched[i] = true;
                    found = true;
                    auto& p = players_[i];
                    p.team = o.team;
                    p.recording = p.recording || o.recording;
                    p.latest = o;
                    p.lastSeenTime = serverTime;
                    p.availability = !o.visible ? PlayerAvailability::NotInView
                                     : o.dead   ? PlayerAvailability::Dead
                                                : PlayerAvailability::Visible;
                    break;
                }
            if (!found)
                pending.push_back(&o);
        }
        for (const PlayerObservation* o : pending)
        {
            TrackedPlayer* target = nullptr;
            for (std::size_t i = 0; i < players_.size(); ++i)
                if (!matched[i] && players_[i].name == o->name && !o->name.empty())
                {
                    // Prefer somebody of this name who is not connected now
                    // (a reconnect to another slot).
                    target = &players_[i];
                    matched[i] = true;
                    break;
                }
            if (!target)
            {
                TrackedPlayer p;
                p.id = "p" + std::to_string(nextId_++);
                p.name = o->name;
                players_.push_back(std::move(p));
                matched.push_back(true);
                target = &players_.back();
            }
            target->clientNum = o->clientNum;
            target->team = o->team;
            target->recording = target->recording || o->recording;
            target->latest = *o;
            target->lastSeenTime = serverTime;
            target->availability = !o->visible ? PlayerAvailability::NotInView
                                   : o->dead   ? PlayerAvailability::Dead
                                               : PlayerAvailability::Visible;
        }
        std::stable_sort(players_.begin(), players_.end(), [](const TrackedPlayer& a, const TrackedPlayer& b) {
            const bool ac = a.availability != PlayerAvailability::Left, bc = b.availability != PlayerAvailability::Left;
            if (ac != bc)
                return ac;
            return a.clientNum < b.clientNum;
        });
    }

    const TrackedPlayer* PlayerRoster::Find(std::string_view id) const
    {
        for (const auto& p : players_)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    const TrackedPlayer* PlayerRoster::BySlot(int clientNum) const
    {
        for (const auto& p : players_)
            if (p.clientNum == clientNum && p.availability != PlayerAvailability::Left)
                return &p;
        return nullptr;
    }

    const TrackedPlayer* PlayerRoster::Resolve(std::string_view name, int clientNumHint) const
    {
        if (name.empty())
            return BySlot(clientNumHint);
        const TrackedPlayer* any = nullptr;
        for (const auto& p : players_)
        {
            if (p.availability == PlayerAvailability::Left || p.name != name)
                continue;
            if (p.clientNum == clientNumHint)
                return &p;
            if (!any)
                any = &p;
        }
        return any;
    }

    std::optional<PlayerRoster::Eyes> PlayerRoster::EyesOf(const TrackedPlayer& player) const
    {
        if (!player.latest || !player.latest->visible)
            return std::nullopt;
        const auto& o = *player.latest;
        Eyes e;
        const double height = o.eyeHeight >= 0.0 ? o.eyeHeight : EyeHeight(o.stance);
        e.position = o.origin + Vec3{0.0, 0.0, height};
        e.angles = {o.angles.pitch, o.angles.yaw, 0.0};
        e.exact = o.recording;
        return e;
    }

    std::string PlayerRoster::Step(std::string_view currentId, int direction) const
    {
        std::vector<const TrackedPlayer*> connected;
        for (const auto& p : players_)
            if (p.availability != PlayerAvailability::Left)
                connected.push_back(&p);
        if (connected.empty())
            return {};
        std::sort(connected.begin(), connected.end(),
                  [](const TrackedPlayer* a, const TrackedPlayer* b) { return a->clientNum < b->clientNum; });
        const auto n = static_cast<int>(connected.size());
        int at = -1;
        for (int i = 0; i < n; ++i)
            if (connected[static_cast<std::size_t>(i)]->id == currentId)
                at = i;
        if (at < 0)
            return connected[static_cast<std::size_t>(direction >= 0 ? 0 : n - 1)]->id;
        const int next = ((at + (direction >= 0 ? 1 : -1)) % n + n) % n;
        return connected[static_cast<std::size_t>(next)]->id;
    }
}  // namespace iris
