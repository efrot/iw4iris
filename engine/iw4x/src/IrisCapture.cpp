// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Capture.hpp"
#include "Clock.hpp"
#include "DemoControl.hpp"
#include "Director.hpp"
#include "Graphics.hpp"
#include "Engine.hpp"
#include "Log.hpp"
#include "Session.hpp"

#include <iris/beatsync.hpp>
#include <iris/preflight.hpp>
#include <iris/version.hpp>

#include <algorithm>
#include <set>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <optional>
#include <deque>
#include <thread>

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

namespace IW4Iris::Capture
{
  namespace
  {
    // ------------------------------------------------------------ frames

    struct Frame
    {
      std::int64_t number = 0;  // output frame number
      int width = 0;
      int height = 0;
      std::vector<std::uint8_t> bgra;
    };

    class FrameQueue
    {
    public:
      explicit FrameQueue(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

      // Blocks while full (backpressure). False when closed.
      bool Push(Frame&& f)
      {
        std::unique_lock lock(m_);
        notFull_.wait(lock, [&] { return closed_ || q_.size() < capacity_; });
        if (closed_)
          return false;
        q_.push_back(std::move(f));
        notEmpty_.notify_one();
        return true;
      }

      std::optional<Frame> Pop()
      {
        std::unique_lock lock(m_);
        notEmpty_.wait(lock, [&] { return closed_ || !q_.empty(); });
        if (q_.empty())
          return std::nullopt;
        Frame f = std::move(q_.front());
        q_.pop_front();
        notFull_.notify_one();
        return f;
      }

      void Close()
      {
        std::lock_guard _(m_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
      }

    private:
      std::mutex m_;
      std::condition_variable notFull_, notEmpty_;
      std::deque<Frame> q_;
      std::size_t capacity_;
      bool closed_ = false;
    };

    // ------------------------------------------------------------ encoders

    class Encoder
    {
    public:
      virtual ~Encoder() = default;
      virtual bool Write(const Frame& frame, std::string* error) = 0;
      virtual bool Finish(std::string* error) = 0;
      virtual bool Concurrent() const { return false; }
    };

    std::string Utf8(const std::filesystem::path& p)
    {
      const auto u8 = p.u8string();
      return {reinterpret_cast<const char*>(u8.data()), u8.size()};
    }

    void BgraToRgb(const Frame& f, std::vector<std::uint8_t>& rgb)
    {
      const std::size_t pixels = static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height);
      rgb.resize(pixels * 3);
      for (std::size_t i = 0; i < pixels; ++i)
      {
        rgb[i * 3 + 0] = f.bgra[i * 4 + 2];
        rgb[i * 3 + 1] = f.bgra[i * 4 + 1];
        rgb[i * 3 + 2] = f.bgra[i * 4 + 0];
      }
    }

    // Built-in PNG / JPEG writer (stb_image_write); frames are independent
    // files, so several workers encode at once.
    class ImageSequenceEncoder final : public Encoder
    {
    public:
      ImageSequenceEncoder(std::filesystem::path directory, std::string base, bool jpeg, int quality)
        : directory_(std::move(directory)), base_(std::move(base)), jpeg_(jpeg), quality_(quality) {}

      bool Concurrent() const override { return true; }

      bool Write(const Frame& f, std::string* error) override
      {
        thread_local std::vector<std::uint8_t> rgb;
        BgraToRgb(f, rgb);
        char name[64];
        std::snprintf(name, sizeof(name), "_%06lld%s", static_cast<long long>(f.number), jpeg_ ? ".jpg" : ".png");
        const auto path = Utf8(directory_ / (base_ + name));
        const int ok = jpeg_ ? stbi_write_jpg(path.c_str(), f.width, f.height, 3, rgb.data(), quality_)
                             : stbi_write_png(path.c_str(), f.width, f.height, 3, rgb.data(), f.width * 3);
        if (!ok && error)
          *error = "Could not write " + path + " (disk full or no permission?)";
        return ok != 0;
      }

      bool Finish(std::string*) override { return true; }

    private:
      std::filesystem::path directory_;
      std::string base_;
      bool jpeg_;
      int quality_;
    };

    // Raw BGRA frames into FFmpeg's stdin.
    class FfmpegEncoder final : public Encoder
    {
    public:
      bool Open(const std::string& exe, const std::vector<std::string>& args, const std::filesystem::path& logFile,
                std::string* error)
      {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE readEnd = nullptr;
        if (!CreatePipe(&readEnd, &stdin_, &sa, 16 * 1024 * 1024))
        {
          *error = "CreatePipe failed";
          return false;
        }
        SetHandleInformation(stdin_, HANDLE_FLAG_INHERIT, 0);
        log_ = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        logFile_ = logFile;

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = readEnd;
        si.hStdOutput = log_ != INVALID_HANDLE_VALUE ? log_ : nullptr;
        si.hStdError = log_ != INVALID_HANDLE_VALUE ? log_ : nullptr;
        std::string commandLine = iris::JoinWindowsCommandLine(exe, args);
        Log::Info("FFmpeg: {}", commandLine);
        const BOOL created = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi_);
        CloseHandle(readEnd);
        if (!created)
        {
          *error = "Could not start FFmpeg (" + exe + "), error " + std::to_string(GetLastError());
          CloseHandle(stdin_);
          stdin_ = nullptr;
          return false;
        }
        return true;
      }

      ~FfmpegEncoder() override
      {
        if (stdin_)
          CloseHandle(stdin_);
        if (pi_.hProcess)
        {
          if (WaitForSingleObject(pi_.hProcess, 0) == WAIT_TIMEOUT)
            TerminateProcess(pi_.hProcess, 1);
          CloseHandle(pi_.hProcess);
          CloseHandle(pi_.hThread);
        }
        if (log_ && log_ != INVALID_HANDLE_VALUE)
          CloseHandle(log_);
      }

      bool Write(const Frame& f, std::string* error) override
      {
        const auto* data = f.bgra.data();
        std::size_t left = f.bgra.size();
        while (left > 0)
        {
          DWORD written = 0;
          const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(left, 1u << 24));
          if (!WriteFile(stdin_, data, chunk, &written, nullptr) || written == 0)
          {
            *error = "FFmpeg stopped accepting frames: " + LogTail();
            return false;
          }
          data += written;
          left -= written;
        }
        return true;
      }

      bool Finish(std::string* error) override
      {
        CloseHandle(stdin_);
        stdin_ = nullptr;
        WaitForSingleObject(pi_.hProcess, 10 * 60 * 1000);
        DWORD code = 1;
        GetExitCodeProcess(pi_.hProcess, &code);
        if (code != 0)
        {
          *error = "FFmpeg failed (exit code " + std::to_string(code) + "): " + LogTail();
          return false;
        }
        return true;
      }

    private:
      std::string LogTail()
      {
        if (log_ && log_ != INVALID_HANDLE_VALUE)
          FlushFileBuffers(log_);
        std::ifstream in(logFile_, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.size() > 600)
          text = "..." + text.substr(text.size() - 600);
        return text.empty() ? std::string("(no output, see ") + logFile_.string() + ")" : text;
      }

      HANDLE stdin_ = nullptr;
      HANDLE log_ = nullptr;
      PROCESS_INFORMATION pi_{};
      std::filesystem::path logFile_;
    };

    // ------------------------------------------------------------ state

    std::mutex mutex;  // guards everything below
    std::atomic<State> state{State::Idle};
    Progress progress;
    iris::RecordingSettings settings;
    iris::FrameSteps steps;
    iris::FrameRange range;
    std::int64_t warmup = 0;
    std::int64_t mainFrame = 0;           // capture frames issued by the main thread
    std::atomic<std::int64_t> backendFrame{0};
    std::atomic<int> latency{-1};         // backend frame b shows main frame b - latency
    std::atomic<std::int64_t> captured{0};
    std::atomic<std::int64_t> encoded{0};
    std::atomic<bool> encoderFailed{false};
    std::string encoderError;
    std::unique_ptr<FrameQueue> queue;
    std::unique_ptr<Encoder> encoder;
    std::vector<std::thread> workers;
    std::chrono::steady_clock::time_point startTime;
    std::filesystem::path outputDirectory;
    std::string baseName;
    double seekTarget = 0.0;
    int seekWaitFrames = 0;
    int seekRetries = 0;
    int demoBase = 0;                     // client server time of scene time 0
    std::vector<std::int64_t> cumulative; // scene ms of capture frame k after the first
    std::int64_t renderFrame = -1;        // capture frame of the last render
    std::optional<std::int64_t> sceneBase;
    std::atomic<std::int64_t> unaligned{0};
    std::atomic<std::int64_t> tagHits{0};

    // Held by the render thread for a whole BackendFrame, and by Finish()
    // before it tears the queue and encoder down.
    std::mutex backendMutex;

    // Render-thread surfaces
    IDirect3DSurface9* resolveSurface = nullptr;
    IDirect3DSurface9* systemSurface = nullptr;
    D3DSURFACE_DESC surfaceDesc{};

    void ReleaseSurfaces()
    {
      if (resolveSurface)
        resolveSurface->Release();
      if (systemSurface)
        systemSurface->Release();
      resolveSurface = nullptr;
      systemSurface = nullptr;
      surfaceDesc = {};
    }

    void SetMessage(std::string m)
    {
      std::lock_guard _(mutex);
      progress.message = std::move(m);
    }

    void StopWorkers()
    {
      if (queue)
        queue->Close();
      for (auto& w : workers)
        if (w.joinable())
          w.join();
      workers.clear();
    }

    void RestoreGame()
    {
      Clock::End();
      Engine::Restore("com_maxfps");
      Engine::Restore("timescale");
    }

    void WriteReport(const char* outcome)
    {
      nlohmann::json report{{"generator", std::string(iris::kVersionString)},
                            {"outcome", outcome},
                            {"format", std::string(iris::ToString(settings.format))},
                            {"fps", {settings.frameRate.numerator, settings.frameRate.denominator}},
                            {"firstFrame", range.first},
                            {"frames", range.count},
                            {"framesCaptured", captured.load()},
                            {"framesEncoded", encoded.load()},
                            {"latencyFrames", latency.load()},
                            {"syncVerified", tagHits > 0},
                            {"unalignedFrames", unaligned.load()},
                            {"clockDriftMs", Clock::WatchdogDriftMs()},
                            {"message", progress.message}};
      std::ofstream(outputDirectory / (baseName + ".capture.json")) << report.dump(2);
    }

    void Finish(State final, std::string message)
    {
      // Stop the render thread from taking more frames, and wait for a frame
      // it may be reading or queueing right now.
      state = State::Finishing;
      {
        std::lock_guard wait(backendMutex);
      }
      RestoreGame();
      StopWorkers();
      std::string error;
      if (encoder && final == State::Done && !encoder->Finish(&error))
      {
        final = State::Failed;
        message = error;
      }
      encoder.reset();
      queue.reset();
      {
        std::lock_guard _(mutex);
        progress.state = final;
        progress.message = message;
        progress.framesCaptured = captured;
        progress.framesEncoded = encoded;
      }
      state = final;
      WriteReport(ToString(final));
      if (final == State::Done)
        Log::Info("Capture finished: {} frames in {}", captured.load(), outputDirectory.string());
      else
        Log::Error("Capture {}: {}", ToString(final), message);
    }

    // 2 bits per channel, 64 values; drawn as solid blocks so scaling and
    // filtering do not change them.
    constexpr int kTagSize = 8;
    constexpr float kLevels[4] = {0.0f, 85.0f / 255.0f, 170.0f / 255.0f, 1.0f};

    int DecodeLevel(std::uint8_t v)
    {
      int best = 0, bestDist = 1000;
      for (int i = 0; i < 4; ++i)
      {
        const int d = std::abs(static_cast<int>(v) - static_cast<int>(kLevels[i] * 255.0f + 0.5f));
        if (d < bestDist)
        {
          bestDist = d;
          best = i;
        }
      }
      return bestDist < 30 ? best : -1;
    }
  }

  const char* ToString(State s)
  {
    switch (s)
    {
    case State::Idle: return "idle";
    case State::Seeking: return "seeking";
    case State::Warmup: return "warming up";
    case State::Recording: return "recording";
    case State::Finishing: return "finishing";
    case State::Done: return "done";
    case State::Failed: return "failed";
    case State::Cancelled: return "cancelled";
    }
    return "idle";
  }

  namespace
  {
    // Runs FFmpeg on its own (the music next to an image sequence); its
    // output goes to a log file.
    bool RunDetached(const std::string& exe, const std::vector<std::string>& args, const std::filesystem::path& logFile)
    {
      SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
      HANDLE log = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
      STARTUPINFOA si{};
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_HIDE;
      si.hStdOutput = log != INVALID_HANDLE_VALUE ? log : nullptr;
      si.hStdError = log != INVALID_HANDLE_VALUE ? log : nullptr;
      std::string commandLine = iris::JoinWindowsCommandLine(exe, args);
      Log::Info("FFmpeg (music): {}", commandLine);
      PROCESS_INFORMATION pi{};
      const BOOL created = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                          CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
      if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
      if (!created)
        return false;
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
    }

    // The music for a recording of `project` with `s`, from the frames'
    // own times (or the span the settings ask for).
    std::optional<iris::AudioInput> MusicFor(const iris::Project& project, const iris::RecordingSettings& s,
                                             std::int64_t firstFrame, std::int64_t frameCount)
    {
      if (!s.includeAudio || !project.audio)
        return std::nullopt;
      std::filesystem::path projectFile;
      {
        auto& session = GetSession();
        std::lock_guard _(session.mutex);
        projectFile = session.projectFile;
      }
      const auto path = iris::ResolveAudioPath(project, projectFile);
      if (!path)
        return std::nullopt;
      double from = s.frameRate.FrameToSeconds(firstFrame), to = s.frameRate.FrameToSeconds(firstFrame + frameCount);
      if (s.audioRangeStart >= 0.0 && s.audioRangeEnd > s.audioRangeStart)
      {
        from = s.audioRangeStart;
        to = s.audioRangeEnd;
      }
      return iris::AudioForRange(*project.audio, path->string(), from, to);
    }
  }  // namespace

  std::optional<std::string> FindFfmpeg(const std::string& configured)
  {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!configured.empty() && fs::is_regular_file(configured, ec))
      return configured;
    const auto base = Engine::BasePath();
    for (const auto& candidate : {fs::path(base) / "iw4iris" / "ffmpeg" / "ffmpeg.exe",
                                  fs::path(base) / "iw4iris" / "ffmpeg.exe", fs::path(base) / "ffmpeg.exe"})
      if (fs::is_regular_file(candidate, ec))
        return candidate.string();
    char found[MAX_PATH]{};
    if (SearchPathA(nullptr, "ffmpeg.exe", nullptr, MAX_PATH, found, nullptr))
      return std::string(found);
    return std::nullopt;
  }

  namespace
  {
    void AddSceneIssues(const iris::Project& p, std::vector<iris::Issue>& out)
    {
      using Sev = iris::Issue::Severity;
      const auto& sc = p.scene;
      const bool directorObjects = !sc.actors.empty() || !sc.props.empty() || !sc.effects.empty();
      if (directorObjects)
      {
        if (Engine::IsDemoPlaying())
          out.push_back({Sev::Warning, "Actors, props and effects exist only on Director Mode film sets; this demo "
                                       "recording will not show them."});
        else if (!Director::Ready())
          out.push_back({Sev::Error, "The film set is not ready, so actors and props would be missing. Wait for "
                                     "\"ready\" in the Director panel (or reload the set)."});
        else
        {
          const auto st = Director::GetStatus();
          if (!st.failures.empty())
            out.push_back({Sev::Error, std::to_string(st.failures.size()) + " scene object(s) failed in the game (" +
                                           st.failures.front() + "). Fix or delete them before recording."});
          if (!st.needReload.empty())
            out.push_back({Sev::Error, std::to_string(st.needReload.size()) +
                                           " asset(s) are not loaded yet. Click \"Reload set\" in the Director panel."});
          if (!st.unavailable.empty())
            out.push_back({Sev::Warning, std::to_string(st.unavailable.size()) +
                                             " asset(s) of this project are not on this map; those objects are left out."});
        }
      }
      const auto gs = Graphics::GetStatus();
      int needDepth = 0;
      for (const auto& g : sc.graphics)
        if (g.visible && g.kind != iris::GraphicKind::Anchor && g.renderMode != iris::GraphicRenderMode::OnTop)
          ++needDepth;
      if (needDepth > 0 && !gs.depthAvailable)
        out.push_back({Sev::Error, std::to_string(needDepth) +
                                       " graphic(s) are set to hide behind walls and characters, but occlusion is not "
                                       "available (" + (gs.depth.empty() ? std::string("not checked yet") : gs.depth) +
                                       "). They would be missing from the video. Set them to \"Always on top\" to render "
                                       "them without occlusion."});
      for (const auto& [id, message] : gs.errors)
        if (const auto* g = sc.FindGraphic(id); g && g->visible)
          out.push_back({Sev::Warning, "Graphic \"" + g->name + "\" has no geometry: " + message});
    }
  }  // namespace

  std::vector<iris::Issue> Check(const iris::RecordingSettings& s, const iris::Project* project)
  {
    auto& session = GetSession();
    std::string ffmpeg;
    {
      std::lock_guard _(session.mutex);
      ffmpeg = s.ffmpegPath.empty() ? session.settings.ffmpegPath : s.ffmpegPath;
    }
    auto issues = iris::ValidateRecording(s, Engine::RenderWidth(), Engine::RenderHeight(), FindFfmpeg(ffmpeg).has_value());
    if (!Clock::Installed())
      issues.push_back({iris::Issue::Severity::Error, "The render clock is unavailable in this game build."});
    if (project)
    {
      AddSceneIssues(*project, issues);
      // Render preflight: broken cuts, cameras without keys, cameras on
      // players this demo does not have, unusually fast camera moves.
      std::set<std::string> players;
      std::optional<std::string> loadedAngle;
      if (Engine::IsDemoPlaying())
      {
        auto& editorSession = GetSession();
        std::lock_guard sessionLock(editorSession.mutex);  // recursive: callers may hold it
        for (const auto& pl : editorSession.players.Players())
          players.insert(pl.name);
        loadedAngle = editorSession.loadedAngle;
      }
      const auto checkRange = iris::ResolveRange(s, project->timeline);
      if (s.includeAudio && project->audio && !project->audio->muted && !FindFfmpeg(ffmpeg))
        issues.push_back({iris::Issue::Severity::Warning,
                          "The music is not included: putting it in the video (or next to an image sequence) needs "
                          "FFmpeg."});
      // A recording shows the demo that plays: every cut in the range must
      // belong to it (another angle would record the wrong player's view).
      if (Engine::IsDemoPlaying() && !project->angles.empty())
      {
        std::set<std::string> reported;
        for (const auto& seg : iris::AngleSegments(*project, checkRange.start, checkRange.end))
          if ((!loadedAngle || seg.angle != *loadedAngle) && reported.insert(seg.angle).second)
          {
            const auto* a = project->FindAngle(seg.angle);
            const std::string name = seg.angle.empty() ? std::string("the main demo") : (a ? a->name : seg.angle);
            char at[32];
            std::snprintf(at, sizeof(at), "%.2f s", seg.start);
            issues.push_back({iris::Issue::Severity::Error,
                              "The range uses " + name + " (from " + at + "), but " +
                                  (loadedAngle ? std::string("another recording") : std::string("a demo that is not part of the project")) +
                                  " is loaded. A recording shows only the demo that plays: render a range with several "
                                  "angles with \"Queue per angle\" in the render queue (it opens each one in turn)."});
          }
      }
      for (auto& i : iris::PreflightProject(*project, checkRange.start, checkRange.end, players))
        issues.push_back(std::move(i));
    }
    return issues;
  }

  bool Start(const iris::Project& project, const iris::RecordingSettings& s, std::string* error)
  {
    if (IsActive())
    {
      *error = "A capture is already running.";
      return false;
    }
    const auto issues = Check(s, &project);
    for (const auto& i : issues)
      if (i.severity == iris::Issue::Severity::Error)
      {
        *error = i.message;
        return false;
      }
    if (!Engine::IsCgameReady())
    {
      *error = "Load a demo or a map first.";
      return false;
    }
    if (!Clock::Verify())
    {
      *error = "The game does not follow the render clock, so frames could not be timed exactly; the capture was "
               "not started. See the log.";
      return false;
    }

    settings = s;
    const auto timeRange = iris::ResolveRange(s, project.timeline);
    range = iris::FramesInRange(timeRange.start, timeRange.end, s.frameRate);
    if (range.count <= 0)
    {
      *error = "The recording range contains no frames.";
      return false;
    }
    warmup = std::max(3, s.warmupFrames);
    // Film sets: script-model animations play from their start and cannot be
    // set to a time, so the capture begins early enough (pre-roll, not
    // recorded) for every clip running at the first frame to start on time.
    if (!Engine::IsDemoPlaying() && Director::InFilmSet())
    {
      const double first = s.frameRate.FrameToSeconds(range.first);
      double earliest = first;
      for (const auto& a : project.scene.actors)
        if (a.visible)
        {
          const auto st = iris::EvaluateActor(a, first);
          if (!st.anim.empty())
            earliest = std::min(earliest, st.animStart);
        }
      const double preroll = std::clamp(first - earliest, 0.0, 15.0);
      const auto prerollFrames = static_cast<int>(std::ceil(preroll * s.frameRate.Fps()));
      if (prerollFrames > 0)
      {
        warmup += prerollFrames;
        Log::Info("Capture: {:.2f} s pre-roll so the animations of the first frame start on time", preroll);
      }
      Director::RestartAnimations();
    }
    steps = iris::ComputeFrameSteps(project.timeRemap, s.frameRate, range.first - warmup, range.count + warmup + 8);
    if (steps.hasFreeze)
    {
      *error = "The playback speed reaches zero (or is too slow for the frame rate) inside the range. Freeze frames "
               "cannot be rendered offline yet; raise the speed so each frame advances at least 1 ms.";
      return false;
    }

    // Output location
    auto& session = GetSession();
    std::string ffmpeg;
    {
      std::lock_guard _(session.mutex);
      outputDirectory = s.outputDirectory.empty() ? session.RecordingsDirectory() : std::filesystem::path(s.outputDirectory);
      ffmpeg = s.ffmpegPath.empty() ? session.settings.ffmpegPath : s.ffmpegPath;
    }
    const auto* camera = project.ActiveCamera();
    const auto date = iris::CurrentUtcTimestamp().substr(0, 10);
    baseName = iris::ExpandNamePattern(s, project.name, camera ? camera->name : "camera", "beauty", date,
                                       Engine::RenderWidth(), Engine::RenderHeight());
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec)
    {
      *error = "Cannot create " + outputDirectory.string() + ": " + ec.message();
      return false;
    }

    // The music: in a video file, or a WAV file next to an image sequence.
    const auto music = MusicFor(project, s, range.first, range.count);
    if (music && iris::IsImageSequence(s.format))
    {
      const auto exe = FindFfmpeg(ffmpeg);
      double seconds = s.frameRate.FrameToSeconds(range.first + range.count) - s.frameRate.FrameToSeconds(range.first);
      if (s.audioRangeStart >= 0.0 && s.audioRangeEnd > s.audioRangeStart)
        seconds = s.audioRangeEnd - s.audioRangeStart;
      const auto wav = outputDirectory / (baseName + "_music.wav");
      if (!exe || !RunDetached(*exe, iris::BuildAudioSidecarArguments(*music, seconds, wav.string()),
                               outputDirectory / (baseName + "_music.ffmpeg.log")))
        Log::Warn("Recording: the music could not be written next to the sequence ({})",
                  exe ? "FFmpeg did not start" : "FFmpeg was not found");
    }

    // Encoder
    const bool builtin = iris::HasBuiltinEncoder(s.format) && !s.useFfmpeg;
    if (builtin)
    {
      encoder = std::make_unique<ImageSequenceEncoder>(outputDirectory, baseName,
                                                       s.format == iris::OutputFormat::JpegSequence, s.jpegQuality);
    }
    else
    {
      const auto exe = FindFfmpeg(ffmpeg);
      auto ff = std::make_unique<FfmpegEncoder>();
      const auto out = iris::BuildOutputPath(s, outputDirectory.string(), baseName);
      const auto args = iris::BuildFfmpegArguments(s, Engine::RenderWidth(), Engine::RenderHeight(), out, range.first,
                                                   music ? &*music : nullptr);
      if (!exe || !ff->Open(*exe, args, outputDirectory / (baseName + ".ffmpeg.log"), error))
      {
        if (!exe)
          *error = "FFmpeg was not found.";
        return false;
      }
      encoder = std::move(ff);
    }

    queue = std::make_unique<FrameQueue>(static_cast<std::size_t>(s.queueDepth));
    captured = 0;
    encoded = 0;
    encoderFailed = false;
    encoderError.clear();
    const int workerCount = encoder->Concurrent()
                                ? std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 4)
                                : 1;
    for (int i = 0; i < workerCount; ++i)
      workers.emplace_back([] {
        while (auto f = queue->Pop())
        {
          std::string err;
          if (!encoder->Write(*f, &err))
          {
            std::lock_guard _(mutex);
            encoderFailed = true;
            encoderError = err;
            queue->Close();
            return;
          }
          ++encoded;
        }
      });

    cumulative.assign(1, 0);
    for (const auto step : steps.stepMs)
      cumulative.push_back(cumulative.back() + step);
    mainFrame = 0;
    backendFrame = 0;
    latency = 0;
    tagHits = 0;
    unaligned = 0;
    seekRetries = 0;
    renderFrame = -1;
    startTime = std::chrono::steady_clock::now();
    seekTarget = project.timeRemap.SceneTime(s.frameRate.FrameToSeconds(range.first - warmup)) - 0.25;
    seekWaitFrames = 0;
    {
      std::lock_guard _(mutex);
      progress = {};
      progress.state = State::Seeking;
      progress.framesTotal = range.count;
      progress.outputPath = (outputDirectory / baseName).string();
      progress.message = "Seeking to the start of the range";
    }
    state = State::Seeking;
    if (Engine::IsDemoPlaying())
      DemoControl::Seek(std::max(0.0, seekTarget));
    Log::Info("Capture started: {} frames at {} FPS ({}), output {}", range.count, s.frameRate.ToString(),
              iris::DisplayName(s.format), outputDirectory.string());
    return true;
  }

  void Cancel()
  {
    if (!IsActive())
      return;
    Finish(State::Cancelled, "Cancelled by the user");
  }

  bool IsActive()
  {
    const auto s = state.load();
    return s == State::Seeking || s == State::Warmup || s == State::Recording || s == State::Finishing;
  }

  Progress GetProgress()
  {
    std::lock_guard _(mutex);
    Progress p = progress;
    p.state = state;
    p.framesCaptured = captured;
    p.framesEncoded = encoded;
    p.latencyFrames = latency.load();
    p.syncVerified = tagHits > 0;
    p.clockDriftMs = Clock::WatchdogDriftMs();
    p.elapsedSeconds = IsActive() ? std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count()
                                  : progress.elapsedSeconds;
    return p;
  }

  std::optional<double> MainFrame()
  {
    const auto s = state.load();
    if (s == State::Idle || s == State::Done || s == State::Failed || s == State::Cancelled)
      return std::nullopt;

    if (encoderFailed)
    {
      std::string err;
      {
        std::lock_guard _(mutex);
        err = encoderError;
      }
      Finish(State::Failed, err);
      return std::nullopt;
    }

    if (s == State::Seeking)
    {
      ++seekWaitFrames;
      const bool demo = Engine::IsDemoPlaying();
      if (demo && (DemoControl::IsSeeking() || !DemoControl::HasScene()))
      {
        if (seekWaitFrames > 60 * 600)
          Finish(State::Failed, "The demo did not reach the start of the range.");
        return std::nullopt;
      }
      std::int64_t preAdvance = steps.stepMs.front();
      if (demo)
      {
        const auto now = static_cast<std::int64_t>(std::llround(DemoControl::SceneTime() * 1000.0));
        preAdvance = steps.firstSceneMs - now;
        if (preAdvance < 1)
        {
          // The seek stopped past the start of the range: seek again from
          // further back.
          if (++seekRetries > 3)
          {
            Finish(State::Failed, "Could not position the demo before the start of the range.");
            return std::nullopt;
          }
          seekTarget -= 1.0;
          DemoControl::Seek(std::max(0.0, seekTarget));
          return std::nullopt;
        }
        demoBase = Engine::ClientServerTime() - static_cast<int>(now);  // server time of scene time 0
      }
      // Switch to the render clock. Every game frame from here on advances
      // the scene by a whole, exact step; this one brings it to the first
      // warm-up frame.
      DemoControl::SetPaused(false);
      DemoControl::SetSpeed(1.0);
      Engine::OverrideFloat("timescale", 1.0f);
      Engine::OverrideInt("com_maxfps", 0);
      Clock::Begin();
      Clock::Advance(static_cast<std::int32_t>(preAdvance));
      mainFrame = 0;
      renderFrame = -1;
      sceneBase.reset();
      state = State::Warmup;
      SetMessage("Calibrating frame sync");
      return std::nullopt;
    }

    if (mainFrame >= static_cast<std::int64_t>(steps.stepMs.size()))
    {
      Finish(State::Failed, "The renderer fell behind the capture (frames were not presented).");
      return std::nullopt;
    }
    Clock::Advance(steps.stepMs[static_cast<std::size_t>(mainFrame)]);
    ++mainFrame;

    if (state == State::Warmup && renderFrame + 1 >= warmup)
    {
      state = State::Recording;
      if (tagHits == 0)
      {
        latency = 0;
        Log::Warn("Capture: no sync tag was read back; assuming the renderer presents each frame in the game frame "
                  "that built it. Check the first frames of the recording.");
      }
      SetMessage("Recording");
    }
    if (state == State::Recording && captured >= range.count)
    {
      state = State::Finishing;
      SetMessage("Encoding the last frames");
      Finish(State::Done, unaligned > 0 ? std::to_string(unaligned) + " frame(s) did not land exactly on their scene "
                                          "time; see the capture report."
                                        : "Finished");
    }
    return std::nullopt;
  }

  std::optional<double> FrameTime()
  {
    const auto s = state.load();
    if (s != State::Warmup && s != State::Recording)
      return std::nullopt;
    std::int64_t k = 0;
    if (Engine::IsDemoPlaying())
    {
      // Which capture frame this render is follows from the scene time the
      // client shows: frame k starts cumulative[k] ms after the first one.
      const std::int64_t elapsed = static_cast<std::int64_t>(Engine::ClientServerTime() - demoBase) - steps.firstSceneMs;
      if (elapsed < 0)
        return settings.frameRate.FrameToSeconds(range.first - warmup);
      const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), elapsed);
      if (it == cumulative.end())
        k = static_cast<std::int64_t>(cumulative.size()) - 1;
      else
      {
        k = it - cumulative.begin();
        if (*it != elapsed)
        {
          if (k > 0)
            --k;
          if (s == State::Recording)
            ++unaligned;
        }
      }
    }
    else
    {
      // A live local scene: one capture frame per rendered frame.
      if (!sceneBase)
        sceneBase = 0;
      k = (*sceneBase)++;
    }
    renderFrame = k;
    return settings.frameRate.FrameToSeconds(range.first - warmup + k);
  }

  void DrawSyncTag()
  {
    if (state != State::Warmup || renderFrame < 0)
      return;
    const auto tag = static_cast<int>(renderFrame & 63);
    const float color[4] = {kLevels[(tag >> 4) & 3], kLevels[(tag >> 2) & 3], kLevels[tag & 3], 1.0f};
    Game::CL_DrawStretchPicPhysical(0.0f, 0.0f, static_cast<float>(kTagSize), static_cast<float>(kTagSize), 0.0f, 0.0f,
                                    1.0f, 1.0f, color, Game::sharedUiInfo->assets.whiteMaterial);
  }

  void BackendFrame(IDirect3DDevice9* device)
  {
    std::lock_guard backend(backendMutex);
    const auto s = state.load();
    if (s != State::Warmup && s != State::Recording)
      return;
    const std::int64_t b = backendFrame++;

    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
      return;
    D3DSURFACE_DESC desc{};
    backBuffer->GetDesc(&desc);
    if (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8)
    {
      backBuffer->Release();
      std::lock_guard _(mutex);
      encoderFailed = true;
      encoderError = "Unsupported back buffer format " + std::to_string(static_cast<int>(desc.Format));
      return;
    }
    if (!systemSurface || desc.Width != surfaceDesc.Width || desc.Height != surfaceDesc.Height ||
        desc.Format != surfaceDesc.Format || desc.MultiSampleType != surfaceDesc.MultiSampleType)
    {
      ReleaseSurfaces();
      if (desc.MultiSampleType != D3DMULTISAMPLE_NONE)
        device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolveSurface, nullptr);
      device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &systemSurface, nullptr);
      surfaceDesc = desc;
    }
    IDirect3DSurface9* source = backBuffer;
    if (resolveSurface)
    {
      device->StretchRect(backBuffer, nullptr, resolveSurface, nullptr, D3DTEXF_NONE);
      source = resolveSurface;
    }
    const bool read = systemSurface && SUCCEEDED(device->GetRenderTargetData(source, systemSurface));
    backBuffer->Release();
    if (!read)
      return;

    D3DLOCKED_RECT locked{};
    if (FAILED(systemSurface->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
      return;
    const auto* bits = static_cast<const std::uint8_t*>(locked.pBits);

    if (s == State::Warmup)
    {
      // Read the tag from the middle of its block.
      const auto* p = bits + static_cast<std::size_t>(kTagSize / 2) * locked.Pitch + (kTagSize / 2) * 4;
      const int r = DecodeLevel(p[2]), g = DecodeLevel(p[1]), bl = DecodeLevel(p[0]);
      if (r >= 0 && g >= 0 && bl >= 0)
      {
        const int tag = (r << 4) | (g << 2) | bl;
        // Frames are presented in order, so backend frame b shows capture
        // frame b - latency for a small, constant latency.
        int diff = static_cast<int>(((b - tag) % 64 + 64) % 64);
        if (diff > 32)
          diff -= 64;
        if (std::abs(diff) <= 4)
        {
          latency = diff;
          ++tagHits;
        }
      }
      systemSurface->UnlockRect();
      return;
    }

    // Recording: backend frame b shows capture frame b - latency.
    const std::int64_t k = b - latency.load();
    const std::int64_t j = k - warmup;  // index in the range
    if (j < 0 || j >= range.count || j != captured)
    {
      systemSurface->UnlockRect();
      return;
    }
    Frame f;
    f.number = range.first + j;
    f.width = static_cast<int>(desc.Width);
    f.height = static_cast<int>(desc.Height);
    f.bgra.resize(static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height) * 4);
    for (int y = 0; y < f.height; ++y)
      std::memcpy(f.bgra.data() + static_cast<std::size_t>(y) * f.width * 4, bits + static_cast<std::size_t>(y) * locked.Pitch,
                  static_cast<std::size_t>(f.width) * 4);
    systemSurface->UnlockRect();
    if (desc.Format == D3DFMT_X8R8G8B8)
      for (std::size_t i = 3; i < f.bgra.size(); i += 4)
        f.bgra[i] = 255;
    if (queue && queue->Push(std::move(f)))
      ++captured;
  }

  void OnDeviceLost()
  {
    {
      std::lock_guard backend(backendMutex);
      ReleaseSurfaces();
    }
    if (IsActive())
    {
      std::lock_guard _(mutex);
      encoderFailed = true;
      encoderError = "The graphics device was reset during the capture; frames would be missing.";
    }
  }
}
