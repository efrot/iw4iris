// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Reconciliation between the scene the editor wants (desired state) and the
// entities the Director Mod has confirmed (engine state). Game independent
// and unit-tested with a simulated mod.
//
// Every engine object has facets: spawn (models), weapon, attachments,
// animation and, for props, link (attached to an actor's bone). For each
// facet the reconciler keeps
//
//   desired    what the scene asks for now,
//   inflight   the request sent to the mod and not answered yet,
//   confirmed  what the mod acknowledged,
//   failure    the last error, attempts and when it may be retried.
//
// Rules:
//   * A facet is only "confirmed" when the mod answered "ok" for that exact
//     value. Queuing a request changes nothing but `inflight`.
//   * Dependent facets (weapon, attachments, animation, link) are requested
//     only once the object's spawn is confirmed for the desired models; a
//     link also needs the target actor's spawn. A respawn resets them.
//   * At most one request per facet is in flight (no duplicates). A changed
//     value is sent after the current request is answered.
//   * Requests time out; failures are retried a bounded number of times
//     with a delay, then reported as failed until the desired value changes.
//   * Request ids carry the generation (one per loaded level); answers from
//     an older generation are rejected as stale.
//   * The outgoing queue and the number of requests in flight are bounded.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace iris::director
{
    enum class Facet
    {
        Spawn,
        Weapon,
        Attachments,
        Animation,
        Link,
        Delete,
    };
    const char* ToString(Facet facet);

    struct DesiredAttachment
    {
        std::string kind;  // "model" or "weapon"
        std::string name;
        std::string tag;
        int camo = 0;
        bool operator==(const DesiredAttachment&) const = default;
    };

    struct DesiredActor
    {
        std::string id;
        std::string body, head;
        std::string weapon, weaponTag = "tag_weapon_right";
        int camo = 0;
        std::vector<DesiredAttachment> attachments;
        std::string anim;        // empty: leave the current animation
        double animStart = 0.0;  // the clip's start; a change restarts the animation
    };

    struct DesiredProp
    {
        std::string id;
        std::string model;
        std::string linkActor;  // empty: free standing
        std::string linkTag;
    };

    struct Desired
    {
        std::vector<DesiredActor> actors;
        std::vector<DesiredProp> props;
    };

    struct Config
    {
        double timeout = 4.0;          // seconds without an answer
        int maxAttempts = 3;           // per value
        double retryDelay = 1.5;       // seconds before a retry
        std::size_t maxQueue = 512;    // requests waiting to be fetched
        std::size_t maxInFlight = 96;  // requests fetched and not answered
    };

    enum class ObjectState
    {
        Missing,      // not in the engine and not requested
        Spawning,     // spawn requested
        Ready,        // spawn confirmed and every facet matches
        Updating,     // spawn confirmed, a facet is being changed
        Waiting,      // waits for something else (the actor a prop links to)
        Failed,       // a facet failed after every retry
        Deleting,
    };
    const char* ToString(ObjectState state);

    struct ObjectStatus
    {
        ObjectState state = ObjectState::Missing;
        bool exists = false;        // the engine has the entity (spawn confirmed)
        std::string message;        // the failure, or what it waits for
        Facet failedFacet = Facet::Spawn;
    };

    enum class Response
    {
        Applied,   // matched an in-flight request
        Stale,     // from an older generation
        Unknown,   // no such request (answered twice, or timed out and replaced)
    };

    struct Failure
    {
        std::string object;
        Facet facet = Facet::Spawn;
        std::string message;
        int attempts = 0;
        bool final = false;        // no more retries
    };

    class Reconciler
    {
       public:
        explicit Reconciler(Config config = {});

        // A new level: the engine has none of our entities. Everything is
        // forgotten and request ids start a new generation.
        void BeginGeneration();
        int Generation() const { return generation_; }

        // Compares the desired state with the confirmed one and queues the
        // requests needed (respecting dependencies, retries and limits).
        void Reconcile(const Desired& desired, double now);
        // Makes the current clips of every actor play again from their start
        // (playback restarted, a recording begins).
        void RestartAnimations();

        // The next request for the mod: [op, request id, arguments...]. It is
        // in flight from then on.
        std::optional<std::vector<std::string>> NextRequest(double now);
        Response OnResponse(const std::string& requestId, bool ok, const std::string& message, double now);

        ObjectStatus Status(const std::string& object) const;
        bool Exists(const std::string& object) const;
        std::vector<std::string> ExistingObjects() const;
        std::size_t Queued() const { return queue_.size(); }
        std::size_t InFlight() const { return inflight_.size(); }
        std::vector<Failure> Failures() const;
        std::uint64_t RejectedStale() const { return stale_; }
        std::uint64_t TimedOut() const { return timedOut_; }
        bool Backpressure() const { return backpressure_; }

       private:
        struct FacetState
        {
            std::string desired;          // as of the last Reconcile
            bool wanted = false;          // the last Reconcile asked for this facet
            std::string confirmed;
            bool hasConfirmed = false;
            int epoch = 0;                // the object's spawn epoch the request was made for
            std::string inflightValue;
            std::string inflightId;       // queued or sent
            double sentAt = -1.0;         // < 0: queued, not fetched yet
            std::string failedValue;
            int attempts = 0;
            double retryAt = 0.0;
            std::string lastError;
        };
        struct Object
        {
            bool actor = true;
            std::map<Facet, FacetState> facets;
            bool deleting = false;
            int epoch = 0;                // increments with every confirmed spawn
            std::string waiting;          // why a dependent facet cannot be requested yet
        };
        struct Request
        {
            std::string id;
            std::string object;
            Facet facet = Facet::Spawn;
            std::string value;
            std::vector<std::string> message;  // [op, id, args...]
        };

        bool Queue(Object& o, const std::string& object, Facet facet, const std::string& value,
                   std::vector<std::string> message, double now);
        void Want(Object& o, const std::string& object, Facet facet, const std::string& value,
                  const std::vector<std::string>& message, double now);
        void ExpireTimeouts(double now);
        void Fail(Object& o, FacetState& f, const std::string& error, double now);
        static bool Matches(const FacetState& f, const std::string& value);

        Config config_;
        int generation_ = 0;
        std::uint64_t serial_ = 0;
        std::map<std::string, Object> objects_;
        std::deque<Request> queue_;
        std::map<std::string, Request> inflight_;  // by request id
        std::uint64_t stale_ = 0, timedOut_ = 0;
        bool backpressure_ = false;
        bool restartAnimations_ = false;
    };
}  // namespace iris::director
