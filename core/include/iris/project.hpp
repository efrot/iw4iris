// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The .iw4iris project: everything a cinematic consists of, as references
// and parameters only. A project never embeds game assets; it names maps,
// demos, models and animations so it can be shared between IW4x editors.
// The file format is versioned JSON, documented in docs/PROJECT_FORMAT.md.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "iris/camera.hpp"
#include "iris/curve.hpp"
#include "iris/recording.hpp"
#include "iris/scene.hpp"
#include "iris/sync.hpp"
#include "iris/timeline.hpp"

namespace iris
{
    inline constexpr int kProjectSchemaVersion = 2;
    inline constexpr std::string_view kProjectFormatName = "iw4iris-project";
    inline constexpr std::string_view kProjectExtension = ".iw4iris";

    enum class CameraMode
    {
        FirstPerson,  // the demo's / game's own view
        ThirdPerson,  // the game's third-person camera around the followed player
        Free,         // flown by hand
        Dolly,        // keyframed path
        Orbit,        // circles a target
        Follow,       // keeps an offset to a target
        Static,       // tripod: fixed transform, optional look-at target
        PlayerEyes,   // a demo player's eyes: exact for the recording player, reconstructed for others
        Chase,        // behind a player, turning with them (third person of any player)
    };
    std::string_view ToString(CameraMode mode);
    std::optional<CameraMode> CameraModeFromString(std::string_view text);

    enum class TargetKind
    {
        None,
        Point,   // a fixed world position
        Player,  // a client number of the demo / game
        Entity,  // an entity number
    };
    std::string_view ToString(TargetKind kind);
    std::optional<TargetKind> TargetKindFromString(std::string_view text);

    struct TargetRef
    {
        TargetKind kind = TargetKind::None;
        int index = -1;           // client or entity number (for a player: the slot when it was chosen)
        Vec3 point;               // for Point
        Vec3 offset{0, 0, 60};    // added to the target's origin (eye height)
        std::string name;         // a player's name: who is meant (slots are reused); empty: the slot alone
        bool operator==(const TargetRef&) const = default;
    };

    // Deterministic handheld / impact shake, evaluated from cinematic time.
    struct ShakeSettings
    {
        bool enabled = false;
        double amplitudePosition = 0.6;  // units
        double amplitudeRotation = 0.35; // degrees
        double frequency = 1.4;          // Hz
        std::uint32_t seed = 1;
        bool operator==(const ShakeSettings&) const = default;
    };

    struct CameraDef
    {
        std::string id;
        std::string name;
        CameraMode mode = CameraMode::Dolly;
        std::string color = "#8B7CF6";

        // Dolly
        CameraPath path;

        // Free / Static
        Vec3 position;
        EulerAngles angles;

        Lens lens;
        bool useLensFov = false;      // FOV from the lens' focal length
        double horizontalFov = 90.0;  // when not using the lens

        // Orbit / Follow / Static look-at
        TargetRef target;
        double orbitDistance = 160.0;
        double orbitHeight = 40.0;
        double orbitSpeed = 20.0;     // degrees per second
        double orbitStartAngle = 0.0; // degrees (chase: 0 behind the player, 180 in front)
        double chaseShoulder = 0.0;   // chase: sideways offset, units (positive: right shoulder)

        ShakeSettings shake;

        bool operator==(const CameraDef& o) const;
    };

    // A keyframed scalar property, addressed by a dotted path such as
    // "film.brightness", "dof.farBlur" or "sun.intensity".
    struct PropertyTrack
    {
        std::string property;
        ScalarCurve curve;
        bool enabled = true;
        bool operator==(const PropertyTrack& o) const
        {
            return property == o.property && curve.Keys() == o.curve.Keys() &&
                   curve.DefaultValue() == o.curve.DefaultValue() && enabled == o.enabled;
        }
    };

    // Editor state of a timeline track (camera, cut, property, ...).
    struct TrackState
    {
        bool locked = false;
        bool hidden = false;
        bool collapsed = false;
        std::string group;
        bool operator==(const TrackState&) const = default;
    };

    // Look settings mapped onto the engine's film tweaks, depth of field, sun
    // and fog. Each block applies only when its override is on, so a project
    // that touches nothing leaves the game exactly as it was.
    struct VisualSettings
    {
        bool filmOverride = false;
        double filmBrightness = 0.0;
        double filmContrast = 1.0;
        double filmDesaturation = 0.0;
        Vec3 filmLightTint{1, 1, 1};
        Vec3 filmDarkTint{1, 1, 1};
        bool filmInvert = false;

        bool dofOverride = false;
        bool dofFromLens = true;  // derive the near/far ranges from the lens
        double dofNearStart = 0.0, dofNearEnd = 0.0, dofFarStart = 1000.0, dofFarEnd = 7000.0;
        double dofNearBlur = 6.0, dofFarBlur = 1.8, dofBias = 0.5;

        bool sunOverride = false;
        Vec3 sunColor{1.0, 0.95, 0.85};
        double sunIntensity = 1.0;
        EulerAngles sunAngles{-45.0, 30.0, 0.0};

        bool fogOverride = false;
        double fogStart = 0.0;
        double fogHalfDistance = 2000.0;
        Vec3 fogColor{0.6, 0.65, 0.7};
        double fogOpacity = 1.0;

        bool hideHud = true;
        bool hideGunInModCameras = true;
        bool hideCrosshair = true;
        bool hideKillfeed = false;
        bool operator==(const VisualSettings&) const = default;
    };

    // Dynamic lights are part of the schema so projects stay compatible as
    // the lighting system lands; their engine integration is still being
    // researched (see FEATURE_MATRIX.md).
    struct LightDef
    {
        std::string id;
        std::string name;
        std::string kind = "point";  // point, spot, directional
        Vec3 position;
        EulerAngles angles;
        Vec3 color{1, 1, 1};
        double intensity = 1.0;
        double radius = 256.0;
        double coneAngle = 45.0;
        double coneSoftness = 0.2;
        double temperatureK = 6500.0;
        bool operator==(const LightDef&) const = default;
    };

    // The demo a project was made on.
    struct DemoRef
    {
        std::string path;        // relative to the project when possible
        std::string name;
        std::string mapName;
        std::string gametype;
        std::string author;
        std::uint64_t fileSize = 0;
        double sceneStart = 0.0; // demo time (s) at timeline 0
        std::optional<int> firstServerTime;  // server time of its first snapshot, once it was played
        std::optional<double> durationSeconds;  // first to last snapshot, once its index was read
        std::optional<KillFeed> killFeed;    // kills it was seen to contain (for synchronising angles)
        bool operator==(const DemoRef&) const = default;
    };

    // How an angle's clock offset was set.
    enum class SyncState
    {
        Unsynced,   // not set: the angle cannot be used
        Suggested,  // a suggestion from the kill feeds was applied, not checked by eye yet
        Manual,     // set or corrected by hand
        Confirmed,  // checked by eye and confirmed
    };
    std::string_view ToString(SyncState state);
    std::optional<SyncState> SyncStateFromString(std::string_view text);

    // Another recording of the main demo's match (a different player's
    // client demo): its recorder's exact first-person view and its own
    // snapshot data. Cuts can switch to it.
    struct DemoAngle
    {
        std::string id;
        std::string name;           // shown, e.g. "Bob's view"
        DemoRef demo;
        std::string recorder;       // the player who recorded it, when known
        int offsetMs = 0;           // its server time minus the main demo's, at one instant
        SyncState sync = SyncState::Unsynced;
        double confidence = 0.0;    // of the suggestion it was set from
        bool operator==(const DemoAngle&) const = default;
    };

    // Why an angle cannot be another recording of the main demo's match
    // (different map or mode), or nothing.
    std::optional<std::string> AngleIncompatibility(const DemoRef& main, const DemoRef& angle);

    // The music of a montage. It plays on the timeline's clock (slow motion
    // in the demo does not slow it down). Beats are stored in seconds into
    // the file, so moving or trimming the track keeps them on the music.
    struct AudioTrack
    {
        std::string path;            // relative to the project when possible
        std::string name;
        std::uint64_t fileSize = 0;
        double start = 0.0;          // timeline time at which `trimIn` plays
        double trimIn = 0.0;         // seconds into the file
        double trimOut = 0.0;        // seconds into the file where it stops (0: its end)
        double fileDuration = 0.0;   // seconds, once the file was read
        double volume = 1.0;         // 0..2
        bool muted = false;
        double bpm = 0.0;            // detected or typed
        double tempoConfidence = 0.0;
        std::vector<double> beats;   // seconds into the file: detected, then corrected by the user
        bool beatsEdited = false;    // corrected by hand (a new analysis asks before replacing them)
        bool operator==(const AudioTrack&) const = default;
    };

    struct ExportSettings
    {
        double unitsPerMetre = 39.37007874;  // game units are inches
        bool blenderIncludeKeysOnly = false; // false: bake every frame
        double aeUnitsPerPixel = 1.0;
        bool operator==(const ExportSettings&) const = default;
    };

    struct Project
    {
        int schemaVersion = kProjectSchemaVersion;
        std::string name = "Untitled";
        std::string generator;
        std::string createdUtc;
        std::string modifiedUtc;

        std::string mapName;               // the film set's map (Director Mode, no demo)
        std::optional<DemoRef> demo;
        std::vector<DemoAngle> angles;     // other recordings of the same match
        std::optional<AudioTrack> audio;   // the montage's music

        TimelineSettings timeline;
        TimeRemap timeRemap;

        std::vector<CameraDef> cameras;
        std::string activeCameraId;
        CameraCutTrack cuts;
        bool useCameraCuts = false;

        std::vector<Marker> markers;
        std::vector<PropertyTrack> tracks;
        std::map<std::string, TrackState> trackStates;

        VisualSettings visuals;
        std::vector<LightDef> lights;
        Scene scene;                       // actors, props, effects, motion graphics

        RecordingSettings recording;
        std::vector<RenderJob> renderQueue;
        ExportSettings exports;

        std::string editorLayout;  // Dear ImGui ini blob of the dock layout
        std::string workspace = "Cinematic";

        CameraDef* FindCamera(std::string_view id);
        const CameraDef* FindCamera(std::string_view id) const;
        CameraDef* ActiveCamera();
        const CameraDef* ActiveCamera() const;
        // The camera the shot uses at cinematic time t (camera cuts when on).
        const CameraDef* CameraAt(double t) const;
        DemoAngle* FindAngle(std::string_view id);
        const DemoAngle* FindAngle(std::string_view id) const;
        PropertyTrack* FindTrack(std::string_view property);
        const PropertyTrack* FindTrack(std::string_view property) const;
        // Value of a keyframed property at t, or `fallback` without a track.
        double PropertyValue(std::string_view property, double t, double fallback) const;

        // Latest key / cut / marker time, for fitting the timeline.
        double ContentEnd() const;
    };

    // The part of the main demo's scene time (seconds) a recording covers:
    // [0, length] for the main demo, the angle's own span moved onto the
    // main clock for an angle. None while its start or length is unknown
    // (it was not played yet).
    struct SceneSpan
    {
        double start = 0.0;
        double end = 0.0;
        bool operator==(const SceneSpan&) const = default;
    };
    std::optional<SceneSpan> RecordingCoverage(const Project& project, const std::string& angleId);

    // The parts of [start, end] (cinematic time) and the recording each needs:
    // consecutive cuts in one angle are merged. Without camera cuts the whole
    // range is the main demo (angle "").
    struct AngleSegment
    {
        std::string angle;
        double start = 0.0;
        double end = 0.0;
        bool operator==(const AngleSegment&) const = default;
    };
    std::vector<AngleSegment> AngleSegments(const Project& project, double start, double end);

    // The render jobs for the recording range of `settings` when it cuts
    // between recordings: one job per segment, each with its own range and
    // the recording it needs loaded. Image sequences share one name (with
    // `stamp` for {date}), so the segments' frames, numbered by timeline
    // frame, form one sequence; a video format gives one file per segment.
    std::vector<RenderJob> PlanAngleRenders(const Project& project, const RecordingSettings& settings,
                                            std::string_view stamp);

    // A new, unique id ("cam-3", "marker-12", ...) for the project.
    std::string MakeUniqueId(const Project& project, std::string_view prefix);
    // A project with one dolly camera, ready for the editor.
    Project MakeDefaultProject(std::string_view name);

    struct LoadResult
    {
        std::optional<Project> project;
        int originalSchemaVersion = 0;
        bool migrated = false;
        std::vector<std::string> warnings;
        std::string error;
        explicit operator bool() const { return project.has_value(); }
    };

    std::string SerializeProject(const Project& project, bool pretty = true);
    LoadResult ParseProject(std::string_view json);

    // Atomic save: writes "<file>.tmp", flushes, then replaces the file. When
    // `projectDirectory` is set, the demo path is stored relative to it.
    bool SaveProjectFile(const std::filesystem::path& file, Project project, std::string* error = nullptr);
    LoadResult LoadProjectFile(const std::filesystem::path& file);

    // Resolves the demo path of a project loaded from `file` (relative paths
    // are relative to the project's folder).
    std::optional<std::filesystem::path> ResolveDemoPath(const Project& project, const std::filesystem::path& file);
    // The music file's path, the same way.
    std::optional<std::filesystem::path> ResolveAudioPath(const Project& project, const std::filesystem::path& file);
    // The same for any demo reference of the project (the main demo, an angle).
    std::optional<std::filesystem::path> ResolveDemoRefPath(const DemoRef& demo, const std::filesystem::path& file);

    // Autosave / crash recovery: the autosave lives next to the project as
    // "<name>.iw4iris.autosave"; an autosave newer than its project after a
    // crash is offered for recovery.
    std::filesystem::path AutosavePathFor(const std::filesystem::path& projectFile);
    bool IsAutosaveNewer(const std::filesystem::path& projectFile);

    // Recent projects list (most recent first, de-duplicated, capped).
    std::vector<std::string> PushRecent(std::vector<std::string> recent, const std::string& path, std::size_t cap = 12);

    // Imports a campath exported by IWXMVM / Iris Studio / T6Iris
    // (properties[].keyframes[].value.values = x y z pitch yaw roll fov,
    // ticks in milliseconds). Times become seconds relative to the first key
    // unless `absoluteTicks` is set.
    std::optional<CameraPath> ImportIwxmvmCampath(std::string_view json, std::string* error = nullptr,
                                                  bool absoluteTicks = false);

    // Checks the references of a project (demo file present, ...), returning
    // human-readable problems.
    std::vector<std::string> FindMissingReferences(const Project& project, const std::filesystem::path& projectFile);

    std::string CurrentUtcTimestamp();
}  // namespace iris
