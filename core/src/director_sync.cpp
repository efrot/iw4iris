// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/director_sync.hpp"

#include <cstdio>
#include <set>

namespace iris::director
{
    const char* ToString(Facet f)
    {
        switch (f)
        {
            case Facet::Spawn: return "spawn";
            case Facet::Weapon: return "weapon";
            case Facet::Attachments: return "attachments";
            case Facet::Animation: return "animation";
            case Facet::Link: return "link";
            case Facet::Delete: return "delete";
        }
        return "spawn";
    }

    const char* ToString(ObjectState s)
    {
        switch (s)
        {
            case ObjectState::Missing: return "missing";
            case ObjectState::Spawning: return "spawning";
            case ObjectState::Ready: return "ready";
            case ObjectState::Updating: return "updating";
            case ObjectState::Waiting: return "waiting";
            case ObjectState::Failed: return "failed";
            case ObjectState::Deleting: return "deleting";
        }
        return "missing";
    }

    namespace
    {
        std::string Num(double v)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.6f", v);
            return buf;
        }

        int GenerationOf(const std::string& id)
        {
            // "g<generation>.<serial>"
            if (id.size() < 3 || id[0] != 'g')
                return -1;
            int g = 0;
            std::size_t i = 1;
            for (; i < id.size() && id[i] >= '0' && id[i] <= '9'; ++i)
                g = g * 10 + (id[i] - '0');
            return i < id.size() && id[i] == '.' && i > 1 ? g : -1;
        }

        constexpr Facet kDependents[] = {Facet::Weapon, Facet::Attachments, Facet::Animation, Facet::Link};
    }  // namespace

    Reconciler::Reconciler(Config config) : config_(config) {}

    void Reconciler::BeginGeneration()
    {
        ++generation_;
        objects_.clear();
        queue_.clear();
        inflight_.clear();
        backpressure_ = false;
        restartAnimations_ = false;
    }

    void Reconciler::RestartAnimations() { restartAnimations_ = true; }

    bool Reconciler::Matches(const FacetState& f, const std::string& value)
    {
        return f.hasConfirmed && f.confirmed == value;
    }

    bool Reconciler::Queue(Object& o, const std::string& object, Facet facet, const std::string& value,
                           std::vector<std::string> message, double)
    {
        if (queue_.size() >= config_.maxQueue)
        {
            backpressure_ = true;
            return false;
        }
        FacetState& f = o.facets[facet];
        Request r;
        r.id = "g" + std::to_string(generation_) + "." + std::to_string(++serial_);
        r.object = object;
        r.facet = facet;
        r.value = value;
        message.insert(message.begin() + 1, r.id);
        r.message = std::move(message);
        f.inflightId = r.id;
        f.inflightValue = value;
        f.sentAt = -1.0;
        f.epoch = o.epoch;
        queue_.push_back(std::move(r));
        return true;
    }

    void Reconciler::Want(Object& o, const std::string& object, Facet facet, const std::string& value,
                          const std::vector<std::string>& message, double now)
    {
        FacetState& f = o.facets[facet];
        f.desired = value;
        f.wanted = true;
        if (Matches(f, value) || !f.inflightId.empty())
            return;
        if (f.failedValue == value && f.attempts > 0)
        {
            if (f.attempts >= config_.maxAttempts || now < f.retryAt)
                return;
        }
        else if (f.failedValue != value)
        {
            f.failedValue.clear();
            f.attempts = 0;
            f.lastError.clear();
        }
        Queue(o, object, facet, value, message, now);
    }

    void Reconciler::Fail(Object& o, FacetState& f, const std::string& error, double now)
    {
        f.failedValue = f.inflightValue;
        ++f.attempts;
        f.retryAt = now + config_.retryDelay;
        f.lastError = error;
        f.inflightId.clear();
        f.inflightValue.clear();
        f.sentAt = -1.0;
        (void)o;
    }

    void Reconciler::ExpireTimeouts(double now)
    {
        for (auto it = inflight_.begin(); it != inflight_.end();)
        {
            const Request& r = it->second;
            auto obj = objects_.find(r.object);
            if (obj == objects_.end())
            {
                it = inflight_.erase(it);
                continue;
            }
            FacetState& f = obj->second.facets[r.facet];
            if (f.inflightId == r.id && f.sentAt >= 0.0 && now - f.sentAt > config_.timeout)
            {
                ++timedOut_;
                char buf[160];
                std::snprintf(buf, sizeof buf, "no answer within %.0f s (the Director Mod may have stopped; see the console)",
                              config_.timeout);
                Fail(obj->second, f, buf, now);
                if (r.facet == Facet::Spawn)
                {
                    // We cannot know whether the entity exists: treat it as gone.
                    f.hasConfirmed = false;
                    for (const Facet d : kDependents)
                        obj->second.facets[d].hasConfirmed = false;
                }
                it = inflight_.erase(it);
            }
            else
                ++it;
        }
    }

    void Reconciler::Reconcile(const Desired& desired, double now)
    {
        ExpireTimeouts(now);
        backpressure_ = false;
        std::set<std::string> wanted;
        for (auto& [id, o] : objects_)
        {
            o.waiting.clear();
            for (auto& [facet, f] : o.facets)
                f.wanted = false;
        }

        auto spawnReady = [&](const Object& o, const std::string& value) {
            const auto it = o.facets.find(Facet::Spawn);
            return it != o.facets.end() && Matches(it->second, value) && it->second.inflightId.empty();
        };

        for (const auto& a : desired.actors)
        {
            wanted.insert(a.id);
            Object& o = objects_[a.id];
            o.actor = true;
            if (o.deleting)
                continue;  // spawned again once the delete is answered
            const std::string spawn = "actor|" + a.body + "|" + a.head;
            Want(o, a.id, Facet::Spawn, spawn, {"spawnActor", a.id, a.body, a.head}, now);
            if (!spawnReady(o, spawn))
                continue;
            Want(o, a.id, Facet::Weapon, a.weapon + "|" + a.weaponTag + "|" + std::to_string(a.camo),
                 {"setWeapon", a.id, a.weapon, a.weaponTag, std::to_string(a.camo)}, now);
            std::string key;
            std::vector<std::string> msg{"setAttachments", a.id, std::to_string(a.attachments.size())};
            for (const auto& at : a.attachments)
            {
                key += at.kind + "|" + at.name + "|" + at.tag + "|" + std::to_string(at.camo) + ";";
                msg.insert(msg.end(), {at.kind, at.name, at.tag, std::to_string(at.camo)});
            }
            Want(o, a.id, Facet::Attachments, key, msg, now);
            if (!a.anim.empty())
            {
                FacetState& f = o.facets[Facet::Animation];
                if (restartAnimations_ && f.inflightId.empty())
                    f.hasConfirmed = false;
                Want(o, a.id, Facet::Animation, a.anim + "@" + Num(a.animStart), {"anim", a.id, a.anim}, now);
            }
        }

        for (const auto& p : desired.props)
        {
            wanted.insert(p.id);
            Object& o = objects_[p.id];
            o.actor = false;
            if (o.deleting)
                continue;
            const std::string spawn = "prop|" + p.model;
            Want(o, p.id, Facet::Spawn, spawn, {"spawnProp", p.id, p.model}, now);
            if (!spawnReady(o, spawn))
                continue;
            if (p.linkActor.empty())
            {
                // Unlink only when it is linked now.
                const auto it = o.facets.find(Facet::Link);
                if (it != o.facets.end() && it->second.hasConfirmed && !it->second.confirmed.empty())
                    Want(o, p.id, Facet::Link, "", {"unlinkProp", p.id}, now);
                continue;
            }
            const auto target = objects_.find(p.linkActor);
            if (target == objects_.end() || !Exists(p.linkActor) || target->second.deleting ||
                !target->second.facets[Facet::Spawn].inflightId.empty())
            {
                o.waiting = "waits for the actor it is attached to";
                continue;
            }
            // The target's spawn epoch is part of the value: a respawned actor
            // needs the link again.
            Want(o, p.id, Facet::Link, p.linkActor + "|" + p.linkTag + "|" + std::to_string(target->second.epoch),
                 {"linkProp", p.id, p.linkActor, p.linkTag}, now);
        }

        // Objects that are no longer wanted.
        for (auto it = objects_.begin(); it != objects_.end();)
        {
            Object& o = it->second;
            if (wanted.contains(it->first))
            {
                ++it;
                continue;
            }
            const FacetState& sp = o.facets[Facet::Spawn];
            const bool engineMayHaveIt = sp.hasConfirmed || !sp.inflightId.empty();
            if (!engineMayHaveIt && !o.deleting)
            {
                it = objects_.erase(it);
                continue;
            }
            if (!o.deleting && sp.inflightId.empty())
            {
                if (Queue(o, it->first, Facet::Delete, "delete", {"delete", it->first}, now))
                    o.deleting = true;
            }
            ++it;
        }
        restartAnimations_ = false;
    }

    std::optional<std::vector<std::string>> Reconciler::NextRequest(double now)
    {
        ExpireTimeouts(now);
        while (!queue_.empty() && inflight_.size() < config_.maxInFlight)
        {
            Request r = std::move(queue_.front());
            queue_.pop_front();
            const auto obj = objects_.find(r.object);
            if (obj == objects_.end())
                continue;
            FacetState& f = obj->second.facets[r.facet];
            if (f.inflightId != r.id)
                continue;  // superseded
            f.sentAt = now;
            auto msg = r.message;
            inflight_.emplace(r.id, std::move(r));
            return msg;
        }
        return std::nullopt;
    }

    Response Reconciler::OnResponse(const std::string& id, bool ok, const std::string& message, double now)
    {
        if (GenerationOf(id) != generation_)
        {
            ++stale_;
            return Response::Stale;
        }
        const auto it = inflight_.find(id);
        if (it == inflight_.end())
            return Response::Unknown;
        const Request r = std::move(it->second);
        inflight_.erase(it);
        const auto obj = objects_.find(r.object);
        if (obj == objects_.end())
            return Response::Unknown;
        Object& o = obj->second;
        FacetState& f = o.facets[r.facet];
        if (f.inflightId != r.id)
            return Response::Unknown;
        // An answer about an entity that has been replaced since.
        if (r.facet != Facet::Spawn && r.facet != Facet::Delete && f.epoch != o.epoch)
        {
            f.inflightId.clear();
            f.inflightValue.clear();
            return Response::Unknown;
        }
        if (!ok)
        {
            Fail(o, f, message.empty() ? "the Director Mod reported an error" : message, now);
            if (r.facet == Facet::Spawn)
            {
                f.hasConfirmed = false;  // the mod removes the old entity before spawning
                for (const Facet d : kDependents)
                    o.facets[d].hasConfirmed = false;
            }
            return Response::Applied;
        }
        f.inflightId.clear();
        f.inflightValue.clear();
        f.sentAt = -1.0;
        f.confirmed = r.value;
        f.hasConfirmed = true;
        f.failedValue.clear();
        f.attempts = 0;
        f.lastError.clear();
        if (r.facet == Facet::Spawn)
        {
            ++o.epoch;
            for (const Facet d : kDependents)
            {
                FacetState& dep = o.facets[d];
                dep.hasConfirmed = false;
                dep.confirmed.clear();
            }
        }
        else if (r.facet == Facet::Delete)
            objects_.erase(obj);
        return Response::Applied;
    }

    bool Reconciler::Exists(const std::string& object) const
    {
        const auto it = objects_.find(object);
        if (it == objects_.end())
            return false;
        const auto sp = it->second.facets.find(Facet::Spawn);
        return sp != it->second.facets.end() && sp->second.hasConfirmed;
    }

    std::vector<std::string> Reconciler::ExistingObjects() const
    {
        std::vector<std::string> out;
        for (const auto& [id, o] : objects_)
            if (Exists(id) && !o.deleting)
                out.push_back(id);
        return out;
    }

    ObjectStatus Reconciler::Status(const std::string& object) const
    {
        ObjectStatus st;
        const auto it = objects_.find(object);
        if (it == objects_.end())
            return st;
        const Object& o = it->second;
        st.exists = Exists(object);
        if (o.deleting)
        {
            st.state = ObjectState::Deleting;
            return st;
        }
        // A facet that has given up on its value.
        for (const auto& [facet, f] : o.facets)
            if (f.wanted && f.attempts >= config_.maxAttempts && f.failedValue == f.desired)
            {
                st.state = ObjectState::Failed;
                st.failedFacet = facet;
                st.message = std::string(ToString(facet)) + " failed: " + f.lastError;
                return st;
            }
        const auto sp = o.facets.find(Facet::Spawn);
        if (!st.exists)
        {
            st.state = ObjectState::Spawning;
            if (sp != o.facets.end() && !sp->second.lastError.empty())
                st.message = "retrying: " + sp->second.lastError;
            return st;
        }
        if (!o.waiting.empty())
        {
            st.state = ObjectState::Waiting;
            st.message = o.waiting;
            return st;
        }
        for (const auto& [facet, f] : o.facets)
            if (f.wanted && (!f.inflightId.empty() || !Matches(f, f.desired)))
            {
                st.state = ObjectState::Updating;
                if (!f.lastError.empty())
                    st.message = std::string(ToString(facet)) + ": retrying after \"" + f.lastError + "\"";
                return st;
            }
        st.state = ObjectState::Ready;
        return st;
    }

    std::vector<Failure> Reconciler::Failures() const
    {
        std::vector<Failure> out;
        for (const auto& [id, o] : objects_)
            for (const auto& [facet, f] : o.facets)
                if (!f.lastError.empty())
                    out.push_back({id, facet, f.lastError, f.attempts, f.attempts >= config_.maxAttempts});
        return out;
    }
}  // namespace iris::director
