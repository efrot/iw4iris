#include "iris/diagnostics.hpp"
#include "test.hpp"

#include <nlohmann/json.hpp>

using namespace iris;

TEST_CASE("diagnostics: overall state and counts")
{
    DiagnosticsReport r;
    CHECK(r.Overall() == CapabilityState::Ready);
    r.capabilities.push_back({"a", "A", "Engine", CapabilityState::Ready, "", ""});
    r.capabilities.push_back({"b", "B", "Engine", CapabilityState::NotAvailable, "no film set", ""});
    CHECK(r.Overall() == CapabilityState::Ready);  // not available is not an error
    r.capabilities.push_back({"c", "C", "Graphics", CapabilityState::Limited, "approximate matrices", ""});
    CHECK(r.Overall() == CapabilityState::Limited);
    r.capabilities.push_back({"d", "D", "Graphics", CapabilityState::Checking, "", ""});
    CHECK(r.Overall() == CapabilityState::Checking);
    r.capabilities.push_back({"e", "E", "Capture", CapabilityState::Failed, "clock", "reinstall"});
    CHECK(r.Overall() == CapabilityState::Failed);
    CHECK(r.Count(CapabilityState::Ready) == 1);
    CHECK(r.Count(CapabilityState::Failed) == 1);

    const auto text = ReportText(r);
    CHECK(text.find("Overall: Failed") != std::string::npos);
    CHECK(text.find("[Graphics]") != std::string::npos);
    CHECK(text.find("fix: reinstall") != std::string::npos);
    const auto j = ReportJson(r);
    CHECK(j["format"] == "iw4iris-diagnostics");
    CHECK(j["capabilities"].size() == 5);
    CHECK(j["capabilities"][1]["state"] == "Not available");
    CHECK(j["overall"] == "Failed");
}

TEST_CASE("diagnostics: support bundles carry no home folder or account name")
{
    Redaction red;
    red.homes = {"C:\\Users\\Roman", "/home/roman/"};
    red.userNames = {"Roman"};
    // Either slash, any case.
    CHECK(Redact("C:\\Users\\Roman\\Documents\\x.iw4iris", red) == "<home>\\Documents\\x.iw4iris");
    CHECK(Redact("c:/users/roman/AppData/Local/IW4Iris", red) == "<home>/AppData/Local/IW4Iris");
    CHECK(Redact("/home/roman/demos/a.dm_13", red) == "<home>/demos/a.dm_13");
    // The account name alone, as a whole word only.
    CHECK(Redact("player Roman joined", red) == "player <user> joined");
    CHECK(Redact("Romania", red) == "Romania");
    // Nothing else changes.
    CHECK(Redact("mp_highrise loaded", red) == "mp_highrise loaded");
    // Too-short entries are ignored rather than shredding the text.
    Redaction tiny;
    tiny.homes = {"C:"};
    tiny.userNames = {"a"};
    CHECK(Redact("C:/a b", tiny) == "C:/a b");
}
