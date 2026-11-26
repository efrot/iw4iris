// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The montage's music in the editor: loading (WAV is read directly; MP3,
// FLAC, OGG and anything else through FFmpeg), the waveform, the beat
// analysis (in the background) and a preview that plays along with the
// playhead through the Windows waveOut API.
//
// The preview is a mono mix at the file's rate, for timing; recordings use
// the original file through FFmpeg.
#pragma once

#include <iris/audio.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace IW4Iris::Audio
{
  struct Status
  {
    bool loading = false;
    bool analysing = false;
    std::string loadedPath;   // the file the clip below comes from
    std::string error;        // of the last load
    double duration = 0.0;
    int sampleRate = 0;
    int channels = 0;
    bool viaFfmpeg = false;
  };
  Status GetStatus();

  // Loads a file in the background (once per path; Reload forces it).
  // `ffmpeg` finds FFmpeg (empty: none) when the file is not a WAV.
  void Request(const std::filesystem::path& file, const std::function<std::string()>& ffmpeg);
  void Reload(const std::filesystem::path& file, const std::function<std::string()>& ffmpeg);
  void Unload();

  // Waveform of the loaded clip (10 ms buckets), or none.
  std::shared_ptr<const iris::WaveformPeaks> Peaks();

  // Beat analysis of the loaded clip in the background; the result is taken
  // once.
  void Analyse();
  std::optional<iris::AudioAnalysis> TakeAnalysis();

  // Main thread, once per frame: plays from `fileSeconds` while `play`,
  // following the position (it restarts when it drifts), else stops.
  void Sync(bool play, double fileSeconds, double volume);
  void Shutdown();
}
