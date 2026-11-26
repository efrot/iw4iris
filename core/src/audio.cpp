// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/audio.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <numeric>

namespace iris
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        std::uint32_t U32(const unsigned char* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint16_t U16(const unsigned char* p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        bool Fail(std::string* error, const char* message)
        {
            if (error)
                *error = message;
            return false;
        }

        // One sample at `p` in -1..1.
        double ReadSample(const unsigned char* p, int bits, bool isFloat)
        {
            if (isFloat)
            {
                if (bits == 32)
                {
                    float f;
                    const std::uint32_t u = U32(p);
                    std::memcpy(&f, &u, sizeof(f));
                    return std::isfinite(f) ? static_cast<double>(f) : 0.0;
                }
                std::uint64_t u = static_cast<std::uint64_t>(U32(p)) | (static_cast<std::uint64_t>(U32(p + 4)) << 32);
                double d;
                std::memcpy(&d, &u, sizeof(d));
                return std::isfinite(d) ? d : 0.0;
            }
            switch (bits)
            {
                case 8: return (static_cast<int>(p[0]) - 128) / 128.0;
                case 16: return static_cast<std::int16_t>(U16(p)) / 32768.0;
                case 24:
                {
                    std::int32_t v = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                    if (v & 0x800000)
                        v -= 0x1000000;
                    return v / 8388608.0;
                }
                case 32: return static_cast<std::int32_t>(U32(p)) / 2147483648.0;
                default: return 0.0;
            }
        }

        void Fft(std::vector<std::complex<double>>& a)
        {
            const std::size_t n = a.size();
            for (std::size_t i = 1, j = 0; i < n; ++i)
            {
                std::size_t bit = n >> 1;
                for (; j & bit; bit >>= 1)
                    j ^= bit;
                j ^= bit;
                if (i < j)
                    std::swap(a[i], a[j]);
            }
            for (std::size_t len = 2; len <= n; len <<= 1)
            {
                const double angle = -2.0 * kPi / static_cast<double>(len);
                const std::complex<double> wlen(std::cos(angle), std::sin(angle));
                for (std::size_t i = 0; i < n; i += len)
                {
                    std::complex<double> w(1.0, 0.0);
                    for (std::size_t k = 0; k < len / 2; ++k)
                    {
                        const auto u = a[i + k], v = a[i + k + len / 2] * w;
                        a[i + k] = u + v;
                        a[i + k + len / 2] = u - v;
                        w *= wlen;
                    }
                }
            }
        }

        int PowerOfTwo(int n)
        {
            int p = 64;
            while (p < n && p < (1 << 16))
                p <<= 1;
            return p;
        }

        double Median(std::vector<double> v)
        {
            if (v.empty())
                return 0.0;
            const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
            std::nth_element(v.begin(), mid, v.end());
            return *mid;
        }

        // Gaussian smoothing with the given standard deviation (frames).
        std::vector<double> Smooth(const std::vector<double>& x, double sigma)
        {
            if (sigma <= 0.25 || x.empty())
                return x;
            const int radius = static_cast<int>(std::ceil(3.0 * sigma));
            std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
            double sum = 0.0;
            for (int i = -radius; i <= radius; ++i)
                sum += kernel[static_cast<std::size_t>(i + radius)] = std::exp(-0.5 * (i / sigma) * (i / sigma));
            for (auto& k : kernel)
                k /= sum;
            std::vector<double> out(x.size(), 0.0);
            const int n = static_cast<int>(x.size());
            for (int i = 0; i < n; ++i)
            {
                double acc = 0.0;
                for (int j = -radius; j <= radius; ++j)
                    if (i + j >= 0 && i + j < n)
                        acc += x[static_cast<std::size_t>(i + j)] * kernel[static_cast<std::size_t>(j + radius)];
                out[static_cast<std::size_t>(i)] = acc;
            }
            return out;
        }

        double FrameTime(const OnsetEnvelope& e, double frame)
        {
            return frame * e.secondsPerFrame + e.firstFrameSeconds;
        }
    }  // namespace

    std::optional<AudioClip> DecodeWav(const std::vector<unsigned char>& bytes, std::string* error)
    {
        if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        {
            Fail(error, "Not a WAV file (no RIFF / WAVE header).");
            return std::nullopt;
        }
        int format = 0, channels = 0, rate = 0, bits = 0, blockAlign = 0;
        const unsigned char* data = nullptr;
        std::size_t dataSize = 0;
        std::size_t pos = 12;
        while (pos + 8 <= bytes.size())
        {
            const unsigned char* chunk = bytes.data() + pos;
            const std::size_t size = U32(chunk + 4);
            const std::size_t body = pos + 8;
            const std::size_t available = std::min(size, bytes.size() - body);
            if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16)
            {
                format = U16(chunk + 8);
                channels = U16(chunk + 10);
                rate = static_cast<int>(U32(chunk + 12));
                blockAlign = U16(chunk + 20);
                bits = U16(chunk + 22);
                if (format == 0xFFFE && available >= 26)  // WAVE_FORMAT_EXTENSIBLE: the sub-format's code
                    format = U16(chunk + 8 + 24);
            }
            else if (std::memcmp(chunk, "data", 4) == 0)
            {
                data = bytes.data() + body;
                dataSize = available;  // a truncated file keeps what is there
            }
            pos = body + size + (size & 1);
            if (size > bytes.size())
                break;
        }
        if (!format)
        {
            Fail(error, "The WAV file has no format chunk.");
            return std::nullopt;
        }
        if (!data)
        {
            Fail(error, "The WAV file has no audio data.");
            return std::nullopt;
        }
        const bool isFloat = format == 3;
        if ((format != 1 && format != 3) || (isFloat && bits != 32 && bits != 64) ||
            (!isFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32))
        {
            Fail(error, "Unsupported WAV encoding (only PCM 8 / 16 / 24 / 32 bit and float 32 / 64): use FFmpeg to "
                        "read it.");
            return std::nullopt;
        }
        if (channels < 1 || rate < 1000 || rate > 768000 || blockAlign < channels * bits / 8)
        {
            Fail(error, "The WAV file's format is inconsistent.");
            return std::nullopt;
        }
        AudioClip clip;
        clip.sampleRate = rate;
        clip.channels = channels;
        const std::size_t frames = dataSize / static_cast<std::size_t>(blockAlign);
        clip.samples.resize(frames);
        const int bytesPer = bits / 8;
        for (std::size_t f = 0; f < frames; ++f)
        {
            const unsigned char* p = data + f * static_cast<std::size_t>(blockAlign);
            double sum = 0.0;
            for (int c = 0; c < channels; ++c)
                sum += ReadSample(p + c * bytesPer, bits, isFloat);
            clip.samples[f] = static_cast<float>(std::clamp(sum / channels, -1.0, 1.0));
        }
        return clip;
    }

    std::vector<unsigned char> EncodeWav16(const std::vector<float>& interleaved, int sampleRate, int channels)
    {
        channels = std::max(1, channels);
        const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * 2);
        std::vector<unsigned char> out;
        out.reserve(44 + dataBytes);
        auto put32 = [&](std::uint32_t v) {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
        };
        auto put16 = [&](std::uint16_t v) {
            out.push_back(static_cast<unsigned char>(v & 0xFF));
            out.push_back(static_cast<unsigned char>(v >> 8));
        };
        auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
        tag("RIFF");
        put32(36 + dataBytes);
        tag("WAVE");
        tag("fmt ");
        put32(16);
        put16(1);
        put16(static_cast<std::uint16_t>(channels));
        put32(static_cast<std::uint32_t>(sampleRate));
        put32(static_cast<std::uint32_t>(sampleRate * channels * 2));
        put16(static_cast<std::uint16_t>(channels * 2));
        put16(16);
        tag("data");
        put32(dataBytes);
        for (const float s : interleaved)
        {
            const auto v = static_cast<std::int16_t>(std::lround(std::clamp(static_cast<double>(s), -1.0, 1.0) * 32767.0));
            put16(static_cast<std::uint16_t>(v));
        }
        return out;
    }

    AudioClip ClipFromS16(const std::vector<unsigned char>& raw, int sampleRate)
    {
        AudioClip clip;
        clip.sampleRate = sampleRate;
        clip.channels = 1;
        clip.samples.resize(raw.size() / 2);
        for (std::size_t i = 0; i < clip.samples.size(); ++i)
            clip.samples[i] = static_cast<float>(static_cast<std::int16_t>(U16(raw.data() + 2 * i)) / 32768.0);
        return clip;
    }

    WaveformPeaks ComputePeaks(const AudioClip& clip, double secondsPerBucket)
    {
        WaveformPeaks peaks;
        if (clip.sampleRate <= 0 || clip.samples.empty())
            return peaks;
        const auto bucket = static_cast<std::size_t>(std::max(1L, std::lround(secondsPerBucket * clip.sampleRate)));
        peaks.secondsPerBucket = static_cast<double>(bucket) / clip.sampleRate;
        for (std::size_t i = 0; i < clip.samples.size(); i += bucket)
        {
            const auto end = std::min(clip.samples.size(), i + bucket);
            const auto [lo, hi] = std::minmax_element(clip.samples.begin() + static_cast<std::ptrdiff_t>(i),
                                                      clip.samples.begin() + static_cast<std::ptrdiff_t>(end));
            peaks.low.push_back(*lo);
            peaks.high.push_back(*hi);
        }
        return peaks;
    }

    OnsetEnvelope ComputeOnsetEnvelope(const AudioClip& clip, const OnsetSettings& settings)
    {
        OnsetEnvelope e;
        if (clip.sampleRate <= 0 || clip.samples.empty())
            return e;
        const int fft = PowerOfTwo(std::max(64, settings.fftSize));
        const int hop = std::clamp(settings.hop, 16, fft);
        e.secondsPerFrame = static_cast<double>(hop) / clip.sampleRate;
        e.firstFrameSeconds = static_cast<double>(fft) / 2.0 / clip.sampleRate;  // window centre
        const auto n = static_cast<std::ptrdiff_t>(clip.samples.size());
        std::vector<double> window(static_cast<std::size_t>(fft));
        for (int i = 0; i < fft; ++i)
            window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (fft - 1));
        const std::size_t bins = static_cast<std::size_t>(fft / 2 + 1);
        std::vector<double> previous(bins, 0.0), current(bins, 0.0);
        std::vector<std::complex<double>> buffer(static_cast<std::size_t>(fft));
        bool first = true;
        for (std::ptrdiff_t start = 0; start < n; start += hop)
        {
            for (int i = 0; i < fft; ++i)
            {
                const auto at = start + i;
                const double s = at < n ? clip.samples[static_cast<std::size_t>(at)] : 0.0;
                buffer[static_cast<std::size_t>(i)] = {s * window[static_cast<std::size_t>(i)], 0.0};
            }
            Fft(buffer);
            double flux = 0.0;
            for (std::size_t k = 0; k < bins; ++k)
            {
                const double mag = std::abs(buffer[k]) / (fft / 2.0);
                current[k] = std::log1p(100.0 * mag);
                if (!first)
                    flux += std::max(0.0, current[k] - previous[k]);
            }
            first = false;
            std::swap(previous, current);
            e.strength.push_back(flux);
        }
        const double peak = e.strength.empty() ? 0.0 : *std::max_element(e.strength.begin(), e.strength.end());
        if (peak > 0.0)
            for (auto& v : e.strength)
                v /= peak;
        return e;
    }

    std::vector<Onset> DetectOnsets(const OnsetEnvelope& e, const OnsetSettings& settings)
    {
        std::vector<Onset> onsets;
        const int n = static_cast<int>(e.strength.size());
        if (n < 3 || e.secondsPerFrame <= 0.0)
            return onsets;
        const int half = std::max(1, static_cast<int>(std::lround(settings.medianWindowSeconds / 2.0 / e.secondsPerFrame)));
        const int spacing = std::max(1, static_cast<int>(std::lround(settings.minSpacingSeconds / e.secondsPerFrame)));
        for (int i = 1; i + 1 < n; ++i)
        {
            const double v = e.strength[static_cast<std::size_t>(i)];
            if (v < e.strength[static_cast<std::size_t>(i - 1)] || v <= e.strength[static_cast<std::size_t>(i + 1)])
                continue;  // not a peak (plateaus count once, at their start)
            std::vector<double> around(e.strength.begin() + std::max(0, i - half),
                                       e.strength.begin() + std::min(n, i + half + 1));
            if (v < Median(std::move(around)) + settings.thresholdMargin)
                continue;
            const double t = FrameTime(e, i);
            if (!onsets.empty() && i - static_cast<int>(std::lround((onsets.back().time - e.firstFrameSeconds) /
                                                                      e.secondsPerFrame)) < spacing)
            {
                if (v > onsets.back().strength)
                    onsets.back() = {t, v};  // the stronger of two close peaks
                continue;
            }
            onsets.push_back({t, v});
        }
        return onsets;
    }

    TempoEstimate EstimateTempo(const OnsetEnvelope& e, double minBpm, double maxBpm)
    {
        TempoEstimate t;
        const auto n = e.strength.size();
        if (n < 16 || e.secondsPerFrame <= 0.0 || !(maxBpm > minBpm) || minBpm <= 0.0)
            return t;
        // Mean-removed, lightly smoothed envelope.
        auto x = Smooth(e.strength, 1.0);
        const double mean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
        for (auto& v : x)
            v -= mean;
        const auto lagMin = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(60.0 / maxBpm / e.secondsPerFrame)));
        const auto lagMax = std::min<std::size_t>(n / 2, static_cast<std::size_t>(std::ceil(60.0 / minBpm / e.secondsPerFrame)));
        if (lagMax <= lagMin + 2)
            return t;
        // Autocorrelation, normalised by the energy (lag 0): 1 for a
        // perfectly periodic envelope, about 0 for noise.
        double energy = 0.0;
        for (const double v : x)
            energy += v * v;
        energy /= static_cast<double>(n);
        if (energy <= 0.0)
            return t;
        std::vector<double> acf(lagMax + 2, 0.0), score(lagMax + 2, 0.0);
        for (std::size_t lag = lagMin; lag <= lagMax + 1 && lag < n; ++lag)
        {
            double acc = 0.0;
            for (std::size_t i = 0; i + lag < n; ++i)
                acc += x[i] * x[i + lag];
            acf[lag] = acc / static_cast<double>(n - lag) / energy;
            const double bpm = 60.0 / (static_cast<double>(lag) * e.secondsPerFrame);
            const double octaves = std::log2(bpm / 120.0);
            score[lag] = std::max(0.0, acf[lag]) * std::exp(-0.5 * octaves * octaves);
        }
        std::size_t best = lagMin;
        for (std::size_t lag = lagMin; lag <= lagMax; ++lag)
            if (score[lag] > score[best])
                best = lag;
        if (score[best] <= 0.0)
            return t;
        // Parabolic refinement of the peak.
        double lag = static_cast<double>(best);
        if (best > lagMin && best < lagMax)
        {
            const double a = score[best - 1], b = score[best], c = score[best + 1];
            const double d = a - 2.0 * b + c;
            if (d < 0.0)
                lag += std::clamp(0.5 * (a - c) / d, -0.5, 0.5);
        }
        t.bpm = 60.0 / (lag * e.secondsPerFrame);
        t.confidence = std::clamp(acf[best], 0.0, 1.0);
        return t;
    }

    std::vector<double> TrackBeats(const OnsetEnvelope& e, double bpm, double tightness)
    {
        std::vector<double> beats;
        const int n = static_cast<int>(e.strength.size());
        if (n < 4 || !(bpm > 0.0) || e.secondsPerFrame <= 0.0)
            return beats;
        const double period = 60.0 / bpm / e.secondsPerFrame;  // frames
        if (period < 2.0)
            return beats;
        // Local score: the envelope smoothed at the scale of the beat.
        const auto local = Smooth(e.strength, period / 32.0);
        std::vector<double> score(static_cast<std::size_t>(n), 0.0);
        std::vector<int> back(static_cast<std::size_t>(n), -1);
        const int from = static_cast<int>(std::lround(period / 2.0)), to = static_cast<int>(std::lround(2.0 * period));
        for (int i = 0; i < n; ++i)
        {
            double bestPrev = 0.0;
            int bestIndex = -1;
            for (int d = from; d <= to && i - d >= 0; ++d)
            {
                const double penalty = std::log(d / period);
                const double v = score[static_cast<std::size_t>(i - d)] - tightness * penalty * penalty;
                if (bestIndex < 0 || v > bestPrev)
                {
                    bestPrev = v;
                    bestIndex = i - d;
                }
            }
            score[static_cast<std::size_t>(i)] = local[static_cast<std::size_t>(i)] + (bestIndex >= 0 ? std::max(0.0, bestPrev) : 0.0);
            back[static_cast<std::size_t>(i)] = bestIndex >= 0 && bestPrev > 0.0 ? bestIndex : -1;
        }
        // The last beat: the best score within the final period.
        int last = n - 1;
        for (int i = std::max(0, n - static_cast<int>(std::ceil(period))); i < n; ++i)
            if (score[static_cast<std::size_t>(i)] > score[static_cast<std::size_t>(last)])
                last = i;
        std::vector<int> frames;
        for (int i = last; i >= 0; i = back[static_cast<std::size_t>(i)])
            frames.push_back(i);
        std::reverse(frames.begin(), frames.end());
        // Trim beats in silence at the start and the end.
        double rms = 0.0;
        for (const int f : frames)
            rms += local[static_cast<std::size_t>(f)] * local[static_cast<std::size_t>(f)];
        rms = frames.empty() ? 0.0 : std::sqrt(rms / static_cast<double>(frames.size()));
        const double floor = 0.5 * rms;
        std::size_t a = 0, b = frames.size();
        while (a < b && local[static_cast<std::size_t>(frames[a])] < floor)
            ++a;
        while (b > a && local[static_cast<std::size_t>(frames[b - 1])] < floor)
            --b;
        for (std::size_t i = a; i < b; ++i)
            beats.push_back(FrameTime(e, frames[i]));
        return beats;
    }

    AudioAnalysis AnalyseAudio(const AudioClip& clip, const OnsetSettings& settings)
    {
        AudioAnalysis a;
        const auto envelope = ComputeOnsetEnvelope(clip, settings);
        a.onsets = DetectOnsets(envelope, settings);
        a.tempo = EstimateTempo(envelope);
        if (a.tempo.bpm > 0.0)
            a.beats = TrackBeats(envelope, a.tempo.bpm);
        return a;
    }
}  // namespace iris
