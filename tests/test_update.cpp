#include <string>

#include "UpdatePlan.hpp"
#include "test.hpp"

using namespace iris::launcher;

namespace
{
  // A trimmed answer of GitHub's REST API (GET /repos/{owner}/{repo}/releases/latest).
  std::string Release(const std::string& tag, const std::string& packageName, const std::string& packageUrl,
                      bool prerelease = false)
  {
    return R"({"tag_name":")" + tag + R"(","draft":false,"prerelease":)" + (prerelease ? "true" : "false") +
           R"(,"body":"Film sets load.","assets":[{"name":")" + packageName + R"(","browser_download_url":")" +
           packageUrl + R"(","size":4616457},{"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/efrot/iw4iris/releases/download/)" +
           tag + R"(/SHA256SUMS.txt","size":90}]})";
  }
}

TEST_CASE("update: versions are compared numerically, tags and product strings parse")
{
  CHECK(ParseVersion("v0.6.0").has_value());
  CHECK(ToString(*ParseVersion("v0.10.2")) == "0.10.2");
  CHECK(ToString(*ParseVersion("IW4Iris 0.5.1")) == "0.5.1");
  CHECK(!ParseVersion("0.6").has_value());
  CHECK(!ParseVersion("v0.6.0-rc1").has_value());
  CHECK(!ParseVersion("latest").has_value());
  CHECK(!ParseVersion("").has_value());
  CHECK(IsNewer(*ParseVersion("0.10.0"), *ParseVersion("0.9.9")));  // not a string comparison
  CHECK(IsNewer(*ParseVersion("1.0.0"), *ParseVersion("0.99.99")));
  CHECK(!IsNewer(*ParseVersion("0.5.1"), *ParseVersion("0.5.1")));
  CHECK(!IsNewer(*ParseVersion("0.5.0"), *ParseVersion("0.5.1")));
  CHECK(PackageAssetName(*ParseVersion("0.6.0")) == "IW4Iris-v0.6.0-Windows-x86.zip");
}

TEST_CASE("update: a release is accepted only with its own package and checksums from this repository")
{
  const std::string url = "https://github.com/efrot/iw4iris/releases/download/v0.6.0/IW4Iris-v0.6.0-Windows-x86.zip";
  std::string error;
  const auto r = ParseRelease(Release("v0.6.0", "IW4Iris-v0.6.0-Windows-x86.zip", url), &error);
  CHECK_MSG(r.has_value(), error);
  CHECK(ToString(r->version) == "0.6.0");
  CHECK(r->package.url == url);
  CHECK(r->package.size == 4616457u);
  CHECK(r->checksums.name == "SHA256SUMS.txt");
  CHECK(r->pageUrl == "https://github.com/efrot/iw4iris/releases/tag/v0.6.0");
  CHECK(r->notes == "Film sets load.");

  // The package of another version (a mislabelled release) is not used.
  CHECK(!ParseRelease(Release("v0.6.0", "IW4Iris-v0.5.9-Windows-x86.zip", url)).has_value());
  // Pre-releases and broken answers are refused.
  CHECK(!ParseRelease(Release("v0.6.0", "IW4Iris-v0.6.0-Windows-x86.zip", url, true)).has_value());
  CHECK(!ParseRelease("<html>rate limited</html>", &error).has_value());
  CHECK(!error.empty());
  CHECK(!ParseRelease(R"({"tag_name":"nightly","assets":[]})").has_value());
  // A download pointing somewhere else is refused.
  CHECK(!ParseRelease(Release("v0.6.0", "IW4Iris-v0.6.0-Windows-x86.zip",
                              "https://evil.example/IW4Iris-v0.6.0-Windows-x86.zip"))
             .has_value());
}

TEST_CASE("update: only https downloads from this repository's releases are trusted")
{
  CHECK(IsTrustedUpdateUrl("https://github.com/efrot/iw4iris/releases/download/v0.6.0/x.zip"));
  CHECK(IsTrustedUpdateUrl("https://api.github.com/repos/efrot/iw4iris/releases/latest"));
  CHECK(IsTrustedUpdateUrl("https://objects.githubusercontent.com/github-production-release-asset/123?sig=abc"));
  CHECK(IsTrustedUpdateUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1"));
  CHECK(!IsTrustedUpdateUrl("http://github.com/efrot/iw4iris/releases/download/v0.6.0/x.zip"));  // not https
  CHECK(!IsTrustedUpdateUrl("https://github.com/someone/else/releases/download/v1/x.zip"));     // other repo
  CHECK(!IsTrustedUpdateUrl("https://github.com/efrot/iw4iris/archive/refs/heads/main.zip"));   // not a release
  CHECK(!IsTrustedUpdateUrl("https://github.com.evil.example/efrot/iw4iris/releases/download/x"));
  CHECK(!IsTrustedUpdateUrl("https://githubusercontent.com/x"));                    // the bare domain
  CHECK(!IsTrustedUpdateUrl("https://evilgithubusercontent.com/x"));
  CHECK(!IsTrustedUpdateUrl("https://user@github.com/efrot/iw4iris/releases/download/x"));
  CHECK(!IsTrustedUpdateUrl("https://github.com:8443/efrot/iw4iris/releases/download/x"));
  CHECK(!IsTrustedUpdateUrl("https://github.com"));
}

TEST_CASE("update: checksums are read from sha256sum files (text and binary mode, CRLF)")
{
  const std::string a(64, 'a');
  const std::string b = "ADA38DB0B703A56D3DD6D57BF84A9C5D664921D870D8FEA4DB153979FB5332C5";
  const std::string sums = a + "  IW4Iris-v0.6.0-Windows-x86.zip\r\n" + b + " *other.zip\n" + "zz  broken.zip\n";
  CHECK(ChecksumFor(sums, "IW4Iris-v0.6.0-Windows-x86.zip") == a);
  CHECK(ChecksumFor(sums, "other.zip") == "ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5");
  CHECK(!ChecksumFor(sums, "broken.zip").has_value());
  CHECK(!ChecksumFor(sums, "IW4Iris-v0.6.0-Windows-x86.zi").has_value());
  CHECK(!ChecksumFor("", "x").has_value());
}

TEST_CASE("update: archive entries can never be written outside the package")
{
  CHECK(SafeArchivePath("client/iw4x.dll") == std::string("client/iw4x.dll"));
  CHECK(SafeArchivePath("client\\iw4x.dll") == std::string("client/iw4x.dll"));  // Compress-Archive on PS 5.1
  CHECK(SafeArchivePath("IW4Iris Launcher.exe") == std::string("IW4Iris Launcher.exe"));
  CHECK(SafeArchivePath("docs/") == std::string("docs/"));
  for (const char* bad : {"../evil.dll", "client/../../x", "/etc/passwd", "\\windows\\x.dll", "C:/x.dll", "C:x.dll",
                          "client//x", "./x", "client/.", "file.txt:stream", "nul", "docs/CON.txt", "com1",
                          "lpt9.log", "x.", "x ", "a\tb", "a*b", ".iw4iris-update/backup/x", ".git/config", ""})
    CHECK_MSG(!SafeArchivePath(bad).has_value(), bad);
  CHECK(SafeArchivePath("docs/.git-notes.md").has_value());  // only the top-level .git is ours
}

TEST_CASE("update: a package must contain the launcher, the client and the Director Mod")
{
  CHECK(MissingPackageFiles({"IW4Iris Launcher.exe", "client/iw4x.dll", "mods/iw4iris_director/mod.json", "README.md"})
            .empty());
  const auto missing = MissingPackageFiles({"README.md", "client/IW4X.DLL"});
  CHECK(missing.size() == 2);
  CHECK(missing[0] == "IW4Iris Launcher.exe");
}
