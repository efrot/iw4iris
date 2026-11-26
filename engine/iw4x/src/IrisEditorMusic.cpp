// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The Music panel (BeatSync): the montage's music file, where it plays on
// the timeline (start, trim), its volume, the tempo and beats found in it
// (corrected by hand on the timeline's music row), and one-click actions
// that turn beats into ordinary, editable content: camera cuts on beats,
// markers on beats, slow motion between two beats.
#include "Audio.hpp"
#include "EditorInternal.hpp"

#include <iris/beatsync.hpp>

#include <algorithm>
#include <cstdio>
#include <set>

namespace IW4Iris::Editor
{
  namespace
  {
    constexpr const wchar_t* kMusicFilter =
        L"Music (WAV, MP3, FLAC, OGG, M4A)\0*.wav;*.mp3;*.flac;*.ogg;*.oga;*.m4a;*.aac;*.opus\0All files\0*.*\0\0";

    struct MusicUi
    {
      std::string path;              // typed path
      int everyNth = 1;
      std::set<std::string> cameras; // for cuts on beats
      float slow = 0.3f;
      int rampBeats = 1;             // beats the slow motion spans
      double typedBpm = 0.0;
    } music;

    void UseFile(Session& s, const std::string& file)
    {
      iris::AudioTrack a;
      if (s.project.audio)
        a = *s.project.audio;  // keep placement and volume, not the old file's beats
      a.path = file;
      a.name = std::filesystem::path(file).stem().string();
      std::error_code ec;
      a.fileSize = std::filesystem::file_size(file, ec);
      a.fileDuration = 0.0;
      a.trimIn = 0.0;
      a.trimOut = 0.0;
      a.bpm = 0.0;
      a.tempoConfidence = 0.0;
      a.beats.clear();
      a.beatsEdited = false;
      if (!s.project.audio)
        a.start = s.project.timeline.inPoint;
      s.project.audio = a;
      s.pendingAudioAnalysis.reset();
      s.Commit("Add music");
      Notify("Music added: \"Find beats\" in the Music panel finds its tempo and beats");
    }

    // The beats before and after the playhead, `beats` apart.
    std::optional<std::pair<double, double>> BeatsAroundPlayhead(const Session& s, int beats)
    {
      const auto& a = *s.project.audio;
      const auto all = iris::BeatTimes(a, -1e9, 1e9);
      const auto next = std::upper_bound(all.begin(), all.end(), s.playhead);
      if (next == all.begin())
        return std::nullopt;
      const auto from = next - 1;
      const auto to = from + std::max(1, beats);
      if (to >= all.end())
        return std::nullopt;
      return std::make_pair(*from, *to);
    }
  }  // namespace

  void DrawMusic(Session& s)
  {
    if (!ImGui::Begin(PanelName(Panel::Music), &Shown(Panel::Music)))
    {
      ImGui::End();
      return;
    }
    auto& p = s.project;
    if (const auto f = TakePickedFile("music"))
      UseFile(s, *f);

    if (!p.audio)
    {
      ImGui::TextWrapped("Add the montage's music: IW4Iris finds its tempo and beats, shows its waveform on the "
                         "timeline and can cut, mark and ramp the speed on the beats. WAV is read directly; MP3, FLAC, "
                         "OGG and others need FFmpeg (the one used for video recordings).");
      if (ImGui::Button((std::string(Icons::FOLDER_OPEN) + "  Choose a music file ...").c_str(), ImVec2(-1, 0)))
        PickAnyFile("music", kMusicFilter);
      ImGui::SetNextItemWidth(-60);
      ImGui::InputTextWithHint("##path", "or paste a path", &music.path);
      ImGui::SameLine();
      if (ImGui::Button("Use", ImVec2(-1, 0)) && !music.path.empty())
        UseFile(s, music.path);
      ImGui::End();
      return;
    }

    auto& a = *p.audio;
    const auto st = Audio::GetStatus();
    const auto& tl = p.timeline;
    ImGui::SeparatorText((std::string(Icons::FILE) + "  " + a.name).c_str());
    if (st.loading)
      ImGui::TextDisabled("Reading the file ...");
    else if (!st.error.empty())
      ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s %s", Icons::WARNING, st.error.c_str());
    else if (st.duration > 0.0)
      ImGui::TextDisabled("%s, %d Hz%s. The preview is a mono mix; recordings use the file itself.",
                          iris::FormatTime(st.duration, tl.frameRate, tl.display).c_str(), st.sampleRate,
                          st.viaFfmpeg ? ", read with FFmpeg" : "");
    ImGui::SetItemTooltip("%s", a.path.c_str());
    if (ImGui::SmallButton("Another file ..."))
      PickAnyFile("music", kMusicFilter);
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove the music"))
    {
      p.audio.reset();
      s.pendingAudioAnalysis.reset();
      s.Commit("Remove music");
      ImGui::End();
      return;
    }

    // Placement and level.
    ImGui::SeparatorText("On the timeline");
    bool changed = false;
    ImGui::SetNextItemWidth(120);
    changed |= DragDouble("Starts at", &a.start, 0.01f, -3600.0, 36000.0, "%.3f s");
    ImGui::SameLine();
    if (ImGui::SmallButton("At the playhead"))
    {
      a.start = s.playhead;
      changed = true;
    }
    ImGui::SetNextItemWidth(120);
    changed |= DragDouble("Trim in", &a.trimIn, 0.01f, 0.0, std::max(0.0, a.fileDuration), "%.3f s");
    ImGui::SetItemTooltip("Where in the file it starts playing.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    changed |= DragDouble("Trim out", &a.trimOut, 0.01f, 0.0, std::max(0.0, a.fileDuration), "%.3f s");
    ImGui::SetItemTooltip("Where in the file it stops (0: at its end).");
    float volume = static_cast<float>(a.volume);
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Volume", &volume, 0.0f, 2.0f, "%.2f"))
    {
      a.volume = volume;
      changed = true;
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Mute", &a.muted);
    if (changed)
      s.Commit("Music", true);

    // Tempo and beats.
    ImGui::SeparatorText((std::string(Icons::CHART) + "  Tempo and beats").c_str());
    if (a.bpm > 0.0)
      ImGui::Text("%.1f BPM  -  %zu beats%s", a.bpm, a.beats.size(), a.beatsEdited ? " (corrected)" : "");
    else
      ImGui::TextDisabled("No beats yet.");
    if (a.bpm > 0.0 && !a.beatsEdited)
    {
      ImGui::SameLine();
      ImGui::TextDisabled("(clarity %.0f%%)", a.tempoConfidence * 100.0);
      ImGui::SetItemTooltip("How clearly one tempo stands out in the music. Low: check the beats on the timeline, or "
                            "type the tempo.");
    }
    ImGui::BeginDisabled(st.loading || st.analysing || st.duration <= 0.0);
    if (ImGui::Button(st.analysing ? "Finding beats ..." : "Find beats"))
      Audio::Analyse();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Finds the tempo and the beats in the music, on this computer. They are suggestions: "
                          "correct them on the timeline's music row.");
    ImGui::SameLine();
    ImGui::BeginDisabled(a.beats.size() < 2);
    if (ImGui::Button("x2"))
    {
      iris::ScaleTempo(a, true);
      s.Commit("Double the tempo");
    }
    ImGui::SetItemTooltip("Twice the tempo: a beat between each pair.");
    ImGui::SameLine();
    if (ImGui::Button("/2"))
    {
      iris::ScaleTempo(a, false);
      s.Commit("Halve the tempo");
    }
    ImGui::SetItemTooltip("Half the tempo: every other beat.");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(90);
    ImGui::InputDouble("BPM", &music.typedBpm, 0.0, 0.0, "%.2f");
    ImGui::SameLine();
    ImGui::BeginDisabled(!(music.typedBpm > 20.0 && music.typedBpm < 400.0) || a.fileDuration <= 0.0);
    if (ImGui::Button("Grid from the playhead"))
    {
      a.bpm = music.typedBpm;
      a.beats = iris::BeatGrid(music.typedBpm, iris::TimelineToAudio(a, s.playhead), a.fileDuration);
      a.beatsEdited = true;
      s.Commit("Beat grid");
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Beats at this tempo, one on the playhead (put it on a beat you hear).");
    if (s.pendingAudioAnalysis)
    {
      const auto& pa = *s.pendingAudioAnalysis;
      ImGui::TextWrapped("%s A new analysis found %zu beats at %.1f BPM. Your corrected beats were kept.", Icons::INFO,
                         pa.beats.size(), pa.tempo.bpm);
      if (ImGui::SmallButton("Replace my beats"))
      {
        iris::ApplyAnalysis(a, pa);
        s.pendingAudioAnalysis.reset();
        s.Commit("Find beats");
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Keep mine"))
        s.pendingAudioAnalysis.reset();
    }
    ImGui::TextDisabled("Music row: drag a beat to move it, double-click to add one, right-click to remove.");

    // Actions on the beats in / out.
    ImGui::SeparatorText((std::string(Icons::MAGIC) + "  On the beats (in to out)").c_str());
    const auto inRange = iris::BeatTimes(a, tl.inPoint, tl.outPoint);
    ImGui::TextDisabled("%zu beat(s) between in and out", inRange.size());
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("Every", &music.everyNth))
      music.everyNth = std::clamp(music.everyNth, 1, 64);
    ImGui::SameLine();
    ImGui::TextUnformatted("beat(s)");
    if (music.cameras.empty() && p.FindCamera(p.activeCameraId))
      music.cameras.insert(p.activeCameraId);
    if (ImGui::BeginCombo("Cameras", (std::to_string(music.cameras.size()) + " chosen").c_str()))
    {
      for (const auto& c : p.cameras)
      {
        bool on = music.cameras.contains(c.id);
        if (ImGui::Checkbox(c.name.c_str(), &on))
        {
          if (on)
            music.cameras.insert(c.id);
          else
            music.cameras.erase(c.id);
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Cuts on beats go round these cameras in turn.");
    ImGui::BeginDisabled(inRange.empty());
    if (ImGui::Button((std::string(Icons::SCISSORS) + "  Cuts on beats").c_str()))
    {
      std::vector<std::string> ids;
      for (const auto& c : p.cameras)
        if (music.cameras.contains(c.id))
          ids.push_back(c.id);
      const int n = iris::AddCutsOnBeats(p, tl.inPoint, tl.outPoint, music.everyNth, ids, CutAngle(s));
      s.Commit("Cuts on beats");
      Notify(std::to_string(n) + " camera cut(s) on the beats");
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(Icons::FLAG) + "  Markers on beats").c_str()))
    {
      const int n = iris::AddBeatMarkers(p, tl.inPoint, tl.outPoint, music.everyNth);
      s.Commit("Markers on beats");
      Notify(std::to_string(n) + " marker(s) on the beats");
    }
    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Slow", &music.slow, 0.05f, 1.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("beats long", &music.rampBeats))
      music.rampBeats = std::clamp(music.rampBeats, 1, 32);
    const auto around = BeatsAroundPlayhead(s, music.rampBeats);
    ImGui::BeginDisabled(!around);
    if (ImGui::Button((std::string(Icons::CLOCK) + "  Slow motion from the beat at the playhead").c_str()))
    {
      iris::SpeedRampBetween(p, around->first, around->second, music.slow);
      s.Commit("Slow motion on the beats");
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Eases into slow motion after the beat at (or before) the playhead and back to normal speed "
                          "on a later beat: an ordinary speed ramp on the Playback speed row.");
    ImGui::End();
  }
}
