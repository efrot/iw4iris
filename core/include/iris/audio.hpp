// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Audio for montages: decoding WAV, waveform peaks, transient (onset)
// detection, tempo estimation and beat tracking, all local and
// deterministic (no cloud service, no model).
//
//   * Onsets: spectral flux of a short-time Fourier transform (Hann window,
//     log-compressed magnitudes, half-wave rectified frame differences),
//     peaks above an adaptive threshold (local median plus a margin).
//   * Tempo: autocorrelation of the onset-strength envelope over 60 to 200
//     BPM, weighted towards common tempi (a log-normal prior around 120 BPM,
//     as described in the beat-tracking literature), so half / double tempo
//     errors are less likely; the confidence says how clear the peak was.
//   * Beats: dynamic-programming beat tracking (D. P. W. Ellis, "Beat
//     Tracking by Dynamic Programming", J. New Music Research 36(1), 2007):
//     beats on strong onsets, spaced close to the tempo, following small
//     tempo changes. Implemented from the paper.
//
// Detected beats are suggestions: the editor shows them, the user corrects
// them, and what is used is stored in the project.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iris
{
    struct AudioClip
    {
        int sampleRate = 0;
        int channels = 0;           // of the source; `samples` is mixed down to mono
        std::vector<float> samples; // mono, -1..1
        double Duration() const { return sampleRate > 0 ? static_cast<double>(samples.size()) / sampleRate : 0.0; }
    };

    // RIFF / WAVE: PCM 8 / 16 / 24 / 32 bit and IEEE float 32 / 64, any
    // channel count (WAVE_FORMAT_EXTENSIBLE included), mixed to mono.
    std::optional<AudioClip> DecodeWav(const std::vector<unsigned char>& bytes, std::string* error = nullptr);
    // 16-bit PCM WAV of interleaved samples (for the audio of a recording).
    std::vector<unsigned char> EncodeWav16(const std::vector<float>& interleaved, int sampleRate, int channels);
    // A clip from raw signed 16-bit little-endian mono samples (FFmpeg's
    // "-f s16le -ac 1" output).
    AudioClip ClipFromS16(const std::vector<unsigned char>& raw, int sampleRate);

    // Waveform for display: min / max per bucket.
    struct WaveformPeaks
    {
        double secondsPerBucket = 0.0;
        std::vector<float> low, high;
    };
    WaveformPeaks ComputePeaks(const AudioClip& clip, double secondsPerBucket);

    struct OnsetSettings
    {
        int fftSize = 1024;
        int hop = 512;
        double thresholdMargin = 0.15;  // above the local median (of the normalised flux)
        double medianWindowSeconds = 0.5;
        double minSpacingSeconds = 0.08;
    };

    // The onset-strength envelope: one value per hop, normalised to 0..1.
    struct OnsetEnvelope
    {
        double secondsPerFrame = 0.0;
        double firstFrameSeconds = 0.0;  // time of frame 0 (the centre of its window)
        std::vector<double> strength;
    };
    OnsetEnvelope ComputeOnsetEnvelope(const AudioClip& clip, const OnsetSettings& settings = {});

    struct Onset
    {
        double time = 0.0;      // seconds into the clip
        double strength = 0.0;  // 0..1
    };
    std::vector<Onset> DetectOnsets(const OnsetEnvelope& envelope, const OnsetSettings& settings = {});

    struct TempoEstimate
    {
        double bpm = 0.0;        // 0: none found
        double confidence = 0.0; // 0..1: how clearly one tempo stands out
    };
    TempoEstimate EstimateTempo(const OnsetEnvelope& envelope, double minBpm = 60.0, double maxBpm = 200.0);

    // Beat times (seconds into the clip) tracked at about `bpm`.
    // `tightness` weighs regular spacing against following onsets.
    std::vector<double> TrackBeats(const OnsetEnvelope& envelope, double bpm, double tightness = 100.0);

    struct AudioAnalysis
    {
        TempoEstimate tempo;
        std::vector<double> beats;
        std::vector<Onset> onsets;
    };
    AudioAnalysis AnalyseAudio(const AudioClip& clip, const OnsetSettings& settings = {});
}  // namespace iris
