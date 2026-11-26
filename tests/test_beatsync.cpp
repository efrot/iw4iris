#include "iris/beatsync.hpp"
#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace iris;

namespace
{
    AudioTrack Song()
    {
        AudioTrack a;
        a.path = "music/song.wav";
        a.name = "song";
        a.fileDuration = 30.0;
        a.start = 2.0;    // the song's 5 s mark plays at timeline 2 s
        a.trimIn = 5.0;
        a.trimOut = 20.0; // until 17 s on the timeline
        a.bpm = 120.0;
        a.beats = BeatGrid(120.0, 0.25, 30.0);  // 0.25, 0.75, ...
        return a;
    }

    bool Contains(const std::vector<std::string>& args, const std::string& value)
    {
        return std::find(args.begin(), args.end(), value) != args.end();
    }

    std::size_t IndexOf(const std::vector<std::string>& args, const std::string& value)
    {
        return static_cast<std::size_t>(std::find(args.begin(), args.end(), value) - args.begin());
    }
}

TEST_CASE("beatsync: file and timeline time, trimmed span")
{
    const auto a = Song();
    CHECK(AudioToTimeline(a, 5.0) == 2.0);
    CHECK(TimelineToAudio(a, 3.5) == 6.5);
    CHECK(AudioEnd(a) == 20.0);
    CHECK(AudioTimelineEnd(a) == 17.0);
    auto untrimmed = a;
    untrimmed.trimOut = 0.0;
    CHECK(AudioEnd(untrimmed) == 30.0);
    // Only beats inside the trim, as timeline times.
    const auto beats = BeatTimes(a, 0.0, 100.0);
    CHECK(beats.size() == 30);  // 5.25 .. 19.75 in the file
    CHECK(std::abs(beats.front() - 2.25) < 1e-9);
    CHECK(std::abs(beats.back() - 16.75) < 1e-9);
    CHECK(BeatTimes(a, 3.0, 4.0).size() == 2);  // 3.25, 3.75
    CHECK(NearestBeat(a, 3.3, 0.1).has_value());
    CHECK(std::abs(*NearestBeat(a, 3.3, 0.1) - 3.25) < 1e-9);
    CHECK(!NearestBeat(a, 3.5, 0.1).has_value());
}

TEST_CASE("beatsync: grid, tempo doubling and halving, beat edits")
{
    const auto grid = BeatGrid(100.0, 1.3, 10.0);
    CHECK(std::abs(grid.front() - 0.1) < 1e-9);  // 1.3 mod 0.6
    CHECK(std::abs(grid[1] - grid[0] - 0.6) < 1e-9);
    CHECK(BeatGrid(0.0, 0.0, 10.0).empty());

    auto a = Song();
    const auto before = a.beats.size();
    ScaleTempo(a, false);
    CHECK(a.bpm == 60.0);
    CHECK(a.beats.size() == (before + 1) / 2);
    CHECK(a.beatsEdited);
    ScaleTempo(a, true);
    CHECK(a.bpm == 120.0);
    CHECK(a.beats.size() == 2 * ((before + 1) / 2) - 1);

    a = Song();
    CHECK(MoveBeat(a, 3.25, 3.30, 0.1));
    CHECK(NearestBeat(a, 3.30, 0.001).has_value());
    CHECK(!MoveBeat(a, 3.5, 3.6, 0.1));
    AddBeat(a, 3.5);
    CHECK(NearestBeat(a, 3.5, 0.001).has_value());
    CHECK(RemoveBeat(a, 3.5, 0.01));
    CHECK(!NearestBeat(a, 3.5, 0.001).has_value());
    CHECK(std::is_sorted(a.beats.begin(), a.beats.end()));
}

TEST_CASE("beatsync: analysis results are stored as editable beats")
{
    AudioTrack a = Song();
    a.beatsEdited = true;
    AudioAnalysis an;
    an.tempo = {128.0, 0.9};
    an.beats = {1.0, 0.5, 1.5};
    ApplyAnalysis(a, an);
    CHECK(a.bpm == 128.0);
    CHECK(a.tempoConfidence == 0.9);
    CHECK(a.beats == (std::vector<double>{0.5, 1.0, 1.5}));
    CHECK(!a.beatsEdited);
}

TEST_CASE("beatsync: cuts and markers on beats, speed ramp between beats")
{
    Project p = MakeDefaultProject("Beats");
    p.audio = Song();
    const int cuts = AddCutsOnBeats(p, 2.0, 6.0, 2, {"cam-1", "cam-2", "missing"}, "");
    CHECK(cuts == 4);  // 2.25, 3.25, 4.25, 5.25
    CHECK(p.useCameraCuts);
    CHECK(p.cuts.Cuts().size() == 4);
    CHECK(p.cuts.Cuts()[0].cameraId == "cam-1");
    CHECK(p.cuts.Cuts()[1].cameraId == "cam-2");
    CHECK(p.cuts.Cuts()[2].cameraId == "cam-1");
    CHECK(std::abs(p.cuts.Cuts()[3].time - 5.25) < 1e-9);
    // In an angle.
    Project q = p;
    q.cuts.Clear();
    CHECK(AddCutsOnBeats(q, 2.0, 3.0, 1, {"cam-1"}, "angle-1") == 2);
    CHECK(q.cuts.Cuts()[0].angle == "angle-1");

    CHECK(AddBeatMarkers(p, 2.0, 4.0, 1) == 4);
    CHECK(AddBeatMarkers(p, 2.0, 4.0, 1) == 0);  // not twice
    CHECK(std::count_if(p.markers.begin(), p.markers.end(), [](const Marker& m) { return m.kind == "beat"; }) == 4);

    SpeedRampBetween(p, 3.25, 4.25, 0.25);
    CHECK(std::abs(p.timeRemap.SpeedAt(3.25) - 1.0) < 1e-6);
    CHECK(std::abs(p.timeRemap.SpeedAt(3.75) - 0.25) < 1e-6);
    CHECK(std::abs(p.timeRemap.SpeedAt(4.25) - 1.0) < 1e-6);
    // Applying it again over the same beats does not stack keys.
    const auto keys = p.timeRemap.Speed().Keys().size();
    SpeedRampBetween(p, 3.25, 4.25, 0.25);
    CHECK(p.timeRemap.Speed().Keys().size() == keys);

    Project none = MakeDefaultProject("x");
    CHECK(AddCutsOnBeats(none, 0.0, 10.0, 1, {"cam-1"}, "") == 0);
    CHECK(AddBeatMarkers(none, 0.0, 10.0, 1) == 0);
}

TEST_CASE("beatsync: the music of a recording range")
{
    const auto a = Song();  // plays on the timeline from 2 s to 17 s
    // A range starting before the music: silence first.
    auto in = AudioForRange(a, "C:/m/song.wav", 1.0, 4.0);
    CHECK(in.has_value());
    CHECK(std::abs(in->delay - 1.0) < 1e-9);
    CHECK(std::abs(in->fileStart - 5.0) < 1e-9);
    CHECK(std::abs(in->duration - 2.0) < 1e-9);
    // Inside it.
    in = AudioForRange(a, "C:/m/song.wav", 10.0, 12.5);
    CHECK(in->delay == 0.0);
    CHECK(std::abs(in->fileStart - 13.0) < 1e-9);
    CHECK(std::abs(in->duration - 2.5) < 1e-9);
    // Past its end, or muted: none.
    CHECK(!AudioForRange(a, "C:/m/song.wav", 17.5, 19.0));
    auto muted = a;
    muted.muted = true;
    CHECK(!AudioForRange(muted, "C:/m/song.wav", 3.0, 4.0));
}

TEST_CASE("beatsync: FFmpeg gets the music as a second input")
{
    RecordingSettings s;
    s.format = OutputFormat::H264;
    AudioInput in;
    in.path = "C:/m/song.wav";
    in.fileStart = 13.0;
    in.duration = 2.5;
    in.delay = 0.5;
    in.volume = 0.8;
    const auto a = BuildFfmpegArguments(s, 1920, 1080, "out.mp4", 0, &in);
    CHECK(IndexOf(a, "pipe:0") < IndexOf(a, "C:/m/song.wav"));
    CHECK(IndexOf(a, "C:/m/song.wav") < IndexOf(a, "-c:v"));
    CHECK(Contains(a, "13.000000"));
    CHECK(Contains(a, "2.500000"));
    CHECK(Contains(a, "aac"));
    CHECK(Contains(a, "-shortest"));
    CHECK(Contains(a, "1:a:0"));
    const auto filter = a[IndexOf(a, "-af") + 1];
    CHECK(filter.find("adelay=500:all=1") != std::string::npos);
    CHECK(filter.find("volume=0.8000") != std::string::npos);
    CHECK(a.back() == "out.mp4");
    // MOV gets PCM, MKV FLAC.
    s.format = OutputFormat::ProRes422HQ;
    CHECK(Contains(BuildFfmpegArguments(s, 64, 64, "o.mov", 0, &in), "pcm_s16le"));
    s.format = OutputFormat::Ffv1Lossless;
    CHECK(Contains(BuildFfmpegArguments(s, 64, 64, "o.mkv", 0, &in), "flac"));
    // Without music nothing changes; an image sequence never takes it.
    s.format = OutputFormat::H264;
    CHECK(!Contains(BuildFfmpegArguments(s, 64, 64, "o.mp4", 0), "-shortest"));
    s.format = OutputFormat::PngSequence;
    CHECK(!Contains(BuildFfmpegArguments(s, 64, 64, "o_%06d.png", 0, &in), "C:/m/song.wav"));
    // The sidecar for a sequence: padded to the range.
    const auto side = BuildAudioSidecarArguments(in, 3.0, "o_audio.wav");
    CHECK(Contains(side, "pcm_s16le"));
    CHECK(side.back() == "o_audio.wav");
    CHECK(side[IndexOf(side, "-af") + 1].find("apad=whole_dur=3.000000") != std::string::npos);
}

TEST_CASE("beatsync: the audio track round-trips and is checked like the demo")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "iw4iris-test-audio";
    fs::remove_all(dir);
    fs::create_directories(dir / "music");
    { std::ofstream(dir / "music" / "song.wav") << "riff"; }
    Project p = MakeDefaultProject("Music");
    p.audio = Song();
    p.audio->path = (dir / "music" / "song.wav").string();
    p.audio->fileSize = 4;
    p.audio->volume = 0.7;
    p.audio->beatsEdited = true;
    const auto text = SerializeProject(p);
    const auto r = ParseProject(text);
    CHECK(r.project.has_value());
    CHECK(r.project->audio == p.audio);
    const fs::path file = dir / "music.iw4iris";
    CHECK(SaveProjectFile(file, p));
    const auto loaded = LoadProjectFile(file);
    CHECK(loaded.project->audio->path == "music/song.wav");
    CHECK(ResolveAudioPath(*loaded.project, file).has_value());
    CHECK(FindMissingReferences(*loaded.project, file).empty());
    fs::remove(dir / "music" / "song.wav");
    CHECK(FindMissingReferences(*loaded.project, file).size() == 1);
    fs::remove_all(dir);

    // A part of a per-angle image sequence: only the first writes the music,
    // for the whole range.
    RecordingSettings rec;
    rec.range = RangeMode::Custom;
    rec.customStart = 1.0;
    rec.customEnd = 8.0;
    p.angles.push_back(DemoAngle{"angle-1", "Bob", {}, "Bob", 0, SyncState::Confirmed, 1.0});
    p.useCameraCuts = true;
    p.cuts.SetCuts({{0.0, "cam-1"}, {3.0, "cam-3", "angle-1"}});
    const auto jobs = PlanAngleRenders(p, rec, "d");
    CHECK(jobs.size() == 2);
    CHECK(jobs[0].settings.includeAudio);
    CHECK(!jobs[1].settings.includeAudio);
    CHECK(jobs[0].settings.audioRangeStart == 1.0);
    CHECK(jobs[0].settings.audioRangeEnd == 8.0);
    const auto again = ParseProject(SerializeProject([&] { auto x = p; x.renderQueue = jobs; return x; }()));
    CHECK(again.project->renderQueue[1].settings.includeAudio == false);
    CHECK(again.project->renderQueue[0].settings.audioRangeEnd == 8.0);
}
