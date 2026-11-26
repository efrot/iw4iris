// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The players of a demo (or film set), followed over time.
//
// Each frame the engine side reports what the current snapshot holds: the
// client list (slot, name, team) and the player entities that are in view
// (position, view angles, stance, alive). This module turns those
// observations into stable identities and an honest availability:
//
//   * a player keeps their identity through death, respawn and moments
//     out of view;
//   * a slot that is reused by somebody else is a different player (the
//     slot number alone never decides who is meant);
//   * a player who left and comes back under the same name is the same
//     player again;
//   * two players with the same name are told apart by their slot.
//
// Nothing here invents data: a player whose entity is not in the current
// snapshot has no position, and a camera on them has no picture rather
// than somebody else.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/math.hpp"

namespace iris
{
    enum class Stance
    {
        Stand,
        Crouch,
        Prone,
    };

    // Eye height above a player's origin for a stance (IW4 movement code's
    // view heights). Used to reconstruct the eyes of players whose exact
    // view is not recorded.
    double EyeHeight(Stance stance);

    // One player as the current snapshot shows them.
    struct PlayerObservation
    {
        int clientNum = -1;
        std::string name;
        int team = 0;              // the engine's team number (0 none, 1 axis, 2 allies, 3 spectator)
        bool inRoster = false;     // listed among the snapshot's clients
        bool visible = false;      // their entity is in the snapshot (position known)
        bool dead = false;
        Stance stance = Stance::Stand;
        Vec3 origin;               // feet
        EulerAngles angles;        // view angles
        double eyeHeight = -1.0;   // exact eye height when recorded (the recording player), else < 0
        int weapon = 0;            // weapon index
        bool recording = false;    // the player the demo was recorded by (exact first-person view)
    };

    enum class PlayerAvailability
    {
        Visible,    // in view now: position and view known
        Dead,       // in view, dead
        NotInView,  // connected, but their entity is not in the current snapshot
        Left,       // not connected now (left, or not joined yet at this time)
    };
    std::string_view ToString(PlayerAvailability availability);

    struct TrackedPlayer
    {
        std::string id;            // stable within the roster ("p1", "p2", ...)
        std::string name;
        int clientNum = -1;        // the slot now (the last known one when Left)
        int team = 0;
        bool recording = false;
        PlayerAvailability availability = PlayerAvailability::Left;
        std::optional<PlayerObservation> latest;  // this frame's observation (none when Left)
        int lastSeenTime = 0;      // server time of the last snapshot that listed them
    };

    class PlayerRoster
    {
    public:
        // A new demo or film set: forget everyone.
        void Reset();
        // The observations of one frame (every listed player, visible or
        // not). serverTime is the snapshot's time.
        void Update(int serverTime, const std::vector<PlayerObservation>& observations);

        const std::vector<TrackedPlayer>& Players() const { return players_; }
        const TrackedPlayer* Find(std::string_view id) const;
        const TrackedPlayer* BySlot(int clientNum) const;  // the player in a slot now (not Left)

        // The player a saved reference means right now. With a name, only a
        // connected player of that name qualifies (the slot decides between
        // two of the same name); without one (older projects), the slot.
        // Never returns somebody else when the named player is gone.
        const TrackedPlayer* Resolve(std::string_view name, int clientNumHint) const;

        // The eyes of a player at this frame, none when not visible.
        struct Eyes
        {
            Vec3 position;
            EulerAngles angles;
            bool exact = false;  // the recording player's recorded view
        };
        std::optional<Eyes> EyesOf(const TrackedPlayer& player) const;

        // Next / previous player (by slot) from `currentId`, skipping
        // players who are not connected; wraps around. Empty when none.
        std::string Step(std::string_view currentId, int direction) const;

    private:
        std::vector<TrackedPlayer> players_;
        int nextId_ = 1;
    };
}  // namespace iris
