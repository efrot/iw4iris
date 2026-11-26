#include "iris/players.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    PlayerObservation Obs(int slot, const char* name, bool visible = true, bool dead = false, int team = 1)
    {
        PlayerObservation o;
        o.clientNum = slot;
        o.name = name;
        o.team = team;
        o.inRoster = true;
        o.visible = visible;
        o.dead = dead;
        o.origin = {static_cast<double>(slot) * 100.0, 0, 0};
        o.angles = {10, 90, 0};
        return o;
    }
}

TEST_CASE("players: identities survive death, respawn and leaving the view")
{
    PlayerRoster r;
    r.Update(1000, {Obs(0, "Alice"), Obs(3, "Bob")});
    CHECK(r.Players().size() == 2);
    const std::string alice = r.BySlot(0)->id;
    const std::string bob = r.BySlot(3)->id;
    CHECK(alice != bob);

    r.Update(1050, {Obs(0, "Alice", true, true), Obs(3, "Bob", false)});
    CHECK(r.Find(alice)->availability == PlayerAvailability::Dead);
    CHECK(r.Find(bob)->availability == PlayerAvailability::NotInView);
    CHECK(!r.EyesOf(*r.Find(bob)));  // no position: nothing is invented

    r.Update(1100, {Obs(0, "Alice"), Obs(3, "Bob")});
    CHECK(r.BySlot(0)->id == alice);
    CHECK(r.BySlot(3)->id == bob);
    CHECK(r.Find(alice)->availability == PlayerAvailability::Visible);
    CHECK(r.Players().size() == 2);
}

TEST_CASE("players: a reused slot is somebody else")
{
    PlayerRoster r;
    r.Update(1000, {Obs(5, "Alice")});
    const std::string alice = r.BySlot(5)->id;
    // Alice leaves; Carol takes slot 5.
    r.Update(2000, {});
    CHECK(r.Find(alice)->availability == PlayerAvailability::Left);
    r.Update(3000, {Obs(5, "Carol")});
    const auto* carol = r.BySlot(5);
    CHECK(carol);
    CHECK(carol->id != alice);
    CHECK(carol->name == "Carol");
    // A camera saved on Alice does not silently film Carol.
    CHECK(r.Resolve("Alice", 5) == nullptr);
    CHECK(r.Resolve("Carol", 5) == carol);
    // Alice comes back in another slot: the same identity again.
    r.Update(4000, {Obs(5, "Carol"), Obs(7, "Alice")});
    CHECK(r.BySlot(7)->id == alice);
    CHECK(r.Resolve("Alice", 5)->clientNum == 7);
}

TEST_CASE("players: two players of the same name are told apart by slot")
{
    PlayerRoster r;
    r.Update(1000, {Obs(1, "Player"), Obs(2, "Player")});
    CHECK(r.Players().size() == 2);
    const std::string a = r.BySlot(1)->id, b = r.BySlot(2)->id;
    CHECK(a != b);
    r.Update(1050, {Obs(1, "Player"), Obs(2, "Player")});
    CHECK(r.BySlot(1)->id == a);
    CHECK(r.BySlot(2)->id == b);
    CHECK(r.Resolve("Player", 2)->id == b);
    CHECK(r.Resolve("Player", 1)->id == a);
}

TEST_CASE("players: legacy references use the slot; reconstructed eyes use the stance")
{
    PlayerRoster r;
    auto crouched = Obs(4, "Dana");
    crouched.stance = Stance::Crouch;
    auto recorder = Obs(0, "Me");
    recorder.recording = true;
    recorder.eyeHeight = 57.5;  // the recorded view height
    r.Update(1000, {recorder, crouched});
    CHECK(r.Resolve("", 4)->name == "Dana");
    CHECK(r.Resolve("", 9) == nullptr);

    const auto dana = r.EyesOf(*r.BySlot(4));
    CHECK(dana);
    CHECK_NEAR(dana->position.z, EyeHeight(Stance::Crouch), 1e-9);
    CHECK(!dana->exact);
    CHECK_NEAR(dana->angles.roll, 0.0, 1e-9);
    const auto me = r.EyesOf(*r.BySlot(0));
    CHECK(me);
    CHECK(me->exact);
    CHECK_NEAR(me->position.z, 57.5, 1e-9);
    CHECK(EyeHeight(Stance::Stand) > EyeHeight(Stance::Crouch));
    CHECK(EyeHeight(Stance::Crouch) > EyeHeight(Stance::Prone));
}

TEST_CASE("players: stepping through connected players")
{
    PlayerRoster r;
    r.Update(1000, {Obs(6, "C"), Obs(2, "A"), Obs(4, "B")});
    const std::string a = r.BySlot(2)->id, b = r.BySlot(4)->id, c = r.BySlot(6)->id;
    CHECK(r.Step(a, 1) == b);
    CHECK(r.Step(c, 1) == a);   // wraps
    CHECK(r.Step(a, -1) == c);
    CHECK(r.Step("", 1) == a);
    r.Update(2000, {Obs(2, "A"), Obs(6, "C")});  // B left
    CHECK(r.Step(a, 1) == c);
    PlayerRoster empty;
    CHECK(empty.Step("", 1).empty());
    r.Reset();
    CHECK(r.Players().empty());
}

TEST_CASE("players: entries without a slot or any presence are ignored")
{
    PlayerRoster r;
    PlayerObservation ghost;
    ghost.clientNum = 3;
    ghost.name = "Ghost";
    r.Update(1000, {ghost, Obs(-1, "NoSlot")});
    CHECK(r.Players().empty());
}
