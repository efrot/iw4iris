// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Audio.hpp"
#include "Log.hpp"

#include <iris/recording.hpp>

#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

namespace IW4Iris::Audio
{
  namespace
  {
    constexpr int kDecodeRate = 44100;  // FFmpeg decodes to this rate

    std::mutex mutex;
    Status status;
    std::string requested;                         // the path a load was started for
    std::shared_ptr<const iris::AudioClip> clip;   // the loaded clip
    std::shared_ptr<const iris::WaveformPeaks> peaks;
    std::optional<iris::AudioAnalysis> analysis;   // a finished analysis, not taken yet
    std::uint64_t loadGeneration = 0;

    std::string Utf8(const std::filesystem::path& p)
    {
      const auto u = p.u8string();
      return std::string(u.begin(), u.end());
    }

    // Runs FFmpeg and returns what it writes to stdout (raw mono s16le).
    bool DecodeWithFfmpeg(const std::string& exe, const std::filesystem::path& file, std::vector<unsigned char>& out,
                          std::string& error)
    {
      SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
      HANDLE readEnd = nullptr, writeEnd = nullptr;
      if (!CreatePipe(&readEnd, &writeEnd, &sa, 1 << 20))
      {
        error = "CreatePipe failed";
        return false;
      }
      SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
      HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_HIDE;
      si.hStdInput = nullptr;
      si.hStdOutput = writeEnd;
      si.hStdError = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
      const std::vector<std::string> args{"-v", "error", "-nostdin", "-i", Utf8(file), "-vn", "-f", "s16le",
                                          "-ac", "1", "-ar", std::to_string(kDecodeRate), "pipe:1"};
      const std::string command = iris::JoinWindowsCommandLine(exe, args);
      std::wstring wide(command.size() + 1, L'\0');
      const int n = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
      PROCESS_INFORMATION pi{};
      const BOOL created = n > 0 && CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                                   nullptr, nullptr, &si, &pi);
      CloseHandle(writeEnd);
      if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
      if (!created)
      {
        CloseHandle(readEnd);
        error = "Could not start FFmpeg (" + exe + ")";
        return false;
      }
      std::vector<unsigned char> chunk(1 << 16);
      for (;;)
      {
        DWORD got = 0;
        if (!ReadFile(readEnd, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr) || got == 0)
          break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + got);
        if (out.size() > (std::size_t{1} << 30))
          break;  // an hour of audio is plenty for a montage
      }
      CloseHandle(readEnd);
      WaitForSingleObject(pi.hProcess, 10000);
      DWORD code = 1;
      GetExitCodeProcess(pi.hProcess, &code);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      if (code != 0 || out.empty())
      {
        error = "FFmpeg could not read the file (exit code " + std::to_string(code) + ")";
        return false;
      }
      return true;
    }

    void LoadAsync(const std::filesystem::path& file, const std::string& ffmpeg, std::uint64_t generation)
    {
      std::thread([file, ffmpeg, generation] {
        std::string error;
        std::optional<iris::AudioClip> decoded;
        bool viaFfmpeg = false;
        std::string ext = file.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".wav" || ext == ".wave")
        {
          std::ifstream in(file, std::ios::binary);
          const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
          if (bytes.empty())
            error = "Cannot read " + Utf8(file);
          else
            decoded = iris::DecodeWav(bytes, &error);
        }
        if (!decoded && !ffmpeg.empty())
        {
          std::vector<unsigned char> raw;
          std::string ffError;
          if (DecodeWithFfmpeg(ffmpeg, file, raw, ffError))
          {
            decoded = iris::ClipFromS16(raw, kDecodeRate);
            viaFfmpeg = true;
            error.clear();
          }
          else if (error.empty())
            error = ffError;
        }
        else if (!decoded && error.empty())
          error = "Only WAV files can be read without FFmpeg. Set FFmpeg's path (Recording panel) for MP3, FLAC, OGG "
                  "and other formats.";

        std::shared_ptr<const iris::AudioClip> loaded;
        std::shared_ptr<const iris::WaveformPeaks> wave;
        if (decoded)
        {
          wave = std::make_shared<iris::WaveformPeaks>(iris::ComputePeaks(*decoded, 0.01));
          loaded = std::make_shared<iris::AudioClip>(std::move(*decoded));
        }
        std::lock_guard _(mutex);
        if (generation != loadGeneration)
          return;  // another file was asked for meanwhile
        status.loading = false;
        status.error = error;
        status.viaFfmpeg = viaFfmpeg;
        if (loaded)
        {
          clip = loaded;
          peaks = wave;
          status.loadedPath = Utf8(file);
          status.duration = loaded->Duration();
          status.sampleRate = loaded->sampleRate;
          status.channels = loaded->channels;
          Log::Info("Music loaded: {} ({:.1f} s{})", Utf8(file), status.duration, viaFfmpeg ? ", via FFmpeg" : "");
        }
        else
          Log::Warn("Music: {}", error);
      }).detach();
    }

    // ------------------------------------------------------------ playback

    class Player
    {
    public:
      ~Player() { Stop(); }

      bool Running() const { return out_ != nullptr; }

      void Start(std::shared_ptr<const iris::AudioClip> source, double seconds, double volume)
      {
        Stop();
        if (!source || source->sampleRate <= 0 || source->samples.empty())
          return;
        clip_ = std::move(source);
        start_ = std::clamp<std::int64_t>(std::llround(seconds * clip_->sampleRate), 0,
                                          static_cast<std::int64_t>(clip_->samples.size()));
        cursor_ = start_;
        volume_ = static_cast<float>(volume);
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = static_cast<DWORD>(clip_->sampleRate);
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * 2;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (waveOutOpen(&out_, WAVE_MAPPER, &fmt, reinterpret_cast<DWORD_PTR>(event_), 0, CALLBACK_EVENT) !=
            MMSYSERR_NOERROR)
        {
          out_ = nullptr;
          CloseHandle(event_);
          event_ = nullptr;
          if (!warned_)
            Log::Warn("Music preview: no audio output device could be opened");
          warned_ = true;
          return;
        }
        for (auto& b : buffers_)
        {
          b.data.assign(kBlock, 0);
          b.header = {};
          b.header.lpData = reinterpret_cast<LPSTR>(b.data.data());
          b.header.dwBufferLength = static_cast<DWORD>(b.data.size() * sizeof(std::int16_t));
          b.header.dwFlags = WHDR_DONE;  // free to fill
          waveOutPrepareHeader(out_, &b.header, sizeof(WAVEHDR));
          b.header.dwFlags |= WHDR_DONE;
        }
        running_ = true;
        feeder_ = std::thread([this] { Feed(); });
      }

      void Stop()
      {
        if (!out_)
          return;
        running_ = false;
        if (event_)
          SetEvent(event_);
        if (feeder_.joinable())
          feeder_.join();
        waveOutReset(out_);
        for (auto& b : buffers_)
          waveOutUnprepareHeader(out_, &b.header, sizeof(WAVEHDR));
        waveOutClose(out_);
        out_ = nullptr;
        CloseHandle(event_);
        event_ = nullptr;
        clip_.reset();
      }

      // Seconds into the file being heard now.
      double Position() const
      {
        if (!out_ || !clip_)
          return 0.0;
        MMTIME t{};
        t.wType = TIME_SAMPLES;
        if (waveOutGetPosition(out_, &t, sizeof(t)) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES)
          return static_cast<double>(start_) / clip_->sampleRate;
        return static_cast<double>(start_ + t.u.sample) / clip_->sampleRate;
      }

      void SetVolume(double v) { volume_ = static_cast<float>(std::clamp(v, 0.0, 4.0)); }

    private:
      static constexpr std::size_t kBlock = 2048;
      struct Buffer
      {
        std::vector<std::int16_t> data;
        WAVEHDR header{};
      };

      void Feed()
      {
        while (running_)
        {
          for (auto& b : buffers_)
          {
            if (!(b.header.dwFlags & WHDR_DONE))
              continue;
            const auto& s = clip_->samples;
            const float v = volume_;
            for (std::size_t i = 0; i < kBlock; ++i)
            {
              const auto at = static_cast<std::size_t>(cursor_) + i;
              const float x = at < s.size() ? s[at] * v : 0.0f;
              b.data[i] = static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
            }
            cursor_ += static_cast<std::int64_t>(kBlock);
            b.header.dwFlags &= ~static_cast<DWORD>(WHDR_DONE);
            waveOutWrite(out_, &b.header, sizeof(WAVEHDR));
          }
          WaitForSingleObject(event_, 30);
        }
      }

      std::shared_ptr<const iris::AudioClip> clip_;
      HWAVEOUT out_ = nullptr;
      HANDLE event_ = nullptr;
      std::thread feeder_;
      std::atomic<bool> running_{false};
      std::atomic<float> volume_{1.0f};
      std::int64_t start_ = 0, cursor_ = 0;
      Buffer buffers_[4];
      bool warned_ = false;
    };

    Player player;
  }  // namespace

  Status GetStatus()
  {
    std::lock_guard _(mutex);
    return status;
  }

  void Request(const std::filesystem::path& file, const std::function<std::string()>& ffmpeg)
  {
    {
      std::lock_guard _(mutex);
      if (requested == Utf8(file))
        return;
    }
    Reload(file, ffmpeg);
  }

  void Reload(const std::filesystem::path& file, const std::function<std::string()>& ffmpeg)
  {
    player.Stop();  // the previous file's preview
    std::uint64_t generation = 0;
    {
      std::lock_guard _(mutex);
      requested = Utf8(file);
      status.loading = true;
      status.error.clear();
      generation = ++loadGeneration;
    }
    // FFmpeg is looked up here, on the calling (main) thread.
    LoadAsync(file, ffmpeg ? ffmpeg() : std::string(), generation);
  }

  void Unload()
  {
    player.Stop();
    std::lock_guard _(mutex);
    ++loadGeneration;
    requested.clear();
    clip.reset();
    peaks.reset();
    analysis.reset();
    status = {};
  }

  std::shared_ptr<const iris::WaveformPeaks> Peaks()
  {
    std::lock_guard _(mutex);
    return peaks;
  }

  void Analyse()
  {
    std::shared_ptr<const iris::AudioClip> source;
    std::uint64_t generation = 0;
    {
      std::lock_guard _(mutex);
      if (!clip || status.analysing)
        return;
      status.analysing = true;
      source = clip;
      generation = loadGeneration;
    }
    std::thread([source, generation] {
      auto result = iris::AnalyseAudio(*source);
      std::lock_guard _(mutex);
      status.analysing = false;
      if (generation != loadGeneration)
        return;
      Log::Info("Music analysed: {:.1f} BPM (confidence {:.0f}%), {} beats, {} onsets", result.tempo.bpm,
                result.tempo.confidence * 100.0, result.beats.size(), result.onsets.size());
      analysis = std::move(result);
    }).detach();
  }

  std::optional<iris::AudioAnalysis> TakeAnalysis()
  {
    std::lock_guard _(mutex);
    auto a = std::move(analysis);
    analysis.reset();
    return a;
  }

  void Sync(bool play, double fileSeconds, double volume)
  {
    std::shared_ptr<const iris::AudioClip> source;
    {
      std::lock_guard _(mutex);
      source = clip;
    }
    if (!play || !source || fileSeconds < 0.0 || fileSeconds >= source->Duration())
    {
      player.Stop();
      return;
    }
    if (!player.Running())
    {
      player.Start(source, fileSeconds, volume);
      return;
    }
    player.SetVolume(volume);
    // Follow the playhead: restart when the music drifted from it (a seek,
    // a stall of the game, a slow frame).
    if (std::abs(player.Position() - fileSeconds) > 0.12)
      player.Start(source, fileSeconds, volume);
  }

  void Shutdown()
  {
    player.Stop();
  }
}
