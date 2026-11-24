#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "iris/presets.hpp"
#include "iris/project.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("samples: every shipped sample project loads")
{
  namespace fs = std::filesystem;
  const fs::path dir = fs::path(IW4IRIS_SOURCE_DIR) / "samples";
  int count = 0;
  for (const auto& e : fs::directory_iterator(dir))
  {
    if (e.path().extension() != kProjectExtension)
      continue;
    const auto r = LoadProjectFile(e.path());
    CHECK(r.project.has_value());
    CHECK(r.warnings.empty());
    CHECK(!r.project->cameras.empty());
    CHECK(r.project->ActiveCamera() != nullptr);
    ++count;
  }
  CHECK(count >= 1);
}

TEST_CASE("samples: the preset catalogue matches the built-in presets")
{
  std::ifstream in(std::string(IW4IRIS_SOURCE_DIR) + "/presets/shot-presets.json");
  const auto j = nlohmann::json::parse(in);
  std::set<std::string> listed, builtin;
  for (const auto& p : j["presets"])
    listed.insert(p["id"].get<std::string>());
  for (const auto& p : ShotPresets())
    builtin.insert(p.id);
  CHECK(listed == builtin);
}
