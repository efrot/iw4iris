#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "InstallPlan.hpp"
#include "test.hpp"

using namespace iris::launcher;

namespace
{
  std::vector<GameEntry> SteamFolder()
  {
    return {{"iw4x.exe", false, 6'000'000, false}, {"iw4mp.exe", false, 6'000'000, false}, {"iw4x.dll", false, 12'000'000, false},
            {"main", true, 0, false},             {"zone", true, 0, false},             {"iw4x", true, 0, false},
            {"players", true, 0, false},          {"userraw", true, 0, false},          {"mods", true, 0, false},
            {"binkw32.dll", false, 300'000, false}, {"localization.txt", false, 20, false}, {"console.log", false, 500, false},
            {"crash.dmp", false, 900, false},     {"iw4iris", true, 0, false},          {"video.bik", false, 900'000'000, false}};
  }

  const PlannedEntry& Find(const std::vector<PlannedEntry>& plan, const std::string& name)
  {
    for (const auto& p : plan)
      if (p.name == name)
        return p;
    throw std::runtime_error("no entry " + name);
  }
}

TEST_CASE("launcher: validates the IW4x game folder")
{
  CHECK(ValidateGameFolder(SteamFolder()).ok);
  auto noExe = SteamFolder();
  noExe.erase(noExe.begin());
  const auto v = ValidateGameFolder(noExe);
  CHECK(!v.ok);
  CHECK(v.missing.size() == 1);
  CHECK(!ValidateGameFolder({}).ok);
  CHECK(ValidateGameFolder({{"IW4X.EXE", false, 1, false}, {"Main", true, 0, false}, {"ZONE", true, 0, false}}).ok);
}

TEST_CASE("launcher: plans links, copies and owned folders")
{
  const auto plan = PlanEnvironment(SteamFolder(), "iw4iris");
  CHECK(Find(plan, "main").action == Action::Junction);
  CHECK(Find(plan, "zone").action == Action::Junction);
  CHECK(Find(plan, "mods").action == Action::Junction);
  CHECK(Find(plan, "iw4x.exe").action == Action::Copy);
  CHECK(Find(plan, "binkw32.dll").action == Action::Copy);
  CHECK(Find(plan, "localization.txt").action == Action::Copy);
  // The environment's own folders and the user's normal client never come along.
  CHECK(Find(plan, "iw4x.dll").action == Action::Skip);
  CHECK(Find(plan, "players").action == Action::Skip);
  CHECK(Find(plan, "userraw").action == Action::Skip);
  CHECK(Find(plan, "iw4iris").action == Action::Skip);
  CHECK(Find(plan, "console.log").action == Action::Skip);
  CHECK(Find(plan, "crash.dmp").action == Action::Skip);
  CHECK(Find(plan, "video.bik").action == Action::Skip);
}

TEST_CASE("launcher: parses Steam library folders")
{
  const std::string vdf = R"("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
	}
})";
  const auto libs = ParseSteamLibraryFolders(vdf);
  CHECK(libs.size() == 2);
  CHECK(libs[0] == "C:\\Program Files (x86)\\Steam");
  CHECK(libs[1] == "D:\\SteamLibrary");
  CHECK(ParseSteamLibraryFolders("").empty());
  CHECK(SameName("Main", "mAIN"));
  CHECK(!SameName("main", "mai"));
}

TEST_CASE("launcher: Director Mod manifest accepts only safe script paths")
{
    using namespace iris::launcher;
    CHECK(IsSafeModFile("scripts/mp/iw4iris_director.gsc"));
    CHECK(IsSafeModFile("scripts\\iw4iris\\director.gsc"));
    CHECK(!IsSafeModFile("../scripts/x.gsc"));
    CHECK(!IsSafeModFile("scripts/../../x.gsc"));
    CHECK(!IsSafeModFile("/etc/x.gsc"));
    CHECK(!IsSafeModFile("C:/x.gsc"));
    CHECK(!IsSafeModFile("scripts//x.gsc"));
    CHECK(!IsSafeModFile("scripts/x.dll"));
    CHECK(!IsSafeModFile("scripts/x.gsc.exe"));
    CHECK(!IsSafeModFile(""));

    std::string error;
    const auto ok = ParseModManifest(R"({"format":"iw4iris-extension","id":"m","version":"1.0","protocol":1,
        "install":{"target":"userraw","files":["scripts/mp/a.gsc","scripts\\b\\c.gsc"]}})", &error);
    CHECK_MSG(ok.has_value(), error);
    CHECK(ok->files.size() == 2 && ok->files[1] == "scripts/b/c.gsc");
    CHECK(ok->protocol == 1);
    CHECK(!ParseModManifest(R"({"format":"iw4iris-extension","id":"m","version":"1","install":{"target":"main","files":["a.gsc"]}})"));
    CHECK(!ParseModManifest(R"({"format":"iw4iris-extension","id":"m","version":"1","install":{"target":"userraw","files":["../a.gsc"]}})"));
    CHECK(!ParseModManifest(R"({"format":"other"})"));
    CHECK(!ParseModManifest("not json", &error));
    CHECK(!error.empty());
}

TEST_CASE("launcher: the shipped Director Mod manifest lists files that exist")
{
    using namespace iris::launcher;
    const std::filesystem::path dir = std::filesystem::path(IW4IRIS_SOURCE_DIR) / "mods" / "iw4iris_director";
    std::ifstream in(dir / "mod.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    const auto m = ParseModManifest(text, &error);
    CHECK_MSG(m.has_value(), error);
    CHECK(m->id == "iw4iris_director");
    CHECK(m->protocol == 2);
    for (const auto& f : m->files)
        CHECK_MSG(std::filesystem::is_regular_file(dir / f), f);
    // The entry point must live where IW4x loads multiplayer scripts from.
    CHECK(std::find(m->files.begin(), m->files.end(), "scripts/mp/iw4iris_director.gsc") != m->files.end());
}
