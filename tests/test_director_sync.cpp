// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Director reconciliation against a SIMULATED Director Mod (MockMod below).
// These tests check the editor-side state machine; they are not a test of
// the real GSC mod in the game (see docs/TESTING.md §8 for that).
#include <map>
#include <set>

#include "iris/director_sync.hpp"
#include "test.hpp"

using namespace iris::director;

namespace
{
    struct MockMod
    {
        std::map<std::string, std::string> entities;  // object -> kind
        std::set<std::string> rejectModels;
        bool answer = true;                           // false: the mod's thread is dead
        std::vector<std::vector<std::string>> seen;
        std::map<std::string, std::string> weapon, anim, link;

        // One server frame: fetch and execute everything queued.
        void Frame(Reconciler& r, double now)
        {
            while (auto req = r.NextRequest(now))
            {
                seen.push_back(*req);
                if (!answer)
                    continue;
                const auto& q = *req;
                const std::string& op = q[0];
                const std::string& id = q[1];
                bool ok = true;
                std::string msg;
                auto need = [&](const std::string& object) {
                    if (!entities.contains(object))
                    {
                        ok = false;
                        msg = "no object " + object;
                    }
                };
                if (op == "spawnActor" || op == "spawnProp")
                {
                    entities.erase(q[2]);
                    if (rejectModels.contains(q[3]))
                    {
                        ok = false;
                        msg = "model not precached";
                    }
                    else
                        entities[q[2]] = op;
                }
                else if (op == "setWeapon")
                {
                    need(q[2]);
                    if (ok)
                        weapon[q[2]] = q[3];
                }
                else if (op == "setAttachments")
                    need(q[2]);
                else if (op == "anim")
                {
                    need(q[2]);
                    if (ok)
                        anim[q[2]] = q[3];
                }
                else if (op == "linkProp")
                {
                    need(q[2]);
                    need(q[3]);
                    if (ok)
                        link[q[2]] = q[3];
                }
                else if (op == "unlinkProp")
                {
                    need(q[2]);
                    link.erase(q[2]);
                }
                else if (op == "delete")
                    entities.erase(q[2]);
                r.OnResponse(id, ok, msg, now);
            }
        }

        int Count(const std::string& op) const
        {
            int n = 0;
            for (const auto& s : seen)
                n += s[0] == op ? 1 : 0;
            return n;
        }
    };

    DesiredActor Actor(const std::string& id, const std::string& body = "mp_body_a", const std::string& weapon = "m4_mp")
    {
        DesiredActor a;
        a.id = id;
        a.body = body;
        a.head = "head_a";
        a.weapon = weapon;
        a.anim = "pb_stand_alert";
        return a;
    }

    // Runs frames until nothing changes, advancing time.
    void Settle(Reconciler& r, MockMod& mod, const Desired& d, double& now, int frames = 20)
    {
        for (int i = 0; i < frames; ++i)
        {
            r.Reconcile(d, now);
            mod.Frame(r, now);
            now += 0.05;
        }
    }
}  // namespace

TEST_CASE("director sync: a spawn is confirmed before anything depends on it")
{
    Reconciler r;
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    r.Reconcile(d, 0.0);
    CHECK(r.Queued() == 1);  // only the spawn
    CHECK(!r.Exists("actor-1"));
    CHECK(r.Status("actor-1").state == ObjectState::Spawning);
    const auto req = r.NextRequest(0.0);
    CHECK(req.has_value() && (*req)[0] == "spawnActor");
    CHECK(!r.Exists("actor-1"));  // sent, not answered: still not spawned
    r.Reconcile(d, 0.01);
    CHECK(r.Queued() == 0);  // nothing depends on an unconfirmed spawn
    CHECK(r.OnResponse((*req)[1], true, "", 0.02) == Response::Applied);
    mod.entities["actor-1"] = "spawnActor";  // the spawn answered by hand above
    CHECK(r.Exists("actor-1"));
    double now = 0.05;
    Settle(r, mod, d, now);
    CHECK(r.Status("actor-1").state == ObjectState::Ready);
    CHECK(mod.weapon["actor-1"] == "m4_mp");
    CHECK(mod.anim["actor-1"] == "pb_stand_alert");
    CHECK(mod.Count("spawnActor") == 0);  // the first spawn was answered by hand above
    CHECK(mod.Count("setWeapon") == 1);
    CHECK(mod.Count("anim") == 1);
}

TEST_CASE("director sync: a failed spawn never lets weapons or animations through")
{
    Config c;
    c.maxAttempts = 3;
    c.retryDelay = 0.5;
    Reconciler r(c);
    r.BeginGeneration();
    MockMod mod;
    mod.rejectModels = {"mp_body_bad"};
    Desired d;
    d.actors.push_back(Actor("actor-1", "mp_body_bad"));
    double now = 0.0;
    Settle(r, mod, d, now, 200);
    CHECK(mod.Count("spawnActor") == 3);  // bounded retries
    CHECK(mod.Count("setWeapon") == 0);
    CHECK(mod.Count("anim") == 0);
    const auto st = r.Status("actor-1");
    CHECK(st.state == ObjectState::Failed);
    CHECK(!st.exists);
    CHECK(st.message.find("model not precached") != std::string::npos);
    CHECK(r.Failures().size() == 1 && r.Failures()[0].final);
    // A different character resets the attempts and works.
    d.actors[0].body = "mp_body_good";
    Settle(r, mod, d, now);
    CHECK(r.Status("actor-1").state == ObjectState::Ready);
    CHECK(mod.weapon["actor-1"] == "m4_mp");
}

TEST_CASE("director sync: unanswered requests time out and are retried, then fail")
{
    Config c;
    c.timeout = 1.0;
    c.retryDelay = 0.5;
    c.maxAttempts = 2;
    Reconciler r(c);
    r.BeginGeneration();
    MockMod mod;
    mod.answer = false;
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    double now = 0.0;
    Settle(r, mod, d, now, 120);
    CHECK(mod.Count("spawnActor") == 2);
    CHECK(r.TimedOut() == 2);
    CHECK(r.InFlight() == 0);
    const auto st = r.Status("actor-1");
    CHECK(st.state == ObjectState::Failed);
    CHECK(st.message.find("no answer") != std::string::npos);
    // The mod comes back (set reloaded): a new generation starts clean.
    mod = MockMod{};
    r.BeginGeneration();
    Settle(r, mod, d, now);
    CHECK(r.Status("actor-1").state == ObjectState::Ready);
}

TEST_CASE("director sync: answers from an older level are rejected")
{
    Reconciler r;
    r.BeginGeneration();
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    r.Reconcile(d, 0.0);
    const auto old = r.NextRequest(0.0);
    r.BeginGeneration();  // map_restart
    CHECK(r.OnResponse((*old)[1], true, "", 0.1) == Response::Stale);
    CHECK(r.RejectedStale() == 1);
    CHECK(!r.Exists("actor-1"));
    CHECK(r.OnResponse("g" + std::to_string(r.Generation()) + ".999", true, "", 0.1) == Response::Unknown);
    CHECK(r.OnResponse("garbage", true, "", 0.1) == Response::Stale);
}

TEST_CASE("director sync: no duplicate requests while one is in flight; the latest value wins")
{
    Reconciler r;
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    for (int i = 0; i < 10; ++i)
        r.Reconcile(d, 0.0);
    CHECK(r.Queued() == 1);
    double now = 0.0;
    Settle(r, mod, d, now);
    // Change the weapon twice while the first change is in flight.
    d.actors[0].weapon = "ak47_mp";
    r.Reconcile(d, now);
    const auto first = r.NextRequest(now);
    CHECK((*first)[0] == "setWeapon" && (*first)[3] == "ak47_mp");
    d.actors[0].weapon = "cheytac_mp";
    r.Reconcile(d, now);
    CHECK(r.Queued() == 0);  // waits for the answer
    r.OnResponse((*first)[1], true, "", now);
    Settle(r, mod, d, now);
    CHECK(mod.weapon["actor-1"] == "cheytac_mp");
    CHECK(r.Status("actor-1").state == ObjectState::Ready);
}

TEST_CASE("director sync: deleting, re-adding and respawning")
{
    Reconciler r;
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    double now = 0.0;
    Settle(r, mod, d, now);
    // A new character respawns the actor and re-applies its weapon.
    d.actors[0].body = "mp_body_b";
    Settle(r, mod, d, now);
    CHECK(mod.Count("spawnActor") == 2);
    CHECK(mod.Count("setWeapon") == 2);
    CHECK(r.Status("actor-1").state == ObjectState::Ready);
    // Removed from the scene: deleted in the engine, then forgotten.
    const Desired none;
    Settle(r, mod, none, now);
    CHECK(mod.Count("delete") == 1);
    CHECK(!mod.entities.contains("actor-1"));
    CHECK(r.Status("actor-1").state == ObjectState::Missing);
    // Back again: a fresh spawn.
    Settle(r, mod, d, now);
    CHECK(mod.Count("spawnActor") == 3);
    CHECK(r.Exists("actor-1"));
    // An object that never reached the engine is just dropped.
    Desired other;
    other.actors.push_back(Actor("actor-2", "mp_body_bad"));
    mod.rejectModels = {"mp_body_bad"};
    Settle(r, mod, other, now);
    Settle(r, mod, Desired{}, now);
    CHECK(mod.Count("delete") == 2);  // only actor-1 again
}

TEST_CASE("director sync: props wait for their actor and relink after a respawn")
{
    Reconciler r;
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    DesiredProp p;
    p.id = "prop-1";
    p.model = "com_plasticcase";
    p.linkActor = "actor-1";
    p.linkTag = "j_spine4";
    d.props.push_back(p);
    double now = 0.0;
    Settle(r, mod, d, now);
    CHECK(r.Exists("prop-1"));
    CHECK(r.Status("prop-1").state == ObjectState::Waiting);
    CHECK(mod.Count("linkProp") == 0);
    d.actors.push_back(Actor("actor-1"));
    Settle(r, mod, d, now);
    CHECK(mod.link["prop-1"] == "actor-1");
    CHECK(r.Status("prop-1").state == ObjectState::Ready);
    d.actors[0].body = "mp_body_c";  // respawn: the link must be made again
    Settle(r, mod, d, now);
    CHECK(mod.Count("linkProp") == 2);
    d.props[0].linkActor.clear();
    Settle(r, mod, d, now);
    CHECK(mod.Count("unlinkProp") == 1);
    CHECK(!mod.link.contains("prop-1"));
}

TEST_CASE("director sync: an answer about a replaced entity does not count")
{
    Reconciler r;
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    double now = 0.0;
    Settle(r, mod, d, now);
    d.actors[0].weapon = "ak47_mp";
    r.Reconcile(d, now);
    const auto weaponReq = r.NextRequest(now);
    CHECK((*weaponReq)[0] == "setWeapon");
    // Before the weapon answer arrives, the actor is respawned.
    d.actors[0].body = "mp_body_d";
    r.Reconcile(d, now);
    const auto spawnReq = r.NextRequest(now);
    CHECK((*spawnReq)[0] == "spawnActor");
    r.OnResponse((*spawnReq)[1], true, "", now);
    CHECK(r.OnResponse((*weaponReq)[1], true, "", now) == Response::Unknown);
    Settle(r, mod, d, now);
    CHECK(mod.weapon["actor-1"] == "ak47_mp");  // sent again for the new entity
}

TEST_CASE("director sync: the queue is bounded and drains")
{
    Config c;
    c.maxQueue = 4;
    c.maxInFlight = 2;
    Reconciler r(c);
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    for (int i = 0; i < 10; ++i)
        d.actors.push_back(Actor("actor-" + std::to_string(i)));
    r.Reconcile(d, 0.0);
    CHECK(r.Queued() == 4);
    CHECK(r.Backpressure());
    double now = 0.0;
    Settle(r, mod, d, now, 80);
    for (int i = 0; i < 10; ++i)
        CHECK(r.Status("actor-" + std::to_string(i)).state == ObjectState::Ready);
    CHECK(!r.Backpressure());
}

TEST_CASE("director sync: restarting animations re-sends the current clip")
{
    Reconciler r;
    r.BeginGeneration();
    MockMod mod;
    Desired d;
    d.actors.push_back(Actor("actor-1"));
    double now = 0.0;
    Settle(r, mod, d, now);
    CHECK(mod.Count("anim") == 1);
    Settle(r, mod, d, now);
    CHECK(mod.Count("anim") == 1);  // unchanged: not re-sent
    r.RestartAnimations();
    Settle(r, mod, d, now);
    CHECK(mod.Count("anim") == 2);
    d.actors[0].animStart = 5.0;  // the next clip of the same animation
    Settle(r, mod, d, now);
    CHECK(mod.Count("anim") == 3);
}
