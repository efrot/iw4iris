#include <cstring>
#include <filesystem>
#include <fstream>

#include "iris/demo.hpp"
#include "test.hpp"

#ifdef IW4IRIS_HAVE_IW4X_HUFFMAN
#include <array>
#include <cstdint>
#include "Huffman.hpp"
#endif

using namespace iris;

TEST_CASE("demo list: when a demo was recorded, said like people say it")
{
    // 2026-11-26 20:31:00 UTC
    const std::int64_t now = 1795725060;
    CHECK(RecordedWhen(0, now, 0).empty());
    CHECK(RecordedWhen(now - 60, now, 0) == "today 20:30");
    CHECK(RecordedWhen(now - 86400, now, 0) == "yesterday 20:31");
    CHECK(RecordedWhen(now - 3 * 86400, now, 0) == "3 days ago");
    CHECK(RecordedWhen(now - 30 * 86400, now, 0) == "27 Oct 2026");
    CHECK(RecordedWhen(951782400, now, 0) == "29 Feb 2000");  // leap day
    // The viewer's midnight decides "today": 23:30 UTC is tomorrow 00:30 in UTC+1.
    const std::int64_t lateEvening = now + 3 * 3600 - 60;  // 23:30 UTC
    CHECK(RecordedWhen(now, lateEvening, 0) == "today 20:31");
    CHECK(RecordedWhen(now, lateEvening, 3600) == "yesterday 21:31");
    DemoEntry e;
    e.modifiedUnix = 100;
    CHECK(RecordedUnix(e) == 100);
    e.meta = DemoMeta{};
    e.meta->timestamp = 200;
    CHECK(RecordedUnix(e) == 200);
}

namespace
{
    void PutInt(std::vector<unsigned char>& b, std::int32_t v)
    {
        unsigned char raw[4];
        std::memcpy(raw, &v, 4);
        b.insert(b.end(), raw, raw + 4);
    }

    std::vector<unsigned char> SnapshotPayload(std::int32_t serverTime, bool withCommand)
    {
        std::vector<unsigned char> p;
        if (withCommand)
        {
            p.push_back(3);  // svc_serverCommand
            PutInt(p, 7);
            const char* cmd = "cs 1 mp_rust";
            p.insert(p.end(), cmd, cmd + std::strlen(cmd) + 1);
        }
        p.push_back(5);  // svc_snapshot
        PutInt(p, serverTime);
        for (int i = 0; i < 40; ++i)
            p.push_back(static_cast<unsigned char>(i * 7));  // delta data
        p.push_back(6);  // svc_EOF
        return p;
    }

    std::vector<unsigned char> ClientArchive()
    {
        std::vector<unsigned char> a{1};
        a.resize(1 + 56, 0);
        return a;
    }
}  // namespace

TEST_CASE("demo: parses IW4x metadata")
{
    const auto m = ParseDemoMeta(
        R"({"mapname": "mp_rust", "gametype": "dm", "author": "x", "length": 31500, "timestamp": "1700000000"})");
    CHECK(m.has_value());
    CHECK(m->mapName == "mp_rust");
    CHECK(m->lengthMs == 31500);
    CHECK(m->timestamp == 1700000000);
    CHECK(!ParseDemoMeta("garbage").has_value());
}

#ifdef IW4IRIS_HAVE_IW4X_HUFFMAN
TEST_CASE("demo: IW4x Huffman round-trips")
{
    std::vector<unsigned char> in(3000);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<unsigned char>((i * 31) ^ (i >> 3));
    std::vector<unsigned char> c(8192), d(8192);
    const int cs = Utils::Huffman::Compress(in.data(), c.data(), static_cast<int>(in.size()), static_cast<int>(c.size()));
    CHECK(cs > 0);
    const int ds = Utils::Huffman::Decompress(c.data(), d.data(), cs, static_cast<int>(d.size()));
    CHECK(ds == static_cast<int>(in.size()));
    CHECK(std::equal(in.begin(), in.end(), d.begin()));
}

TEST_CASE("demo: scans snapshot times and checkpoints")
{
    std::vector<unsigned char> file;
    const auto add = [&](const std::vector<unsigned char>& r) { file.insert(file.end(), r.begin(), r.end()); };
    add(MakeNetworkRecord(10, 8, SnapshotPayload(41000, true), Utils::Huffman::Compress));
    add(ClientArchive());
    add(MakeNetworkRecord(11, 8, SnapshotPayload(41050, false), Utils::Huffman::Compress));
    add(ClientArchive());
    add(MakeNetworkRecord(12, 8, SnapshotPayload(41100, true), Utils::Huffman::Compress));
    // end marker
    file.push_back(0);
    PutInt(file, -1);
    PutInt(file, -1);

    const auto index = ScanDemo(file, Utils::Huffman::Decompress);
    CHECK(index.reachedEnd);
    CHECK(index.warnings.empty());
    CHECK(index.networkMessages == 3);
    CHECK(index.clientArchives == 2);
    CHECK(index.snapshots.size() == 3);
    CHECK(index.FirstServerTime() == 41000);
    CHECK(index.LastServerTime() == 41100);
    CHECK_NEAR(index.DurationSeconds(), 0.1, 1e-12);
    const auto cp = index.CheckpointBefore(41099);
    CHECK(cp.has_value());
    CHECK(cp->serverTime == 41050);
    CHECK(!index.CheckpointBefore(40000).has_value());

    // Truncated files are reported, not crashed on.
    std::vector<unsigned char> truncated(file.begin(), file.begin() + 20);
    const auto bad = ScanDemo(truncated, Utils::Huffman::Decompress);
    CHECK(!bad.warnings.empty());
}
#endif

TEST_CASE("demo: unknown records stop the scan with a warning")
{
    const std::vector<unsigned char> junk{9, 1, 2, 3};
    const auto index = ScanDemo(junk, nullptr);
    CHECK(!index.reachedEnd);
    CHECK(index.warnings.size() == 1);
}

TEST_CASE("demo: finds demos with their metadata")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "iw4iris-test-demos";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "auto_1.dm_13") << "x"; }
    { std::ofstream(dir / "auto_1.dm_13.json") << R"({"mapname": "mp_highrise", "gametype": "sd", "author": "Me", "length": 1000, "timestamp": "200"})"; }
    { std::ofstream(dir / "old.dm_13") << "xy"; }
    { std::ofstream(dir / "notes.txt") << "-"; }
    const auto demos = FindDemos({dir});
    CHECK(demos.size() == 2);
    const auto it = std::find_if(demos.begin(), demos.end(), [](const DemoEntry& d) { return d.name == "auto_1"; });
    CHECK(it != demos.end());
    CHECK(it->meta.has_value());
    CHECK(DemoMatches(*it, "HIGHrise"));
    CHECK(!DemoMatches(*it, "rust"));
    CHECK(DemoMatches(*it, ""));
    fs::remove_all(dir);
}
