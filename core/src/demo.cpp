// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/demo.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <nlohmann/json.hpp>

namespace iris
{
    namespace
    {
        constexpr int kMaxMessage = 0x20000;  // MAX_MSGLEN in IW4

        // IW4 svc ops (Game::svc_ops_e in IW4x)
        constexpr std::uint8_t svc_nop = 0;
        constexpr std::uint8_t svc_gamestate = 1;
        constexpr std::uint8_t svc_configstring = 2;
        constexpr std::uint8_t svc_serverCommand = 3;
        constexpr std::uint8_t svc_matchdata = 4;
        constexpr std::uint8_t svc_snapshot = 5;
        constexpr std::uint8_t svc_EOF = 6;

        bool ReadInt32(const std::vector<unsigned char>& b, std::size_t& pos, std::int32_t& out)
        {
            if (pos + 4 > b.size())
                return false;
            std::uint32_t v = static_cast<std::uint32_t>(b[pos]) | (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
                              (static_cast<std::uint32_t>(b[pos + 2]) << 16) | (static_cast<std::uint32_t>(b[pos + 3]) << 24);
            std::memcpy(&out, &v, 4);
            pos += 4;
            return true;
        }

        void WriteInt32(std::vector<unsigned char>& b, std::int32_t v)
        {
            std::uint32_t u;
            std::memcpy(&u, &v, 4);
            for (int i = 0; i < 4; ++i)
                b.push_back(static_cast<unsigned char>((u >> (8 * i)) & 0xFF));
        }

        std::string Lower(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }
    }  // namespace

    std::optional<DemoMeta> ParseDemoMeta(std::string_view text, std::string* error)
    {
        try
        {
            const auto j = nlohmann::json::parse(text.begin(), text.end());
            if (!j.is_object())
            {
                if (error)
                    *error = "metadata is not an object";
                return std::nullopt;
            }
            DemoMeta m;
            m.mapName = j.value("mapname", std::string());
            m.gametype = j.value("gametype", std::string());
            m.author = j.value("author", std::string());
            if (j.contains("length") && j["length"].is_number())
                m.lengthMs = j["length"].get<std::int64_t>();
            // IW4x stores the timestamp as a string of seconds.
            if (j.contains("timestamp"))
            {
                if (j["timestamp"].is_string())
                    m.timestamp = std::strtoll(j["timestamp"].get<std::string>().c_str(), nullptr, 10);
                else if (j["timestamp"].is_number())
                    m.timestamp = j["timestamp"].get<std::int64_t>();
            }
            return m;
        }
        catch (const nlohmann::json::exception& e)
        {
            if (error)
                *error = e.what();
            return std::nullopt;
        }
    }

    std::optional<std::int32_t> DemoIndex::FirstServerTime() const
    {
        if (snapshots.empty())
            return std::nullopt;
        return snapshots.front().serverTime;
    }

    std::optional<std::int32_t> DemoIndex::LastServerTime() const
    {
        if (snapshots.empty())
            return std::nullopt;
        return snapshots.back().serverTime;
    }

    double DemoIndex::DurationSeconds() const
    {
        if (snapshots.size() < 2)
            return 0.0;
        return (static_cast<double>(snapshots.back().serverTime) - snapshots.front().serverTime) / 1000.0;
    }

    std::optional<DemoSnapshot> DemoIndex::CheckpointBefore(std::int32_t serverTime) const
    {
        auto it = std::upper_bound(snapshots.begin(), snapshots.end(), serverTime,
                                   [](std::int32_t t, const DemoSnapshot& s) { return t < s.serverTime; });
        if (it == snapshots.begin())
            return std::nullopt;
        return *(it - 1);
    }

    DemoIndex ScanDemo(const std::vector<unsigned char>& bytes, const DecompressFn& decompress)
    {
        DemoIndex index;
        std::vector<unsigned char> message(kMaxMessage);
        std::size_t pos = 0;
        std::int32_t lastTime = INT32_MIN;

        while (pos < bytes.size())
        {
            const std::uint64_t recordOffset = pos;
            const std::uint8_t type = bytes[pos++];
            if (type == 0)
            {
                std::int32_t sequence = 0, length = 0;
                if (!ReadInt32(bytes, pos, sequence) || !ReadInt32(bytes, pos, length))
                {
                    index.warnings.push_back("Truncated record header at offset " + std::to_string(recordOffset));
                    return index;
                }
                if (length == -1 || sequence == -1)
                {
                    index.reachedEnd = true;
                    return index;
                }
                if (length < 4 || static_cast<std::size_t>(length) > bytes.size() - pos || length > kMaxMessage)
                {
                    index.warnings.push_back("Invalid message length " + std::to_string(length) + " at offset " +
                                             std::to_string(recordOffset));
                    return index;
                }
                ++index.networkMessages;
                const unsigned char* compressed = bytes.data() + pos + 4;  // skip reliableAcknowledge
                const int compressedSize = length - 4;
                pos += static_cast<std::size_t>(length);
                if (!decompress)
                    continue;
                const int size = decompress(compressed, message.data(), compressedSize, kMaxMessage);
                if (size <= 0)
                    continue;

                // Walk the byte-aligned ops up to the first snapshot.
                std::size_t m = 0;
                while (m < static_cast<std::size_t>(size))
                {
                    const std::uint8_t op = message[m++];
                    if (op == svc_serverCommand)
                    {
                        if (m + 4 > static_cast<std::size_t>(size))
                            break;
                        m += 4;
                        while (m < static_cast<std::size_t>(size) && message[m] != 0)
                            ++m;
                        ++m;  // terminator
                        continue;
                    }
                    if (op == svc_snapshot)
                    {
                        if (m + 4 > static_cast<std::size_t>(size))
                            break;
                        std::int32_t serverTime;
                        std::uint32_t u = static_cast<std::uint32_t>(message[m]) |
                                          (static_cast<std::uint32_t>(message[m + 1]) << 8) |
                                          (static_cast<std::uint32_t>(message[m + 2]) << 16) |
                                          (static_cast<std::uint32_t>(message[m + 3]) << 24);
                        std::memcpy(&serverTime, &u, 4);
                        if (serverTime < lastTime)
                            index.warnings.push_back("Server time went backwards at offset " + std::to_string(recordOffset));
                        lastTime = serverTime;
                        index.snapshots.push_back({serverTime, recordOffset});
                        break;
                    }
                    if (op == svc_gamestate)
                        ++index.gamestates;
                    // gamestate, configstring and matchdata carry bit-packed data
                    // whose length is not known without decoding them: the rest
                    // of the message is skipped.
                    if (op == svc_nop)
                        continue;
                    (void)svc_configstring;
                    (void)svc_matchdata;
                    (void)svc_EOF;
                    break;
                }
            }
            else if (type == 1)
            {
                // IW4x client archive: fixed 56 bytes.
                constexpr std::size_t archiveSize = 56;
                if (pos + archiveSize > bytes.size())
                {
                    index.warnings.push_back("Truncated client archive at offset " + std::to_string(recordOffset));
                    return index;
                }
                std::int32_t locationSelection = 0;
                std::size_t lp = pos + archiveSize - 4;
                ReadInt32(bytes, lp, locationSelection);
                pos += archiveSize;
                if (locationSelection != 0)
                {
                    // Demos recorded without IW4x's fix carry the selected
                    // location (2 floats + angle) after the flag.
                    if (pos + 12 > bytes.size())
                        return index;
                    pos += 12;
                }
                ++index.clientArchives;
            }
            else
            {
                index.warnings.push_back("Unknown record type " + std::to_string(type) + " at offset " +
                                         std::to_string(recordOffset) + "; the rest of the file was not read.");
                return index;
            }
        }
        index.reachedEnd = true;
        return index;
    }

    std::optional<DemoIndex> ScanDemoFile(const std::filesystem::path& file, const DecompressFn& decompress,
                                          std::string* error)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            if (error)
                *error = "cannot open " + file.string();
            return std::nullopt;
        }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return ScanDemo(bytes, decompress);
    }

    std::vector<unsigned char> MakeNetworkRecord(std::int32_t sequence, std::int32_t reliableAcknowledge,
                                                 const std::vector<unsigned char>& payload, const CompressFn& compress)
    {
        std::vector<unsigned char> compressed(kMaxMessage);
        const int size = compress(payload.data(), compressed.data(), static_cast<int>(payload.size()), kMaxMessage);
        std::vector<unsigned char> out;
        out.push_back(0);
        WriteInt32(out, sequence);
        WriteInt32(out, size + 4);
        WriteInt32(out, reliableAcknowledge);
        out.insert(out.end(), compressed.begin(), compressed.begin() + std::max(size, 0));
        return out;
    }

    std::vector<DemoEntry> FindDemos(const std::vector<std::filesystem::path>& folders, bool recursive)
    {
        namespace fs = std::filesystem;
        std::vector<DemoEntry> out;
        std::error_code ec;
        auto consider = [&](const fs::directory_entry& e) {
            if (!e.is_regular_file(ec) || e.path().extension() != kDemoExtension)
                return;
            DemoEntry d;
            d.path = e.path();
            d.name = e.path().stem().string();
            d.size = e.file_size(ec);
            const auto ft = e.last_write_time(ec);
            if (!ec)
            {
                const auto sys = std::chrono::time_point_cast<std::chrono::seconds>(
                    ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                d.modifiedUnix = sys.time_since_epoch().count();
            }
            fs::path metaPath = e.path();
            metaPath += ".json";
            if (std::ifstream meta(metaPath, std::ios::binary); meta)
            {
                std::string text((std::istreambuf_iterator<char>(meta)), std::istreambuf_iterator<char>());
                d.meta = ParseDemoMeta(text);
            }
            out.push_back(std::move(d));
        };
        for (const auto& folder : folders)
        {
            if (!fs::is_directory(folder, ec))
                continue;
            if (recursive)
            {
                for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                     it != end; it.increment(ec))
                    consider(*it);
            }
            else
            {
                for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                     it != end; it.increment(ec))
                    consider(*it);
            }
        }
        std::sort(out.begin(), out.end(), [](const DemoEntry& a, const DemoEntry& b) {
            const auto ta = a.meta && a.meta->timestamp ? a.meta->timestamp : a.modifiedUnix;
            const auto tb = b.meta && b.meta->timestamp ? b.meta->timestamp : b.modifiedUnix;
            return ta > tb;
        });
        return out;
    }

    bool DemoMatches(const DemoEntry& entry, std::string_view query)
    {
        if (query.empty())
            return true;
        const std::string q = Lower(query);
        auto has = [&](std::string_view s) { return Lower(s).find(q) != std::string::npos; };
        if (has(entry.name))
            return true;
        if (entry.meta)
            return has(entry.meta->mapName) || has(entry.meta->gametype) || has(entry.meta->author);
        return false;
    }

    std::int64_t RecordedUnix(const DemoEntry& entry)
    {
        if (entry.meta && entry.meta->timestamp > 0)
            return entry.meta->timestamp;
        return entry.modifiedUnix > 0 ? entry.modifiedUnix : 0;
    }

    std::string RecordedWhen(std::int64_t recordedUnix, std::int64_t nowUnix, std::int32_t utcOffsetSeconds)
    {
        if (recordedUnix <= 0)
            return {};
        constexpr std::int64_t kDay = 86400;
        // Local days since the epoch (floor division, also before 1970).
        auto localDay = [&](std::int64_t t) {
            const std::int64_t local = t + utcOffsetSeconds;
            return local >= 0 ? local / kDay : -((-local + kDay - 1) / kDay);
        };
        const std::int64_t days = localDay(nowUnix) - localDay(recordedUnix);
        const std::int64_t secondOfDay = recordedUnix + utcOffsetSeconds - localDay(recordedUnix) * kDay;
        char clock[8];
        std::snprintf(clock, sizeof(clock), "%02d:%02d", static_cast<int>(secondOfDay / 3600),
                      static_cast<int>(secondOfDay / 60 % 60));
        if (days == 0)
            return std::string("today ") + clock;
        if (days == 1)
            return std::string("yesterday ") + clock;
        if (days > 1 && days < 7)
            return std::to_string(days) + " days ago";
        // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
        const std::int64_t z = localDay(recordedUnix) + 719468;
        const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const std::int64_t doe = z - era * 146097;
        const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const std::int64_t mp = (5 * doy + 2) / 153;
        const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
        const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
        const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
        static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        return std::to_string(day) + " " + names[month - 1] + " " + std::to_string(year);
    }
}  // namespace iris
