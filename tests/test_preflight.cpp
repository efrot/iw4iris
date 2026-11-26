#include "iris/preflight.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    bool Has(const std::vector<Issue>& issues, const char* text, Issue::Severity severity)
    {
        for (const auto& i : issues)
            if (i.severity == severity && i.message.find(text) != std::string::npos)
                return true;
        return false;
    }

    CameraKey Key(double t, Vec3 pos, double yaw)
    {
        CameraKey k;
        k.time = t;
        k.position = pos;
        k.angles = {0, yaw, 0};
        return k;
    }
}

TEST_CASE("preflight: a calm dolly shot passes")
{
    Project p;
    CameraDef c;
    c.id = "cam-1";
    c.name = "Dolly";
    c.mode = CameraMode::Dolly;
    c.path.SetKeys({Key(0, {0, 0, 0}, 0), Key(2, {200, 0, 0}, 30), Key(4, {400, 100, 0}, 60)});
    p.cameras.push_back(c);
    p.activeCameraId = "cam-1";
    const auto issues = PreflightProject(p, 0, 4, {});
    CHECK(issues.empty());
}

TEST_CASE("preflight: broken cuts, empty dollies, unknown players, whip turns")
{
    Project p;
    CameraDef empty;
    empty.id = "cam-1";
    empty.name = "Empty";
    empty.mode = CameraMode::Dolly;
    CameraDef eyes;
    eyes.id = "cam-2";
    eyes.name = "Eyes";
    eyes.mode = CameraMode::PlayerEyes;
    eyes.target.kind = TargetKind::Player;
    eyes.target.name = "Ghost";
    CameraDef whip;
    whip.id = "cam-3";
    whip.name = "Whip";
    whip.mode = CameraMode::Dolly;
    whip.path.SetKeys({Key(4, {0, 0, 0}, 0), Key(4.1, {1, 0, 0}, 170)});  // 170 degrees in 0.1 s
    p.cameras = {empty, eyes, whip};
    p.useCameraCuts = true;
    p.cuts.SetCuts({{0.0, "cam-1"}, {2.0, "cam-2"}, {4.0, "cam-3"}, {5.0, "cam-gone"}});

    const auto issues = PreflightProject(p, 0, 6, {"Alice", "Bob"});
    CHECK(Has(issues, "was deleted", Issue::Severity::Error));
    CHECK(Has(issues, "\"Empty\" is shown from 0.00 s but has no keys", Issue::Severity::Error));
    CHECK(Has(issues, "\"Ghost\", who has not appeared", Issue::Severity::Warning));
    CHECK(Has(issues, "\"Whip\" turns at", Issue::Severity::Warning));
    CHECK(HasErrors(issues));

    // Without a demo, players are not checked.
    const auto noDemo = PreflightProject(p, 0, 6, {});
    CHECK(!Has(noDemo, "has not appeared", Issue::Severity::Warning));
    // Outside the range nothing about cameras that are not shown.
    const auto early = PreflightProject(p, 0, 1, {"Alice"});
    CHECK(!Has(early, "Whip", Issue::Severity::Warning));
    CHECK(!Has(early, "Ghost", Issue::Severity::Warning));
}
