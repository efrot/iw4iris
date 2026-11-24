#include <algorithm>
#include <numeric>

#include "iris/recording.hpp"
#include "test.hpp"

using namespace iris;

namespace
{
    bool HasMessage(const std::vector<Issue>& issues, std::string_view needle)
    {
        return std::any_of(issues.begin(), issues.end(),
                           [&](const Issue& i) { return i.message.find(needle) != std::string::npos; });
    }
    bool Contains(const std::vector<std::string>& args, std::string_view a, std::string_view b)
    {
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == a && args[i + 1] == b)
                return true;
        return false;
    }
}  // namespace

TEST_CASE("recording: a default PNG recording at game size is valid")
{
    RecordingSettings s;
    const auto issues = ValidateRecording(s, 1920, 1080, false);
    CHECK(!HasErrors(issues));
}

TEST_CASE("recording: validation errors")
{
    RecordingSettings s;
    s.format = OutputFormat::H264;
    auto issues = ValidateRecording(s, 1921, 1080, true);
    CHECK(HasErrors(issues));
    CHECK(HasMessage(issues, "even"));

    s.format = OutputFormat::ProRes422HQ;
    issues = ValidateRecording(s, 1920, 1080, false);
    CHECK(HasMessage(issues, "FFmpeg"));

    s = {};
    s.passes.push_back({PassKind::Depth, true, ""});
    issues = ValidateRecording(s, 1920, 1080, true);
    CHECK(HasMessage(issues, "depth"));

    s = {};
    s.width = 3840;
    s.height = 2160;
    issues = ValidateRecording(s, 1920, 1080, true);
    CHECK(HasMessage(issues, "Custom render sizes"));

    s = {};
    s.frameRate = {0, 1};
    CHECK(HasErrors(ValidateRecording(s, 1920, 1080, true)));
    s = {};
    s.range = RangeMode::Custom;
    s.customStart = 3;
    s.customEnd = 1;
    CHECK(HasErrors(ValidateRecording(s, 1920, 1080, true)));
}

TEST_CASE("recording: ffmpeg arguments per format")
{
    RecordingSettings s;
    s.frameRate = {24000, 1001};
    s.format = OutputFormat::ProRes4444XQ;
    auto a = BuildFfmpegArguments(s, 1920, 1080, "out.mov", 0);
    CHECK(Contains(a, "-f", "rawvideo"));
    CHECK(Contains(a, "-pix_fmt", "bgra"));
    CHECK(Contains(a, "-video_size", "1920x1080"));
    CHECK(Contains(a, "-framerate", "24000/1001"));
    CHECK(Contains(a, "-c:v", "prores_ks"));
    CHECK(Contains(a, "-profile:v", "5"));
    CHECK(a.back() == "out.mov");

    s.format = OutputFormat::H265;
    s.crf = 20;
    a = BuildFfmpegArguments(s, 1280, 720, "x.mp4", 0);
    CHECK(Contains(a, "-c:v", "libx265"));
    CHECK(Contains(a, "-crf", "20"));
    CHECK(Contains(a, "-pix_fmt", "yuv420p"));

    s.format = OutputFormat::PngSequence;
    a = BuildFfmpegArguments(s, 640, 360, "seq_%06d.png", 120);
    CHECK(Contains(a, "-start_number", "120"));
    CHECK(BuildOutputPath(s, "C:/renders", "shot") == "C:/renders/shot_%06d.png");
    s.format = OutputFormat::Ffv1Lossless;
    CHECK(BuildOutputPath(s, "out/", "shot") == "out/shot.mkv");
}

TEST_CASE("recording: windows command line quoting")
{
    CHECK(QuoteWindowsArgument("abc") == "abc");
    CHECK(QuoteWindowsArgument("") == "\"\"");
    CHECK(QuoteWindowsArgument("a b") == "\"a b\"");
    CHECK(QuoteWindowsArgument("a\"b") == "\"a\\\"b\"");
    CHECK(QuoteWindowsArgument("C:\\My Renders\\") == "\"C:\\My Renders\\\\\"");
    CHECK(QuoteWindowsArgument("C:\\path\\x") == "C:\\path\\x");
    CHECK(JoinWindowsCommandLine("C:\\ff mpeg\\ffmpeg.exe", {"-i", "pipe:0"}) == "\"C:\\ff mpeg\\ffmpeg.exe\" -i pipe:0");
}

TEST_CASE("recording: file names")
{
    CHECK(SanitizeFileName("con") == "_con");
    CHECK(SanitizeFileName("a:b*c?") == "a_b_c_");
    CHECK(SanitizeFileName("trailing. ") == "trailing");
    CHECK(SanitizeFileName("") == "render");
    RecordingSettings s;
    s.namePattern = "{project}-{camera}-{pass}-{w}x{h}@{fps}";
    CHECK(ExpandNamePattern(s, "My/Edit", "Dolly 1", "beauty", "2026", 1920, 1080) ==
          "My_Edit-Dolly 1-beauty-1920x1080@60");
}

TEST_CASE("recording: frame steps keep exact timing")
{
    TimeRemap remap;
    const auto steps = ComputeFrameSteps(remap, FrameRate{60, 1}, 0, 600);
    CHECK(steps.stepMs.size() == 600);
    CHECK(!steps.hasFreeze);
    const auto total = std::accumulate(steps.stepMs.begin(), steps.stepMs.end(), 0LL);
    CHECK(total == 10000);
    for (auto s : steps.stepMs)
        CHECK(s == 16 || s == 17);

    const auto ntsc = ComputeFrameSteps(remap, FrameRate{24000, 1001}, 0, 24000);
    const auto ntscTotal = std::accumulate(ntsc.stepMs.begin(), ntsc.stepMs.end(), 0LL);
    CHECK(ntscTotal == 1001000);

    remap.Speed().SetDefaultValue(0.0);
    CHECK(ComputeFrameSteps(remap, FrameRate{60, 1}, 0, 10).hasFreeze);
}

TEST_CASE("recording: disk estimate and ranges")
{
    RecordingSettings s;
    CHECK(EstimateDiskUsage(s, 1920, 1080, 60) > 0);
    s.format = OutputFormat::ProRes4444XQ;
    CHECK(EstimateBytesPerFrame(OutputFormat::ProRes4444XQ, 1920, 1080, 0) >
          EstimateBytesPerFrame(OutputFormat::ProRes422, 1920, 1080, 0));
    CHECK(FormatBytes(1536) == "1.5 KB");
    TimelineSettings t;
    t.inPoint = 1;
    t.outPoint = 3;
    s.range = RangeMode::InOut;
    CHECK_NEAR(ResolveRange(s, t).start, 1.0, 0);
    s.range = RangeMode::Full;
    CHECK_NEAR(ResolveRange(s, t).end, t.end, 0);
    for (auto f : {OutputFormat::PngSequence, OutputFormat::JpegSequence, OutputFormat::H264, OutputFormat::H265,
                   OutputFormat::ProRes422, OutputFormat::ProRes422HQ, OutputFormat::ProRes4444,
                   OutputFormat::ProRes4444XQ, OutputFormat::Ffv1Lossless})
        CHECK(OutputFormatFromString(ToString(f)) == f);
}
