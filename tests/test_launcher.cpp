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
