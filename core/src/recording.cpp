// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/recording.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace iris
{
    std::string_view ToString(OutputFormat format)
    {
        switch (format)
        {
            case OutputFormat::PngSequence: return "png";
            case OutputFormat::JpegSequence: return "jpeg";
            case OutputFormat::H264: return "h264";
            case OutputFormat::H265: return "h265";
            case OutputFormat::ProRes422: return "prores422";
            case OutputFormat::ProRes422HQ: return "prores422hq";
            case OutputFormat::ProRes4444: return "prores4444";
            case OutputFormat::ProRes4444XQ: return "prores4444xq";
            case OutputFormat::Ffv1Lossless: return "ffv1";
        }
        return "png";
    }

    std::optional<OutputFormat> OutputFormatFromString(std::string_view text)
    {
        for (auto f : {OutputFormat::PngSequence, OutputFormat::JpegSequence, OutputFormat::H264, OutputFormat::H265,
                       OutputFormat::ProRes422, OutputFormat::ProRes422HQ, OutputFormat::ProRes4444,
                       OutputFormat::ProRes4444XQ, OutputFormat::Ffv1Lossless})
            if (ToString(f) == text)
                return f;
        return std::nullopt;
    }

    std::string_view DisplayName(OutputFormat format)
    {
        switch (format)
        {
            case OutputFormat::PngSequence: return "PNG sequence";
            case OutputFormat::JpegSequence: return "JPEG sequence";
            case OutputFormat::H264: return "H.264 (MP4)";
            case OutputFormat::H265: return "H.265 / HEVC (MP4)";
            case OutputFormat::ProRes422: return "ProRes 422 (MOV)";
            case OutputFormat::ProRes422HQ: return "ProRes 422 HQ (MOV)";
            case OutputFormat::ProRes4444: return "ProRes 4444 (MOV)";
            case OutputFormat::ProRes4444XQ: return "ProRes 4444 XQ (MOV)";
            case OutputFormat::Ffv1Lossless: return "FFV1 lossless (MKV)";
        }
        return "PNG sequence";
    }

    bool IsImageSequence(OutputFormat format)
    {
        return format == OutputFormat::PngSequence || format == OutputFormat::JpegSequence;
    }

    bool HasBuiltinEncoder(OutputFormat format)
    {
        return IsImageSequence(format);
    }

    std::string_view FileExtension(OutputFormat format)
    {
        switch (format)
        {
            case OutputFormat::PngSequence: return ".png";
            case OutputFormat::JpegSequence: return ".jpg";
            case OutputFormat::H264:
            case OutputFormat::H265: return ".mp4";
            case OutputFormat::ProRes422:
            case OutputFormat::ProRes422HQ:
            case OutputFormat::ProRes4444:
            case OutputFormat::ProRes4444XQ: return ".mov";
            case OutputFormat::Ffv1Lossless: return ".mkv";
        }
        return ".png";
    }

    std::string_view ToString(RangeMode mode)
    {
        switch (mode)
        {
            case RangeMode::InOut: return "inOut";
            case RangeMode::Full: return "full";
            case RangeMode::Custom: return "custom";
        }
        return "inOut";
    }

    std::optional<RangeMode> RangeModeFromString(std::string_view text)
    {
        if (text == "inOut") return RangeMode::InOut;
        if (text == "full") return RangeMode::Full;
        if (text == "custom") return RangeMode::Custom;
        return std::nullopt;
    }

    std::string_view ToString(PassKind kind)
    {
        switch (kind)
        {
            case PassKind::Beauty: return "beauty";
            case PassKind::WorldOnly: return "worldOnly";
            case PassKind::PlayersOnly: return "playersOnly";
            case PassKind::NoWeapons: return "noWeapons";
            case PassKind::WeaponsOnly: return "weaponsOnly";
            case PassKind::Greenscreen: return "greenscreen";
            case PassKind::Depth: return "depth";
            case PassKind::Normals: return "normals";
            case PassKind::ObjectId: return "objectId";
        }
        return "beauty";
    }

    std::optional<PassKind> PassKindFromString(std::string_view text)
    {
        for (auto k : {PassKind::Beauty, PassKind::WorldOnly, PassKind::PlayersOnly, PassKind::NoWeapons,
                       PassKind::WeaponsOnly, PassKind::Greenscreen, PassKind::Depth, PassKind::Normals,
                       PassKind::ObjectId})
            if (ToString(k) == text)
                return k;
        return std::nullopt;
    }

    ResolvedSize ResolveSize(const RecordingSettings& settings, int gameWidth, int gameHeight)
    {
        ResolvedSize s;
        s.width = settings.width > 0 ? settings.width : gameWidth;
        s.height = settings.height > 0 ? settings.height : gameHeight;
        return s;
    }

    bool HasErrors(const std::vector<Issue>& issues)
    {
        return std::any_of(issues.begin(), issues.end(),
                           [](const Issue& i) { return i.severity == Issue::Severity::Error; });
    }

    std::vector<Issue> ValidateRecording(const RecordingSettings& s, int gameWidth, int gameHeight,
                                         bool ffmpegAvailable)
    {
        std::vector<Issue> issues;
        auto error = [&](std::string m) { issues.push_back({Issue::Severity::Error, std::move(m)}); };
        auto warning = [&](std::string m) { issues.push_back({Issue::Severity::Warning, std::move(m)}); };

        if (!s.frameRate.Valid() || s.frameRate.Fps() < 1.0 || s.frameRate.Fps() > 1000.0)
            error("Frame rate must be between 1 and 1000 FPS.");

        const auto size = ResolveSize(s, gameWidth, gameHeight);
        if (size.width <= 0 || size.height <= 0)
            error("The render size is unknown; start the game renderer first.");
        if (size.width > 16384 || size.height > 16384)
            error("The render size exceeds 16384 pixels.");

        // Rendering at a size other than the game's is not implemented yet:
        // frames are captured from the game's back buffer.
        if ((s.width > 0 && s.width != gameWidth) || (s.height > 0 && s.height != gameHeight))
            error("Custom render sizes are not available yet: set the game resolution (r_mode) to the "
                  "size you want and leave the recording size on 'Game resolution'.");

        const bool needsFfmpeg = !HasBuiltinEncoder(s.format) || s.useFfmpeg;
        if (needsFfmpeg && !ffmpegAvailable)
            error(std::string(DisplayName(s.format)) + " needs FFmpeg, which was not found.");

        const bool yuv420 = (s.format == OutputFormat::H264 || s.format == OutputFormat::H265) && !s.chroma444;
        const bool yuv422 = s.format == OutputFormat::ProRes422 || s.format == OutputFormat::ProRes422HQ;
        if ((yuv420 || yuv422) && (size.width % 2 != 0))
            error("Width must be even for 4:2:0 / 4:2:2 chroma subsampling.");
        if (yuv420 && size.height % 2 != 0)
            error("Height must be even for 4:2:0 chroma subsampling.");

        if ((s.format == OutputFormat::H264 || s.format == OutputFormat::H265) && (s.crf < 0 || s.crf > 51))
            error("CRF must be between 0 and 51.");
        if (s.format == OutputFormat::JpegSequence && (s.jpegQuality < 1 || s.jpegQuality > 100))
            error("JPEG quality must be between 1 and 100.");
        if (s.queueDepth < 1 || s.queueDepth > 64)
            error("Queue depth must be between 1 and 64 frames.");

        if (s.range == RangeMode::Custom && s.customEnd <= s.customStart)
            error("The custom range must end after it starts.");

        const auto enabledPasses = std::count_if(s.passes.begin(), s.passes.end(),
                                                 [](const RenderPass& p) { return p.enabled; });
        if (enabledPasses == 0)
            error("Enable at least one render pass.");
        for (const auto& p : s.passes)
            if (p.enabled && p.kind != PassKind::Beauty)
                error("Render pass '" + std::string(ToString(p.kind)) +
                      "' is not implemented for IW4x yet; only the beauty pass is available.");

        if (size.width * static_cast<std::int64_t>(size.height) * 4 * s.queueDepth > 768ll * 1024 * 1024)
            warning("The frame queue would use more than 768 MB of the game's 32-bit address space; "
                    "lower the queue depth.");
        if (s.frameRate.Fps() > 240.0)
            warning("Frame rates above 240 FPS step the scene clock by less than 5 ms per frame.");
        return issues;
    }

    TimeRange ResolveRange(const RecordingSettings& settings, const TimelineSettings& timeline)
    {
        switch (settings.range)
        {
            case RangeMode::Full: return {timeline.start, timeline.end};
            case RangeMode::Custom: return {settings.customStart, settings.customEnd};
            case RangeMode::InOut:
            default: return {timeline.inPoint, timeline.outPoint};
        }
    }

    std::string SanitizeFileName(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        for (const char c : name)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
                c == '?' || c == '*')
                out.push_back('_');
            else
                out.push_back(c);
        }
        while (!out.empty() && (out.back() == '.' || out.back() == ' '))
            out.pop_back();
        if (out.empty())
            out = "render";
        // Reserved device names on Windows.
        static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                         "LPT1", "LPT2", "LPT3"};
        for (const char* r : reserved)
        {
            std::string upper = out;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            if (upper == r)
                out.insert(out.begin(), '_');
        }
        return out;
    }

    std::string ExpandNamePattern(const RecordingSettings& settings, std::string_view project,
                                  std::string_view camera, std::string_view pass, std::string_view date, int width,
                                  int height)
    {
        std::string out(settings.namePattern.empty() ? "{project}_{pass}" : settings.namePattern);
        auto replace = [&](std::string_view key, std::string_view value) {
            std::size_t pos = 0;
            while ((pos = out.find(key, pos)) != std::string::npos)
            {
                out.replace(pos, key.size(), value);
                pos += value.size();
            }
        };
        replace("{project}", project);
        replace("{camera}", camera);
        replace("{pass}", pass);
        replace("{date}", date);
        replace("{fps}", settings.frameRate.ToString());
        replace("{w}", std::to_string(width));
        replace("{h}", std::to_string(height));
        return SanitizeFileName(out);
    }

    std::string BuildOutputPath(const RecordingSettings& settings, const std::string& directory,
                                const std::string& baseName)
    {
        std::string path = directory;
        if (!path.empty() && path.back() != '/' && path.back() != '\\')
            path.push_back('/');
        path += baseName;
        if (IsImageSequence(settings.format))
            path += "_%06d";
        path += FileExtension(settings.format);
        return path;
    }

    namespace
    {
        std::string Seconds(double v)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.6f", std::max(0.0, v));
            return text;
        }

        // The audio input (-ss / -t / -i) and its filter (delay, volume).
        void AddAudioInput(std::vector<std::string>& a, const AudioInput& in)
        {
            if (in.fileStart > 0.0)
                a.insert(a.end(), {"-ss", Seconds(in.fileStart)});
            a.insert(a.end(), {"-t", Seconds(in.duration), "-i", in.path});
        }

        std::string AudioFilter(const AudioInput& in, double seconds, bool pad)
        {
            std::string f;
            if (in.delay > 0.0)
                f += "adelay=" + std::to_string(std::llround(in.delay * 1000.0)) + ":all=1,";
            char volume[32];
            std::snprintf(volume, sizeof(volume), "volume=%.4f", std::clamp(in.volume, 0.0, 4.0));
            f += volume;
            if (pad)
                f += seconds > 0.0 ? ",apad=whole_dur=" + Seconds(seconds) : std::string(",apad");
            return f;
        }
    }  // namespace

    std::vector<std::string> BuildAudioSidecarArguments(const AudioInput& audio, double seconds,
                                                        const std::string& outputPath)
    {
        std::vector<std::string> a{"-hide_banner", "-loglevel", "warning", "-nostats", "-y"};
        AddAudioInput(a, audio);
        a.insert(a.end(), {"-af", AudioFilter(audio, seconds, true), "-t", Seconds(seconds), "-c:a", "pcm_s16le",
                           outputPath});
        return a;
    }

    std::vector<std::string> BuildFfmpegArguments(const RecordingSettings& s, int width, int height,
                                                  const std::string& outputPath, std::int64_t firstFrameNumber,
                                                  const AudioInput* audio)
    {
        const std::string rate = std::to_string(s.frameRate.numerator) + "/" + std::to_string(s.frameRate.denominator);
        std::vector<std::string> a{"-hide_banner", "-loglevel", "warning", "-nostats", "-y",
                                   "-f",           "rawvideo", "-pix_fmt", "bgra",     "-video_size",
                                   std::to_string(width) + "x" + std::to_string(height),
                                   "-framerate",   rate,       "-i",       "pipe:0"};
        const std::size_t inputsEnd = a.size();  // a second input (the music) goes here
        auto add = [&](std::initializer_list<std::string> items) { a.insert(a.end(), items); };
        switch (s.format)
        {
            case OutputFormat::PngSequence:
                add({"-c:v", "png", "-pix_fmt", "rgb24", "-start_number", std::to_string(firstFrameNumber)});
                break;
            case OutputFormat::JpegSequence:
            {
                // mjpeg -q:v 2 (best) .. 31; map 1..100 quality onto it.
                const int q = std::clamp(2 + (100 - std::clamp(s.jpegQuality, 1, 100)) * 29 / 99, 2, 31);
                add({"-c:v", "mjpeg", "-q:v", std::to_string(q), "-pix_fmt", "yuvj444p", "-start_number",
                     std::to_string(firstFrameNumber)});
                break;
            }
            case OutputFormat::H264:
                add({"-c:v", "libx264", "-preset", s.preset.empty() ? "slow" : s.preset, "-crf", std::to_string(s.crf),
                     "-pix_fmt", s.chroma444 ? "yuv444p" : "yuv420p", "-movflags", "+faststart"});
                break;
            case OutputFormat::H265:
                add({"-c:v", "libx265", "-preset", s.preset.empty() ? "medium" : s.preset, "-crf",
                     std::to_string(s.crf), "-pix_fmt", s.chroma444 ? "yuv444p" : "yuv420p", "-tag:v", "hvc1",
                     "-movflags", "+faststart"});
                break;
            case OutputFormat::ProRes422:
                add({"-c:v", "prores_ks", "-profile:v", "2", "-vendor", "apl0", "-pix_fmt", "yuv422p10le"});
                break;
            case OutputFormat::ProRes422HQ:
                add({"-c:v", "prores_ks", "-profile:v", "3", "-vendor", "apl0", "-pix_fmt", "yuv422p10le"});
                break;
            case OutputFormat::ProRes4444:
                add({"-c:v", "prores_ks", "-profile:v", "4", "-vendor", "apl0", "-pix_fmt", "yuva444p10le"});
                break;
            case OutputFormat::ProRes4444XQ:
                add({"-c:v", "prores_ks", "-profile:v", "5", "-vendor", "apl0", "-pix_fmt", "yuva444p10le"});
                break;
            case OutputFormat::Ffv1Lossless:
                add({"-c:v", "ffv1", "-level", "3", "-g", "1", "-slicecrc", "1", "-pix_fmt", "bgr0"});
                break;
        }
        if (audio && !IsImageSequence(s.format))
        {
            // The audio input goes before the output options: rebuild with it
            // as the second input.
            const auto split = a.begin() + static_cast<std::ptrdiff_t>(inputsEnd);
            std::vector<std::string> withAudio(a.begin(), split);
            AddAudioInput(withAudio, *audio);
            withAudio.insert(withAudio.end(), split, a.end());
            const char* codec = FileExtension(s.format) == ".mp4" ? "aac" : FileExtension(s.format) == ".mkv" ? "flac" : "pcm_s16le";
            withAudio.insert(withAudio.end(), {"-map", "0:v:0", "-map", "1:a:0", "-af", AudioFilter(*audio, 0.0, true),
                                               "-c:a", codec});
            if (std::string_view(codec) == "aac")
                withAudio.insert(withAudio.end(), {"-b:a", "320k"});
            withAudio.push_back("-shortest");
            a = std::move(withAudio);
        }
        a.push_back(outputPath);
        return a;
    }

    std::string QuoteWindowsArgument(std::string_view arg)
    {
        if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string_view::npos)
            return std::string(arg);
        std::string out = "\"";
        std::size_t backslashes = 0;
        for (const char c : arg)
        {
            if (c == '\\')
            {
                ++backslashes;
                continue;
            }
            if (c == '"')
            {
                out.append(backslashes * 2 + 1, '\\');
                out.push_back('"');
            }
            else
            {
                out.append(backslashes, '\\');
                out.push_back(c);
            }
            backslashes = 0;
        }
        out.append(backslashes * 2, '\\');
        out.push_back('"');
        return out;
    }

    std::string JoinWindowsCommandLine(const std::string& executable, const std::vector<std::string>& arguments)
    {
        std::string line = QuoteWindowsArgument(executable);
        for (const auto& a : arguments)
        {
            line.push_back(' ');
            line += QuoteWindowsArgument(a);
        }
        return line;
    }

    std::uint64_t EstimateBytesPerFrame(OutputFormat format, int width, int height, int crf)
    {
        const double pixels = static_cast<double>(std::max(width, 0)) * std::max(height, 0);
        double bitsPerPixel = 0.0;
        switch (format)
        {
            case OutputFormat::PngSequence: bitsPerPixel = 12.0; break;   // typical game footage
            case OutputFormat::JpegSequence: bitsPerPixel = 2.4; break;
            case OutputFormat::H264:
            case OutputFormat::H265:
            {
                // ~0.3 bpp at CRF 16, halving every 6 CRF steps.
                bitsPerPixel = 0.3 * std::pow(2.0, (16.0 - std::clamp(crf, 0, 51)) / 6.0);
                if (format == OutputFormat::H265)
                    bitsPerPixel *= 0.7;
                break;
            }
            case OutputFormat::ProRes422: bitsPerPixel = 3.4; break;
            case OutputFormat::ProRes422HQ: bitsPerPixel = 5.1; break;
            case OutputFormat::ProRes4444: bitsPerPixel = 7.6; break;
            case OutputFormat::ProRes4444XQ: bitsPerPixel = 11.4; break;
            case OutputFormat::Ffv1Lossless: bitsPerPixel = 10.0; break;
        }
        return static_cast<std::uint64_t>(pixels * bitsPerPixel / 8.0);
    }

    std::uint64_t EstimateDiskUsage(const RecordingSettings& s, int width, int height, std::int64_t frames)
    {
        std::uint64_t perFrame = EstimateBytesPerFrame(s.format, width, height, s.crf);
        const auto passes = std::max<std::int64_t>(
            1, std::count_if(s.passes.begin(), s.passes.end(), [](const RenderPass& p) { return p.enabled; }));
        return perFrame * static_cast<std::uint64_t>(std::max<std::int64_t>(frames, 0)) *
               static_cast<std::uint64_t>(passes);
    }

    std::string FormatBytes(std::uint64_t bytes)
    {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double v = static_cast<double>(bytes);
        int u = 0;
        while (v >= 1024.0 && u < 4)
        {
            v /= 1024.0;
            ++u;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
        return buf;
    }

    FrameSteps ComputeFrameSteps(const TimeRemap& remap, const FrameRate& rate, std::int64_t firstFrame,
                                 std::int64_t count)
    {
        FrameSteps out;
        if (count <= 0 || !rate.Valid())
            return out;
        out.stepMs.reserve(static_cast<std::size_t>(count));
        auto sceneMs = [&](std::int64_t frame) {
            return static_cast<std::int64_t>(std::llround(remap.SceneTime(rate.FrameToSeconds(frame)) * 1000.0));
        };
        std::int64_t previous = sceneMs(firstFrame);
        out.firstSceneMs = previous;
        for (std::int64_t k = 0; k < count; ++k)
        {
            const std::int64_t next = sceneMs(firstFrame + k + 1);
            const auto step = static_cast<std::int32_t>(std::max<std::int64_t>(0, next - previous));
            if (step == 0)
                out.hasFreeze = true;
            out.stepMs.push_back(step);
            previous = next;
        }
        return out;
    }
}  // namespace iris
