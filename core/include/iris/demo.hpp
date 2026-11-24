// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// IW4x demos (.dm_13) and their metadata (.dm_13.json).
//
// File layout, as IW4x writes it (Components::Theatre and the engine's
// CL_Record / CL_WriteDemoMessage / CL_WriteDemoClientArchive):
//
//   repeated records:
//     uint8  type         0 = network message, 1 = client archive
//     type 0:
//       int32 serverMessageSequence
//       int32 length      bytes that follow (-1: end of demo)
//       int32 reliableAcknowledge  (uncompressed, counted in length)
//       ...   length-4 bytes of Huffman-compressed message
//     type 1 (IW4x layout, 56 bytes):
//       int32 index, float origin[3], float velocity[3], int32 movementDir,
//       int32 bobCycle, uint32 (pointer, ignored), float viewangles[3],
//       int32 locationSelectionInfo (IW4x always writes 0)
//
// The decompressed message is a sequence of svc ops; a snapshot starts with
// svc_snapshot (5) followed by its int32 serverTime, so the time of every
// snapshot can be read without decoding the delta-compressed entities.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iris
{
    inline constexpr std::string_view kDemoExtension = ".dm_13";

    struct DemoMeta
    {
        std::string mapName;
        std::string gametype;
        std::string author;
        std::int64_t lengthMs = 0;     // recording length as IW4x measured it (wall clock)
        std::int64_t timestamp = 0;    // Unix time of the recording
    };

    // Parses the IW4x metadata sidecar ("<demo>.dm_13.json").
    std::optional<DemoMeta> ParseDemoMeta(std::string_view json, std::string* error = nullptr);

    struct DemoSnapshot
    {
        std::int32_t serverTime = 0;
        std::uint64_t fileOffset = 0;  // offset of the record that carries it
    };

    struct DemoIndex
    {
        std::vector<DemoSnapshot> snapshots;
        std::uint32_t networkMessages = 0;
        std::uint32_t clientArchives = 0;
        std::uint32_t gamestates = 0;
        bool reachedEnd = false;        // a clean end marker or end of file
        std::vector<std::string> warnings;

        std::optional<std::int32_t> FirstServerTime() const;
        std::optional<std::int32_t> LastServerTime() const;
        // Scene duration in seconds (last - first snapshot server time).
        double DurationSeconds() const;
        // Record offset of the last snapshot at or before the server time: a
        // checkpoint to re-read the demo from.
        std::optional<DemoSnapshot> CheckpointBefore(std::int32_t serverTime) const;
    };

    // Huffman decompression of a demo message (IW4x's Utils::Huffman::Decompress
    // in the game; returns the decompressed size, or <= 0 on failure).
    using DecompressFn = std::function<int(const unsigned char* in, unsigned char* out, int inSize, int outSize)>;

    DemoIndex ScanDemo(const std::vector<unsigned char>& bytes, const DecompressFn& decompress);
    std::optional<DemoIndex> ScanDemoFile(const std::filesystem::path& file, const DecompressFn& decompress,
                                          std::string* error = nullptr);

    // Builds a demo record (for tests and tools): a network message holding
    // the given uncompressed payload, compressed with `compress`.
    using CompressFn = std::function<int(const unsigned char* in, unsigned char* out, int inSize, int outSize)>;
    std::vector<unsigned char> MakeNetworkRecord(std::int32_t sequence, std::int32_t reliableAcknowledge,
                                                 const std::vector<unsigned char>& payload, const CompressFn& compress);

    struct DemoEntry
    {
        std::filesystem::path path;
        std::string name;               // file name without the extension
        std::uint64_t size = 0;
        std::int64_t modifiedUnix = 0;
        std::optional<DemoMeta> meta;
    };

    // Lists demos in the folders (non-recursive unless asked), newest first.
    std::vector<DemoEntry> FindDemos(const std::vector<std::filesystem::path>& folders, bool recursive = false);

    // Case-insensitive substring filter over name / map / gametype / author.
    bool DemoMatches(const DemoEntry& entry, std::string_view query);
}  // namespace iris
