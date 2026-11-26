#include "iris/audio.hpp"
#include "test.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace iris;

namespace
{
    constexpr int kRate = 44100;

    // Deterministic noise.
    struct Lcg
    {
        std::uint32_t state = 12345;
        double Next()
        {
            state = state * 1664525u + 1013904223u;
            return (state >> 8) / 8388608.0 - 1.0;
        }
    };

    // A click track: short decaying bursts at `bpm` from `start` for `seconds`,
    // over a quiet noise floor.
    AudioClip Clicks(double bpm, double start, double seconds, std::vector<double>* times = nullptr)
    {
        AudioClip c;
        c.sampleRate = kRate;
        c.channels = 1;
        c.samples.assign(static_cast<std::size_t>((start + seconds + 1.0) * kRate), 0.0f);
        Lcg noise;
        for (auto& s : c.samples)
            s = static_cast<float>(0.003 * noise.Next());
        for (double t = start; t < start + seconds; t += 60.0 / bpm)
        {
            if (times)
                times->push_back(t);
            const auto at = static_cast<std::size_t>(std::lround(t * kRate));
            for (std::size_t i = 0; i < 600 && at + i < c.samples.size(); ++i)
                c.samples[at + i] += static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 120.0) * noise.Next());
        }
        return c;
    }

    double Nearest(const std::vector<double>& v, double t)
    {
        double best = 1e9;
        for (const double x : v)
            best = std::min(best, std::abs(x - t));
        return best;
    }
}

TEST_CASE("audio: WAV round trip, stereo mixed to mono")
{
    std::vector<float> interleaved;
    for (int i = 0; i < 1000; ++i)
    {
        interleaved.push_back(static_cast<float>(std::sin(i * 0.05)));
        interleaved.push_back(static_cast<float>(0.5 * std::sin(i * 0.05)));
    }
    const auto bytes = EncodeWav16(interleaved, 48000, 2);
    std::string error;
    const auto clip = DecodeWav(bytes, &error);
    CHECK(clip.has_value());
    CHECK(error.empty());
    CHECK(clip->sampleRate == 48000);
    CHECK(clip->channels == 2);
    CHECK(clip->samples.size() == 1000);
    double worst = 0.0;
    for (int i = 0; i < 1000; ++i)
        worst = std::max(worst, std::abs(clip->samples[static_cast<std::size_t>(i)] - 0.75 * std::sin(i * 0.05)));
    CHECK(worst < 1e-4);
    CHECK(std::abs(clip->Duration() - 1000.0 / 48000.0) < 1e-12);
}

TEST_CASE("audio: 24-bit PCM, float and extensible WAV")
{
    auto header = [](std::uint16_t format, std::uint16_t bits, std::uint32_t dataBytes, bool extensible) {
        std::vector<unsigned char> b;
        auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i))); };
        auto put16 = [&](std::uint16_t v) { b.push_back(static_cast<unsigned char>(v)); b.push_back(static_cast<unsigned char>(v >> 8)); };
        auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
        tag("RIFF"); put32(0); tag("WAVE");
        tag("LIST"); put32(3); b.insert(b.end(), {'a', 'b', 'c', 0});  // an odd chunk with its pad byte
        tag("fmt "); put32(extensible ? 40 : 16);
        put16(extensible ? 0xFFFE : format); put16(1); put32(8000); put32(8000u * bits / 8); put16(static_cast<std::uint16_t>(bits / 8)); put16(bits);
        if (extensible)
        {
            put16(22); put16(bits); put32(4);
            put16(format);
            for (int i = 0; i < 14; ++i) b.push_back(0);
        }
        tag("data"); put32(dataBytes);
        return b;
    };
    // 24 bit: +0.5 and -0.25
    auto b24 = header(1, 24, 6, false);
    for (const int c : {0x00, 0x00, 0x40, 0x00, 0x00, 0xE0})
        b24.push_back(static_cast<unsigned char>(c));
    const auto c24 = DecodeWav(b24);
    CHECK(c24.has_value());
    CHECK(std::abs(c24->samples[0] - 0.5f) < 1e-6);
    CHECK(std::abs(c24->samples[1] + 0.25f) < 1e-6);
    // float 32 in an extensible header
    auto bf = header(3, 32, 4, true);
    const float v = -0.125f;
    std::uint32_t u;
    std::memcpy(&u, &v, 4);
    for (int i = 0; i < 4; ++i)
        bf.push_back(static_cast<unsigned char>(u >> (8 * i)));
    const auto cf = DecodeWav(bf);
    CHECK(cf.has_value());
    CHECK(cf->samples.size() == 1);
    CHECK(std::abs(cf->samples[0] + 0.125f) < 1e-7);
}

TEST_CASE("audio: WAV errors are reported, not guessed")
{
    std::string error;
    CHECK(!DecodeWav({'n', 'o', 'p', 'e'}, &error));
    CHECK(error.find("Not a WAV") != std::string::npos);
    auto adpcm = EncodeWav16({0.0f, 0.1f}, 8000, 1);
    adpcm[20] = 2;  // MS ADPCM
    CHECK(!DecodeWav(adpcm, &error));
    CHECK(error.find("Unsupported") != std::string::npos);
    auto nodata = EncodeWav16({}, 8000, 1);
    nodata.resize(36);
    CHECK(!DecodeWav(nodata, &error));
    CHECK(error.find("no audio data") != std::string::npos);
}

TEST_CASE("audio: raw s16 and waveform peaks")
{
    const std::vector<unsigned char> raw{0x00, 0x40, 0x00, 0xC0};  // +0.5, -0.5
    const auto clip = ClipFromS16(raw, 8000);
    CHECK(clip.samples.size() == 2);
    CHECK(std::abs(clip.samples[0] - 0.5f) < 1e-6);
    CHECK(std::abs(clip.samples[1] + 0.5f) < 1e-6);

    AudioClip sine;
    sine.sampleRate = 8000;
    sine.channels = 1;
    for (int i = 0; i < 8000; ++i)
        sine.samples.push_back(static_cast<float>(0.9 * std::sin(2.0 * 3.14159265358979 * 100.0 * i / 8000.0)));
    const auto peaks = ComputePeaks(sine, 0.1);
    CHECK(peaks.high.size() == 10);
    CHECK(std::abs(peaks.secondsPerBucket - 0.1) < 1e-12);
    for (std::size_t i = 0; i < peaks.high.size(); ++i)
    {
        CHECK(peaks.high[i] > 0.85f);
        CHECK(peaks.low[i] < -0.85f);
    }
}

TEST_CASE("audio: onsets of a click track, none in the silence")
{
    std::vector<double> clicks;
    const auto clip = Clicks(120.0, 1.0, 8.0, &clicks);
    const auto env = ComputeOnsetEnvelope(clip);
    const auto onsets = DetectOnsets(env);
    CHECK(onsets.size() == clicks.size());
    std::vector<double> times;
    for (const auto& o : onsets)
        times.push_back(o.time);
    for (const double t : clicks)
        CHECK(Nearest(times, t) < 0.03);
    for (const auto& o : onsets)
        CHECK(o.time > 0.95);
}

TEST_CASE("audio: tempo of click tracks")
{
    for (const double bpm : {90.0, 120.0, 140.0})
    {
        const auto t = EstimateTempo(ComputeOnsetEnvelope(Clicks(bpm, 0.5, 12.0)));
        CHECK(std::abs(t.bpm - bpm) < 1.5);
        CHECK(t.confidence > 0.3);
    }
    // Noise alone has no clear tempo.
    AudioClip noise;
    noise.sampleRate = kRate;
    noise.channels = 1;
    Lcg lcg;
    for (int i = 0; i < kRate * 6; ++i)
        noise.samples.push_back(static_cast<float>(0.3 * lcg.Next()));
    const auto t = EstimateTempo(ComputeOnsetEnvelope(noise));
    CHECK(t.confidence < 0.6);
}

TEST_CASE("audio: beats follow the clicks and stop at the silence")
{
    std::vector<double> clicks;
    const auto clip = Clicks(128.0, 2.0, 10.0, &clicks);
    const auto analysis = AnalyseAudio(clip);
    CHECK(std::abs(analysis.tempo.bpm - 128.0) < 1.5);
    CHECK(analysis.beats.size() >= clicks.size() - 1);
    CHECK(analysis.beats.size() <= clicks.size() + 1);
    for (const double b : analysis.beats)
    {
        CHECK(b > 1.9);
        CHECK(b < 12.1);
        CHECK(Nearest(clicks, b) < 0.03);
    }
}

TEST_CASE("audio: empty input")
{
    AudioClip empty;
    CHECK(ComputeOnsetEnvelope(empty).strength.empty());
    CHECK(DetectOnsets(ComputeOnsetEnvelope(empty)).empty());
    CHECK(EstimateTempo(ComputeOnsetEnvelope(empty)).bpm == 0.0);
    CHECK(TrackBeats(ComputeOnsetEnvelope(empty), 120.0).empty());
    CHECK(ComputePeaks(empty, 0.1).high.empty());
}
