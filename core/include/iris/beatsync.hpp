// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// BeatSync: the music's beats on the timeline and one-click actions that
// turn them into ordinary, editable project content (camera cuts, markers,
// speed ramps). Nothing here is hidden state: an action adds cuts, keys or
// markers the user can move or delete like any other.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "iris/audio.hpp"
#include "iris/project.hpp"
#include "iris/recording.hpp"

namespace iris
{
    // File time <-> timeline time.
    double AudioToTimeline(const AudioTrack& track, double fileSeconds);
    double TimelineToAudio(const AudioTrack& track, double timelineSeconds);
    // Where the track stops in the file (trimOut, else its length).
    double AudioEnd(const AudioTrack& track);
    // The timeline span the track plays over.
    double AudioTimelineEnd(const AudioTrack& track);

    // The music of a recording whose first frame is at `firstFrameTime` and
    // which ends at `endTime` (timeline seconds), or none (muted, or the
    // track does not play in that time).
    std::optional<AudioInput> AudioForRange(const AudioTrack& track, const std::string& resolvedPath,
                                            double firstFrameTime, double endTime);

    // Beats (timeline times) inside the trimmed track and [from, to].
    std::vector<double> BeatTimes(const AudioTrack& track, double from, double to);
    std::optional<double> NearestBeat(const AudioTrack& track, double timelineSeconds, double maxDistance);

    // Stores an analysis in the track: tempo and beats (seconds into the file).
    void ApplyAnalysis(AudioTrack& track, const AudioAnalysis& analysis);
    // Beats every `period` seconds from `phase` (seconds into the file) over
    // the whole file: for a typed tempo, or doubling / halving.
    std::vector<double> BeatGrid(double bpm, double phase, double fileDuration);
    // Doubles or halves the tempo, keeping the beats that were there (half:
    // every other beat; double: a beat between each pair).
    void ScaleTempo(AudioTrack& track, bool doubleIt);

    // Moves the beat nearest to `fromTimeline` to `toTimeline`, adds one, or
    // removes the one nearest (within maxDistance). Each marks the beats edited.
    bool MoveBeat(AudioTrack& track, double fromTimeline, double toTimeline, double maxDistance);
    void AddBeat(AudioTrack& track, double timelineSeconds);
    bool RemoveBeat(AudioTrack& track, double timelineSeconds, double maxDistance);

    // A camera cut on every `everyNth` beat in [from, to), cycling through
    // `cameras` (ids), in the given recording (angle id, "" the main demo).
    // Turns camera cuts on. Returns the number of cuts added.
    int AddCutsOnBeats(Project& project, double from, double to, int everyNth, const std::vector<std::string>& cameras,
                       const std::string& angle);
    // A marker (kind "beat") on every `everyNth` beat in [from, to); beats
    // that already have one are skipped. Returns the number added.
    int AddBeatMarkers(Project& project, double from, double to, int everyNth);
    // Slow motion from beat `a` to beat `b` (timeline times): the speed
    // eases from normal to `slow` after a and back to normal by b, so the
    // change lands on the music. Speed keys inside (a, b) are replaced.
    void SpeedRampBetween(Project& project, double a, double b, double slow);
}  // namespace iris
