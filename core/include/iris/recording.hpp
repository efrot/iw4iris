// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Recording configuration, validation, FFmpeg command lines and the exact
// per-frame clock steps of an offline render.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/timeline.hpp"

namespace iris
{
    enum class OutputFormat
    {
        PngSequence,
        JpegSequence,
        H264,
        H265,
        ProRes422,
        ProRes422HQ,
        ProRes4444,
        ProRes4444XQ,
        Ffv1Lossless,
    };
    std::string_view ToString(OutputFormat format);
    std::optional<OutputFormat> OutputFormatFromString(std::string_view text);
    std::string_view DisplayName(OutputFormat format);
    bool IsImageSequence(OutputFormat format);
    // Formats the built-in encoder writes without FFmpeg.
    bool HasBuiltinEncoder(OutputFormat format);
    std::string_view FileExtension(OutputFormat format);

    enum class RangeMode
    {
        InOut,   // the timeline's in/out points
        Full,    // the whole timeline
        Custom,  // customStart..customEnd
    };
    std::string_view ToString(RangeMode mode);
    std::optional<RangeMode> RangeModeFromString(std::string_view text);

    // Render passes. Only passes whose engine integration is implemented are
    // offered by the editor; the rest are listed so projects can name them.
    enum class PassKind
    {
        Beauty,
        WorldOnly,
        PlayersOnly,
        NoWeapons,
        WeaponsOnly,
        Greenscreen,
        Depth,
        Normals,
        ObjectId,
    };
    std::string_view ToString(PassKind kind);
    std::optional<PassKind> PassKindFromString(std::string_view text);

    struct RenderPass
    {
        PassKind kind = PassKind::Beauty;
        bool enabled = true;
        std::string backgroundColor = "#00FF00";  // greenscreen / matte
        bool operator==(const RenderPass&) const = default;
    };

    struct RecordingSettings
    {
        OutputFormat format = OutputFormat::PngSequence;
        bool useFfmpeg = false;        // image sequences: FFmpeg instead of the built-in encoder
        std::string ffmpegPath;        // empty: look next to the game / on PATH
        std::string outputDirectory;   // empty: <game>/iw4iris/recordings
        std::string namePattern = "{project}_{camera}_{pass}";
        int width = 0;                 // 0: the game's render size
        int height = 0;
        FrameRate frameRate{60, 1};
        RangeMode range = RangeMode::InOut;
        double customStart = 0.0;
        double customEnd = 5.0;
        int crf = 16;                  // H.264 / H.265 quality
        std::string preset = "slow";   // x264 / x265 preset
        bool chroma444 = false;        // H.264 / H.265 in 4:4:4
        int jpegQuality = 95;
        int queueDepth = 6;            // frames in flight to the encoder
        int warmupFrames = 4;          // frames rendered and discarded before the range
        bool hideEditorOverlays = true;
        bool includeAudio = true;      // the project's music in the video / next to a sequence
        double audioRangeStart = -1.0; // the music's span when it differs from the range (a part of a
        double audioRangeEnd = -1.0;   // per-angle sequence writes the whole range's music); < 0: the range
        std::vector<RenderPass> passes{RenderPass{}};
        bool operator==(const RecordingSettings&) const = default;
    };

    struct RenderJob
    {
        std::string id;
        std::string name;
        std::string cameraId;          // empty: the shot's cameras / cuts
        std::string angle;             // the recording it needs loaded (empty: the main demo)
        RecordingSettings settings;
        std::string status = "queued"; // queued, rendering, done, failed, cancelled
        std::string message;
        bool operator==(const RenderJob&) const = default;
    };

    struct Issue
    {
        enum class Severity
        {
            Warning,
            Error,
        } severity = Severity::Error;
        std::string message;
    };

    // Checks settings against the game's render size. Errors make a capture
    // refuse to start.
    std::vector<Issue> ValidateRecording(const RecordingSettings& settings, int gameWidth, int gameHeight,
                                         bool ffmpegAvailable);
    bool HasErrors(const std::vector<Issue>& issues);

    struct ResolvedSize
    {
        int width = 0;
        int height = 0;
    };
    ResolvedSize ResolveSize(const RecordingSettings& settings, int gameWidth, int gameHeight);

    // Cinematic time range a recording covers.
    struct TimeRange
    {
        double start = 0.0;
        double end = 0.0;
    };
    TimeRange ResolveRange(const RecordingSettings& settings, const TimelineSettings& timeline);

    // Expands the name pattern ({project}, {camera}, {pass}, {date}, {fps},
    // {w}, {h}) into a file-system-safe name (no extension, no frame number).
    std::string ExpandNamePattern(const RecordingSettings& settings, std::string_view project,
                                  std::string_view camera, std::string_view pass, std::string_view date, int width,
                                  int height);
    std::string SanitizeFileName(std::string_view name);

    // Full FFmpeg argument list (without the executable) for raw BGRA frames
    // of the given size arriving on stdin. For image sequences the output is
    // a printf-style pattern ("name_%06d.png").
    //
    // The music of a recording: a part of an audio file, placed `delay`
    // seconds after the first frame (it starts later than the range).
    struct AudioInput
    {
        std::string path;
        double fileStart = 0.0;   // seconds into the file at the first frame (after the delay)
        double delay = 0.0;       // seconds of silence before it
        double duration = 0.0;    // seconds of the file to use
        double volume = 1.0;
        bool operator==(const AudioInput&) const = default;
    };
    // With `audio`, a video format also gets the music (AAC in MP4, PCM in
    // MOV, FLAC in MKV), padded or cut to the video's length. Image
    // sequences cannot hold audio: see BuildAudioSidecarArguments.
    std::vector<std::string> BuildFfmpegArguments(const RecordingSettings& settings, int width, int height,
                                                  const std::string& outputPath, std::int64_t firstFrameNumber,
                                                  const AudioInput* audio = nullptr);
    // FFmpeg arguments that write the music of a range to a WAV file next to
    // an image sequence (`seconds`: the range's length).
    std::vector<std::string> BuildAudioSidecarArguments(const AudioInput& audio, double seconds,
                                                        const std::string& outputPath);
    // Output path for a pass: directory / base + (sequence ? "_%06d" : "") + extension.
    std::string BuildOutputPath(const RecordingSettings& settings, const std::string& directory,
                                const std::string& baseName);

    // Windows command-line quoting (the rules CommandLineToArgvW and the MSVC
    // runtime use), for CreateProcess.
    std::string QuoteWindowsArgument(std::string_view argument);
    std::string JoinWindowsCommandLine(const std::string& executable, const std::vector<std::string>& arguments);

    // Disk space estimate (bytes) for `frames` frames; approximate for
    // compressed formats.
    std::uint64_t EstimateBytesPerFrame(OutputFormat format, int width, int height, int crf);
    std::uint64_t EstimateDiskUsage(const RecordingSettings& settings, int width, int height, std::int64_t frames);
    std::string FormatBytes(std::uint64_t bytes);

    // The scene clock of an offline render. The engine advances its clock in
    // whole milliseconds per frame; to keep the output exactly timed, frame k
    // starts at scene millisecond round(sceneTime(t_k) * 1000) and the frame
    // advances by the difference to the next frame. Errors never accumulate.
    struct FrameSteps
    {
        std::vector<std::int32_t> stepMs;  // stepMs[k]: scene ms from frame k to k+1
        bool hasFreeze = false;            // some step is 0 (speed 0 or very slow)
        std::int64_t firstSceneMs = 0;
    };
    FrameSteps ComputeFrameSteps(const TimeRemap& remap, const FrameRate& rate, std::int64_t firstFrame,
                                 std::int64_t count);
}  // namespace iris
