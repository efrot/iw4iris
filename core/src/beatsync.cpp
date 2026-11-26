// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/beatsync.hpp"

#include <algorithm>
#include <cmath>

namespace iris
{
    namespace
    {
        void SortBeats(AudioTrack& track)
        {
            std::sort(track.beats.begin(), track.beats.end());
            track.beats.erase(std::unique(track.beats.begin(), track.beats.end(),
                                          [](double a, double b) { return std::abs(a - b) < 1e-6; }),
                              track.beats.end());
        }

        // Index of the beat nearest to a timeline time, within maxDistance.
        std::optional<std::size_t> NearestIndex(const AudioTrack& track, double timelineSeconds, double maxDistance)
        {
            std::optional<std::size_t> best;
            double bestDistance = maxDistance;
            for (std::size_t i = 0; i < track.beats.size(); ++i)
            {
                const double d = std::abs(AudioToTimeline(track, track.beats[i]) - timelineSeconds);
                if (d <= bestDistance)
                {
                    bestDistance = d;
                    best = i;
                }
            }
            return best;
        }
    }  // namespace

    double AudioToTimeline(const AudioTrack& track, double fileSeconds)
    {
        return track.start + (fileSeconds - track.trimIn);
    }

    double TimelineToAudio(const AudioTrack& track, double timelineSeconds)
    {
        return timelineSeconds - track.start + track.trimIn;
    }

    double AudioEnd(const AudioTrack& track)
    {
        if (track.trimOut > track.trimIn)
            return track.fileDuration > 0.0 ? std::min(track.trimOut, track.fileDuration) : track.trimOut;
        return track.fileDuration;
    }

    double AudioTimelineEnd(const AudioTrack& track)
    {
        return AudioToTimeline(track, std::max(AudioEnd(track), track.trimIn));
    }

    std::optional<AudioInput> AudioForRange(const AudioTrack& track, const std::string& resolvedPath,
                                            double firstFrameTime, double endTime)
    {
        if (track.muted || resolvedPath.empty() || !(endTime > firstFrameTime))
            return std::nullopt;
        const double playFrom = std::max(firstFrameTime, track.start);
        const double playTo = std::min(endTime, AudioTimelineEnd(track));
        if (!(playTo > playFrom))
            return std::nullopt;
        AudioInput in;
        in.path = resolvedPath;
        in.delay = playFrom - firstFrameTime;
        in.fileStart = TimelineToAudio(track, playFrom);
        in.duration = playTo - playFrom;
        in.volume = track.volume;
        return in;
    }

    std::vector<double> BeatTimes(const AudioTrack& track, double from, double to)
    {
        std::vector<double> out;
        const double end = AudioEnd(track);
        for (const double b : track.beats)
        {
            if (b < track.trimIn - 1e-9 || (end > 0.0 && b > end + 1e-9))
                continue;
            const double t = AudioToTimeline(track, b);
            if (t >= from - 1e-9 && t < to - 1e-9)
                out.push_back(t);
        }
        return out;
    }

    std::optional<double> NearestBeat(const AudioTrack& track, double timelineSeconds, double maxDistance)
    {
        if (const auto i = NearestIndex(track, timelineSeconds, maxDistance))
            return AudioToTimeline(track, track.beats[*i]);
        return std::nullopt;
    }

    void ApplyAnalysis(AudioTrack& track, const AudioAnalysis& analysis)
    {
        track.bpm = analysis.tempo.bpm;
        track.tempoConfidence = analysis.tempo.confidence;
        track.beats = analysis.beats;
        track.beatsEdited = false;
        SortBeats(track);
    }

    std::vector<double> BeatGrid(double bpm, double phase, double fileDuration)
    {
        std::vector<double> out;
        if (!(bpm > 0.0) || !(fileDuration > 0.0))
            return out;
        const double period = 60.0 / bpm;
        double t = std::fmod(phase, period);
        if (t < 0.0)
            t += period;
        for (; t <= fileDuration + 1e-9 && out.size() < 100000; t += period)
            out.push_back(t);
        return out;
    }

    void ScaleTempo(AudioTrack& track, bool doubleIt)
    {
        SortBeats(track);
        std::vector<double> beats;
        if (doubleIt)
        {
            for (std::size_t i = 0; i < track.beats.size(); ++i)
            {
                beats.push_back(track.beats[i]);
                if (i + 1 < track.beats.size())
                    beats.push_back(0.5 * (track.beats[i] + track.beats[i + 1]));
            }
            track.bpm *= 2.0;
        }
        else
        {
            for (std::size_t i = 0; i < track.beats.size(); i += 2)
                beats.push_back(track.beats[i]);
            track.bpm *= 0.5;
        }
        track.beats = std::move(beats);
        track.beatsEdited = true;
    }

    bool MoveBeat(AudioTrack& track, double fromTimeline, double toTimeline, double maxDistance)
    {
        const auto i = NearestIndex(track, fromTimeline, maxDistance);
        if (!i)
            return false;
        track.beats[*i] = TimelineToAudio(track, toTimeline);
        track.beatsEdited = true;
        SortBeats(track);
        return true;
    }

    void AddBeat(AudioTrack& track, double timelineSeconds)
    {
        track.beats.push_back(TimelineToAudio(track, timelineSeconds));
        track.beatsEdited = true;
        SortBeats(track);
    }

    bool RemoveBeat(AudioTrack& track, double timelineSeconds, double maxDistance)
    {
        const auto i = NearestIndex(track, timelineSeconds, maxDistance);
        if (!i)
            return false;
        track.beats.erase(track.beats.begin() + static_cast<std::ptrdiff_t>(*i));
        track.beatsEdited = true;
        return true;
    }

    int AddCutsOnBeats(Project& project, double from, double to, int everyNth, const std::vector<std::string>& cameras,
                       const std::string& angle)
    {
        if (!project.audio)
            return 0;
        std::vector<std::string> ids;
        for (const auto& id : cameras)
            if (project.FindCamera(id))
                ids.push_back(id);
        if (ids.empty() && project.FindCamera(project.activeCameraId))
            ids.push_back(project.activeCameraId);
        if (ids.empty())
            return 0;
        everyNth = std::max(1, everyNth);
        const auto beats = BeatTimes(*project.audio, from, to);
        int added = 0;
        for (std::size_t i = 0; i < beats.size(); i += static_cast<std::size_t>(everyNth))
        {
            project.cuts.AddCut({beats[i], ids[static_cast<std::size_t>(added) % ids.size()], angle});
            ++added;
        }
        if (added)
            project.useCameraCuts = true;
        return added;
    }

    int AddBeatMarkers(Project& project, double from, double to, int everyNth)
    {
        if (!project.audio)
            return 0;
        everyNth = std::max(1, everyNth);
        const auto beats = BeatTimes(*project.audio, from, to);
        int added = 0;
        for (std::size_t i = 0; i < beats.size(); i += static_cast<std::size_t>(everyNth))
        {
            const double t = beats[i];
            const bool exists = std::any_of(project.markers.begin(), project.markers.end(), [&](const Marker& m) {
                return m.kind == "beat" && std::abs(m.time - t) < 1e-4;
            });
            if (exists)
                continue;
            Marker m;
            m.id = MakeUniqueId(project, "marker");
            m.time = t;
            m.name = "Beat " + std::to_string(i + 1);
            m.kind = "beat";
            m.color = "#5EA1F7";
            project.markers.push_back(m);
            ++added;
        }
        std::sort(project.markers.begin(), project.markers.end(),
                  [](const Marker& a, const Marker& b) { return a.time < b.time; });
        return added;
    }

    void SpeedRampBetween(Project& project, double a, double b, double slow)
    {
        if (b < a)
            std::swap(a, b);
        if (!(b - a > 1e-3))
            return;
        slow = std::clamp(slow, 0.02, 4.0);
        auto& remap = project.timeRemap;
        const double before = remap.SpeedAt(a), after = remap.SpeedAt(b);
        const double ramp = std::min(0.35, 0.25 * (b - a));
        std::vector<ScalarKey> keys;
        for (const auto& key : remap.Speed().Keys())
            if (key.time < a - 1e-9 || key.time > b + 1e-9)
                keys.push_back(key);
        auto add = [&](double t, double v) {
            ScalarKey key;
            key.time = t;
            key.value = v;
            keys.push_back(key);
        };
        add(a, before);
        add(a + ramp, slow);
        add(b - ramp, slow);
        add(b, after);
        std::sort(keys.begin(), keys.end(), [](const ScalarKey& x, const ScalarKey& y) { return x.time < y.time; });
        remap.Speed().SetKeys(std::move(keys));
        remap.Speed().UpdateAutoHandles();
    }
}  // namespace iris
