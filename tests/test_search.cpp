#include "iris/search.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("search: fuzzy matching")
{
    CHECK(FuzzyScore("", "Anything") == 0);
    CHECK(FuzzyScore("   ", "Anything") == 0);
    CHECK(FuzzyScore("mark", "Add marker").has_value());
    CHECK(FuzzyScore("MARK", "add marker").has_value());   // case-insensitive
    CHECK(FuzzyScore("adm", "Add marker").has_value());    // subsequence
    CHECK(!FuzzyScore("xyz", "Add marker").has_value());
    CHECK(!FuzzyScore("markers", "Add marker").has_value());
    CHECK(!FuzzyScore("ma ad z", "Add marker").has_value());  // every word must match
    CHECK(FuzzyScore("marker add", "Add marker").has_value()); // words in any order
}

TEST_CASE("search: better matches score higher")
{
    // Word starts and consecutive letters beat scattered letters.
    CHECK(*FuzzyScore("in", "Set in point") > *FuzzyScore("in", "Graph editor: interpolation"));
    CHECK(*FuzzyScore("cam", "Camera from view") > *FuzzyScore("cam", "Recording: frame rate and capture method"));
    // An exact word beats a scattered subsequence.
    CHECK(*FuzzyScore("play", "Play / pause") > *FuzzyScore("play", "Preview speed: plays slower"));
    // The shorter of two otherwise equal matches wins.
    CHECK(*FuzzyScore("save", "Save") > *FuzzyScore("save", "Save project as a copy somewhere else"));
}
