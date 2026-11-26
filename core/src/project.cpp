// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/project.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "iris/version.hpp"
#include "json_util.hpp"
#include "scene_json.hpp"

namespace iris
{
    using json = nlohmann::json;

    // ------------------------------------------------------------ enums

    std::string_view ToString(CameraMode mode)
    {
        switch (mode)
        {
            case CameraMode::FirstPerson: return "firstPerson";
            case CameraMode::ThirdPerson: return "thirdPerson";
            case CameraMode::Free: return "free";
            case CameraMode::Dolly: return "dolly";
            case CameraMode::Orbit: return "orbit";
            case CameraMode::Follow: return "follow";
            case CameraMode::Static: return "static";
            case CameraMode::PlayerEyes: return "playerEyes";
            case CameraMode::Chase: return "chase";
        }
        return "dolly";
    }

    std::optional<CameraMode> CameraModeFromString(std::string_view text)
    {
        for (auto m : {CameraMode::FirstPerson, CameraMode::ThirdPerson, CameraMode::Free, CameraMode::Dolly,
                       CameraMode::Orbit, CameraMode::Follow, CameraMode::Static, CameraMode::PlayerEyes,
                       CameraMode::Chase})
            if (ToString(m) == text)
                return m;
        return std::nullopt;
    }

    std::string_view ToString(SyncState state)
    {
        switch (state)
        {
            case SyncState::Unsynced: return "unsynced";
            case SyncState::Suggested: return "suggested";
            case SyncState::Manual: return "manual";
            case SyncState::Confirmed: return "confirmed";
        }
        return "unsynced";
    }

    std::optional<SyncState> SyncStateFromString(std::string_view text)
    {
        for (auto st : {SyncState::Unsynced, SyncState::Suggested, SyncState::Manual, SyncState::Confirmed})
            if (ToString(st) == text)
                return st;
        return std::nullopt;
    }

    std::optional<std::string> AngleIncompatibility(const DemoRef& main, const DemoRef& angle)
    {
        auto lower = [](std::string t) {
            for (auto& ch : t)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return t;
        };
        if (!main.mapName.empty() && !angle.mapName.empty() && lower(main.mapName) != lower(angle.mapName))
            return "It was recorded on another map (" + angle.mapName + ", the main demo on " + main.mapName + ").";
        if (!main.gametype.empty() && !angle.gametype.empty() && lower(main.gametype) != lower(angle.gametype))
            return "It was recorded in another mode (" + angle.gametype + ", the main demo in " + main.gametype + ").";
        if (!main.path.empty() && main.path == angle.path)
            return "It is the main demo itself.";
        return std::nullopt;
    }

    std::string_view ToString(TargetKind kind)
    {
        switch (kind)
        {
            case TargetKind::None: return "none";
            case TargetKind::Point: return "point";
            case TargetKind::Player: return "player";
            case TargetKind::Entity: return "entity";
        }
        return "none";
    }

    std::optional<TargetKind> TargetKindFromString(std::string_view text)
    {
        for (auto k : {TargetKind::None, TargetKind::Point, TargetKind::Player, TargetKind::Entity})
            if (ToString(k) == text)
                return k;
        return std::nullopt;
    }

    bool CameraDef::operator==(const CameraDef& o) const
    {
        return id == o.id && name == o.name && mode == o.mode && color == o.color && path.Keys() == o.path.Keys() &&
               path.GetRotationMode() == o.path.GetRotationMode() && path.IsConstantSpeed() == o.path.IsConstantSpeed() &&
               position == o.position && angles == o.angles && lens == o.lens && useLensFov == o.useLensFov &&
               horizontalFov == o.horizontalFov && target == o.target && orbitDistance == o.orbitDistance &&
               orbitHeight == o.orbitHeight && orbitSpeed == o.orbitSpeed && orbitStartAngle == o.orbitStartAngle &&
               chaseShoulder == o.chaseShoulder && shake == o.shake;
    }

    // ------------------------------------------------------------ project queries

    CameraDef* Project::FindCamera(std::string_view id)
    {
        for (auto& c : cameras)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    const CameraDef* Project::FindCamera(std::string_view id) const
    {
        for (const auto& c : cameras)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    CameraDef* Project::ActiveCamera()
    {
        return FindCamera(activeCameraId);
    }

    const CameraDef* Project::ActiveCamera() const
    {
        return FindCamera(activeCameraId);
    }

    const CameraDef* Project::CameraAt(double t) const
    {
        if (useCameraCuts)
            if (const auto id = cuts.CameraAt(t))
                if (const auto* c = FindCamera(*id))
                    return c;
        return ActiveCamera();
    }

    DemoAngle* Project::FindAngle(std::string_view id)
    {
        for (auto& a : angles)
            if (a.id == id)
                return &a;
        return nullptr;
    }

    const DemoAngle* Project::FindAngle(std::string_view id) const
    {
        for (const auto& a : angles)
            if (a.id == id)
                return &a;
        return nullptr;
    }

    PropertyTrack* Project::FindTrack(std::string_view property)
    {
        for (auto& t : tracks)
            if (t.property == property)
                return &t;
        return nullptr;
    }

    const PropertyTrack* Project::FindTrack(std::string_view property) const
    {
        for (const auto& t : tracks)
            if (t.property == property)
                return &t;
        return nullptr;
    }

    double Project::PropertyValue(std::string_view property, double t, double fallback) const
    {
        const auto* track = FindTrack(property);
        if (!track || !track->enabled || track->curve.Empty())
            return fallback;
        return track->curve.Evaluate(t);
    }

    double Project::ContentEnd() const
    {
        double end = 0.0;
        for (const auto& c : cameras)
            end = std::max(end, c.path.EndTime());
        for (const auto& c : cuts.Cuts())
            end = std::max(end, c.time);
        for (const auto& m : markers)
            end = std::max(end, m.time);
        for (const auto& t : tracks)
            if (!t.curve.Empty())
                end = std::max(end, t.curve.Keys().back().time);
        return std::max(end, scene.ContentEnd());
    }

    std::vector<AngleSegment> AngleSegments(const Project& project, double start, double end)
    {
        std::vector<AngleSegment> out;
        if (!(end > start))
            return out;
        auto add = [&](const std::string& angle, double a, double b) {
            a = std::max(a, start);
            b = std::min(b, end);
            if (!(b > a))
                return;
            if (!out.empty() && out.back().angle == angle && std::abs(out.back().end - a) < 1e-9)
                out.back().end = b;
            else
                out.push_back({angle, a, b});
        };
        const auto& cuts = project.cuts.Cuts();
        if (!project.useCameraCuts || cuts.empty())
        {
            add("", start, end);
            return out;
        }
        // Before the first cut its camera (and angle) is used, as CameraAt does.
        for (std::size_t i = 0; i < cuts.size(); ++i)
        {
            const double a = i == 0 ? start : cuts[i].time;
            const double b = i + 1 < cuts.size() ? cuts[i + 1].time : end;
            add(cuts[i].angle, a, b);
        }
        return out;
    }

    std::optional<SceneSpan> RecordingCoverage(const Project& project, const std::string& angleId)
    {
        if (!project.demo)
            return std::nullopt;
        if (angleId.empty())
        {
            if (!project.demo->durationSeconds)
                return std::nullopt;
            return SceneSpan{0.0, *project.demo->durationSeconds};
        }
        const auto* a = project.FindAngle(angleId);
        if (!a || !a->demo.durationSeconds || !a->demo.firstServerTime || !project.demo->firstServerTime)
            return std::nullopt;
        const double shift = AngleSceneShift(*project.demo->firstServerTime, *a->demo.firstServerTime, a->offsetMs);
        return SceneSpan{shift, shift + *a->demo.durationSeconds};
    }

    std::vector<RenderJob> PlanAngleRenders(const Project& project, const RecordingSettings& settings,
                                            std::string_view stamp)
    {
        std::vector<RenderJob> jobs;
        const auto range = ResolveRange(settings, project.timeline);
        const auto segments = AngleSegments(project, range.start, range.end);
        std::string pattern = settings.namePattern.empty() ? std::string("{project}_{pass}") : settings.namePattern;
        for (std::size_t pos = 0; (pos = pattern.find("{date}", pos)) != std::string::npos;)
        {
            pattern.replace(pos, 6, stamp);
            pos += stamp.size();
        }
        Project ids = project;  // ids unique among the queue and the new jobs
        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            const auto& seg = segments[i];
            const auto* a = project.FindAngle(seg.angle);
            const std::string recording = seg.angle.empty() ? std::string("main demo") : (a ? a->name : seg.angle);
            RenderJob job;
            job.id = MakeUniqueId(ids, "job");
            char span[64];
            std::snprintf(span, sizeof(span), "%.2f-%.2f s", seg.start, seg.end);
            job.name = project.name + " " + std::to_string(i + 1) + "/" + std::to_string(segments.size()) + ": " +
                       recording + " " + span;
            job.angle = seg.angle;
            job.settings = settings;
            job.settings.range = RangeMode::Custom;
            job.settings.customStart = seg.start;
            job.settings.customEnd = seg.end;
            job.settings.namePattern =
                IsImageSequence(settings.format) ? pattern : pattern + "_part" + std::to_string(i + 1);
            if (IsImageSequence(settings.format))
            {
                // One sequence, one music file: the first part writes the
                // whole range's music.
                job.settings.includeAudio = settings.includeAudio && i == 0;
                job.settings.audioRangeStart = range.start;
                job.settings.audioRangeEnd = range.end;
            }
            ids.renderQueue.push_back(job);
            jobs.push_back(std::move(job));
        }
        return jobs;
    }

    std::string MakeUniqueId(const Project& project, std::string_view prefix)
    {
        std::set<std::string> used;
        for (const auto& c : project.cameras) used.insert(c.id);
        for (const auto& m : project.markers) used.insert(m.id);
        for (const auto& l : project.lights) used.insert(l.id);
        for (const auto& a : project.scene.actors) used.insert(a.id);
        for (const auto& p : project.scene.props) used.insert(p.id);
        for (const auto& e : project.scene.effects) used.insert(e.id);
        for (const auto& g : project.scene.graphics) used.insert(g.id);
        for (const auto& j : project.renderQueue) used.insert(j.id);
        for (const auto& a : project.angles) used.insert(a.id);
        for (int i = 1;; ++i)
        {
            std::string id = std::string(prefix) + "-" + std::to_string(i);
            if (!used.contains(id))
                return id;
        }
    }

    Project MakeDefaultProject(std::string_view name)
    {
        Project p;
        p.name = std::string(name);
        p.generator = std::string(kVersionString);
        p.createdUtc = p.modifiedUtc = CurrentUtcTimestamp();
        CameraDef cam;
        cam.id = "cam-1";
        cam.name = "Dolly 1";
        cam.mode = CameraMode::Dolly;
        p.cameras.push_back(cam);
        CameraDef free;
        free.id = "cam-2";
        free.name = "Free camera";
        free.mode = CameraMode::Free;
        free.color = "#5EA1F7";
        p.cameras.push_back(free);
        CameraDef game;
        game.id = "cam-3";
        game.name = "Game view";
        game.mode = CameraMode::FirstPerson;
        game.color = "#9AA4B2";
        p.cameras.push_back(game);
        // Start on the game's own view: nothing changes until a cinematic
        // camera is chosen.
        p.activeCameraId = "cam-3";
        p.timeline.Normalize();
        return p;
    }

    std::string CurrentUtcTimestamp()
    {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &now);
#else
        gmtime_r(&now, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }

    // ------------------------------------------------------------ JSON helpers

    namespace
    {
        using namespace detail;

        json CameraKeyJson(const CameraKey& k)
        {
            json j{{"t", k.time},
                   {"pos", ToJson(k.position)},
                   {"ang", ToJson(k.angles)},
                   {"fov", k.horizontalFov},
                   {"focus", k.focusDistance},
                   {"fstop", k.fNumber},
                   {"interp", ToString(k.interpolation)}};
            if (k.ease.in != 0.0 || k.ease.out != 0.0)
                j["ease"] = EaseJson(k.ease);
            if (k.interpolation == Interpolation::Bezier || k.inTangent != Vec3{} || k.outTangent != Vec3{})
            {
                j["inTangent"] = ToJson(k.inTangent);
                j["outTangent"] = ToJson(k.outTangent);
            }
            if (k.tension != 0.0)
                j["tension"] = k.tension;
            return j;
        }

        CameraKey CameraKeyFrom(const json& j)
        {
            CameraKey k;
            k.time = GetNumber(j, "t", 0.0);
            k.position = Vec3From(j.value("pos", json()));
            k.angles = AnglesFrom(j.value("ang", json()));
            k.horizontalFov = Clamp(GetNumber(j, "fov", 90.0), 1.0, 179.0);
            k.focusDistance = std::max(0.0, GetNumber(j, "focus", 256.0));
            k.fNumber = Clamp(GetNumber(j, "fstop", 2.8), 0.5, 64.0);
            k.interpolation = InterpolationFromString(Get<std::string>(j, "interp", "smooth")).value_or(Interpolation::Smooth);
            if (j.contains("ease"))
                k.ease = EaseFrom(j["ease"]);
            k.inTangent = Vec3From(j.value("inTangent", json()));
            k.outTangent = Vec3From(j.value("outTangent", json()));
            k.tension = Clamp(GetNumber(j, "tension", 0.0), 0.0, 1.0);
            return k;
        }

        json LensJson(const Lens& l)
        {
            return {{"focalLength", l.focalLengthMm},
                    {"sensor", {{"name", l.sensor.name}, {"width", l.sensor.widthMm}, {"height", l.sensor.heightMm}}},
                    {"fstop", l.fNumber},
                    {"focus", l.focusDistance},
                    {"anamorphic", l.anamorphicSqueeze},
                    {"breathing", l.breathing}};
        }

        Lens LensFrom(const json& j)
        {
            Lens l;
            if (!j.is_object())
                return l;
            l.focalLengthMm = Clamp(GetNumber(j, "focalLength", l.focalLengthMm), 1.0, 2000.0);
            if (const auto it = j.find("sensor"); it != j.end() && it->is_object())
            {
                l.sensor.name = Get<std::string>(*it, "name", l.sensor.name);
                l.sensor.widthMm = Clamp(GetNumber(*it, "width", l.sensor.widthMm), 1.0, 100.0);
                l.sensor.heightMm = Clamp(GetNumber(*it, "height", l.sensor.heightMm), 1.0, 100.0);
            }
            l.fNumber = Clamp(GetNumber(j, "fstop", l.fNumber), 0.5, 64.0);
            l.focusDistance = std::max(0.0, GetNumber(j, "focus", l.focusDistance));
            l.anamorphicSqueeze = Clamp(GetNumber(j, "anamorphic", 1.0), 1.0, 2.0);
            l.breathing = Clamp(GetNumber(j, "breathing", 0.0), 0.0, 1.0);
            return l;
        }

        json TargetJson(const TargetRef& t)
        {
            json j{{"kind", ToString(t.kind)}, {"index", t.index}, {"point", ToJson(t.point)}, {"offset", ToJson(t.offset)}};
            if (!t.name.empty())
                j["name"] = t.name;
            return j;
        }

        TargetRef TargetFrom(const json& j)
        {
            TargetRef t;
            if (!j.is_object())
                return t;
            t.kind = TargetKindFromString(Get<std::string>(j, "kind", "none")).value_or(TargetKind::None);
            t.index = Get<int>(j, "index", -1);
            t.name = Get<std::string>(j, "name", "");
            t.point = Vec3From(j.value("point", json()));
            t.offset = Vec3From(j.value("offset", json()), t.offset);
            return t;
        }

        json CameraJson(const CameraDef& c)
        {
            json keys = json::array();
            for (const auto& k : c.path.Keys())
                keys.push_back(CameraKeyJson(k));
            return {{"id", c.id},
                    {"name", c.name},
                    {"mode", ToString(c.mode)},
                    {"color", c.color},
                    {"path",
                     {{"rotation", ToString(c.path.GetRotationMode())},
                      {"constantSpeed", c.path.IsConstantSpeed()},
                      {"keys", keys}}},
                    {"position", ToJson(c.position)},
                    {"angles", ToJson(c.angles)},
                    {"lens", LensJson(c.lens)},
                    {"useLensFov", c.useLensFov},
                    {"fov", c.horizontalFov},
                    {"target", TargetJson(c.target)},
                    {"orbit",
                     {{"distance", c.orbitDistance},
                      {"height", c.orbitHeight},
                      {"speed", c.orbitSpeed},
                      {"startAngle", c.orbitStartAngle},
                      {"shoulder", c.chaseShoulder}}},
                    {"shake",
                     {{"enabled", c.shake.enabled},
                      {"position", c.shake.amplitudePosition},
                      {"rotation", c.shake.amplitudeRotation},
                      {"frequency", c.shake.frequency},
                      {"seed", c.shake.seed}}}};
        }

        CameraDef CameraFrom(const json& j, std::vector<std::string>& warnings)
        {
            CameraDef c;
            c.id = Get<std::string>(j, "id", "");
            c.name = Get<std::string>(j, "name", c.id);
            const auto mode = Get<std::string>(j, "mode", "dolly");
            if (const auto m = CameraModeFromString(mode))
                c.mode = *m;
            else
                warnings.push_back("Camera '" + c.name + "' has an unknown mode '" + mode + "'; using dolly.");
            c.color = Get<std::string>(j, "color", c.color);
            if (const auto it = j.find("path"); it != j.end() && it->is_object())
            {
                c.path.SetRotationMode(RotationModeFromString(Get<std::string>(*it, "rotation", "quaternion"))
                                           .value_or(RotationMode::Quaternion));
                c.path.SetConstantSpeed(Get<bool>(*it, "constantSpeed", false));
                std::vector<CameraKey> keys;
                if (const auto k = it->find("keys"); k != it->end() && k->is_array())
                    for (const auto& key : *k)
                        if (key.is_object())
                            keys.push_back(CameraKeyFrom(key));
                c.path.SetKeys(std::move(keys));
            }
            c.position = Vec3From(j.value("position", json()));
            c.angles = AnglesFrom(j.value("angles", json()));
            c.lens = LensFrom(j.value("lens", json()));
            c.useLensFov = Get<bool>(j, "useLensFov", false);
            c.horizontalFov = Clamp(GetNumber(j, "fov", 90.0), 1.0, 179.0);
            c.target = TargetFrom(j.value("target", json()));
            if (const auto it = j.find("orbit"); it != j.end() && it->is_object())
            {
                c.orbitDistance = GetNumber(*it, "distance", c.orbitDistance);
                c.orbitHeight = GetNumber(*it, "height", c.orbitHeight);
                c.orbitSpeed = GetNumber(*it, "speed", c.orbitSpeed);
                c.orbitStartAngle = GetNumber(*it, "startAngle", c.orbitStartAngle);
                c.chaseShoulder = GetNumber(*it, "shoulder", c.chaseShoulder);
            }
            if (const auto it = j.find("shake"); it != j.end() && it->is_object())
            {
                c.shake.enabled = Get<bool>(*it, "enabled", false);
                c.shake.amplitudePosition = GetNumber(*it, "position", c.shake.amplitudePosition);
                c.shake.amplitudeRotation = GetNumber(*it, "rotation", c.shake.amplitudeRotation);
                c.shake.frequency = GetNumber(*it, "frequency", c.shake.frequency);
                c.shake.seed = Get<std::uint32_t>(*it, "seed", 1u);
            }
            return c;
        }

        json DemoRefJson(const DemoRef& d)
        {
            json j{{"path", d.path},   {"name", d.name},     {"map", d.mapName},          {"gametype", d.gametype},
                   {"author", d.author}, {"size", d.fileSize}, {"sceneStart", d.sceneStart}};
            if (d.firstServerTime)
                j["firstServerTime"] = *d.firstServerTime;
            if (d.durationSeconds)
                j["duration"] = *d.durationSeconds;
            if (d.killFeed)
            {
                json seen = json::array(), kills = json::array();
                for (const auto& span : d.killFeed->seen)
                    seen.push_back(json::array({span.first, span.last}));
                for (const auto& k : d.killFeed->kills)
                    kills.push_back(json::array({k.serverTime, k.killer, k.victim}));
                j["killFeed"] = {{"seen", seen}, {"kills", kills}};
            }
            return j;
        }

        DemoRef DemoRefFrom(const json& j)
        {
            DemoRef d;
            d.path = Get<std::string>(j, "path", "");
            d.name = Get<std::string>(j, "name", "");
            d.mapName = Get<std::string>(j, "map", "");
            d.gametype = Get<std::string>(j, "gametype", "");
            d.author = Get<std::string>(j, "author", "");
            d.fileSize = Get<std::uint64_t>(j, "size", 0);
            d.sceneStart = GetNumber(j, "sceneStart", 0.0);
            if (const auto it = j.find("firstServerTime"); it != j.end() && it->is_number_integer())
                d.firstServerTime = it->get<int>();
            if (const auto it = j.find("duration"); it != j.end() && it->is_number() && it->get<double>() >= 0.0)
                d.durationSeconds = it->get<double>();
            if (const auto it = j.find("killFeed"); it != j.end() && it->is_object())
            {
                KillFeed feed;
                if (const auto seen = it->find("seen"); seen != it->end() && seen->is_array())
                    for (const auto& span : *seen)
                        if (span.is_array() && span.size() == 2 && span[0].is_number_integer() &&
                            span[1].is_number_integer())
                            feed.seen.push_back({span[0].get<int>(), span[1].get<int>()});
                if (const auto kills = it->find("kills"); kills != it->end() && kills->is_array())
                    for (const auto& k : *kills)
                        if (k.is_array() && k.size() == 3 && k[0].is_number_integer() && k[1].is_string() &&
                            k[2].is_string())
                            feed.kills.push_back({k[0].get<int>(), k[1].get<std::string>(), k[2].get<std::string>()});
                std::sort(feed.seen.begin(), feed.seen.end(),
                          [](const ServerSpan& a, const ServerSpan& b) { return a.first < b.first; });
                std::stable_sort(feed.kills.begin(), feed.kills.end(),
                                 [](const FeedKill& a, const FeedKill& b) { return a.serverTime < b.serverTime; });
                d.killFeed = std::move(feed);
            }
            return d;
        }

        json RecordingJson(const RecordingSettings& s)
        {
            json passes = json::array();
            for (const auto& p : s.passes)
                passes.push_back({{"kind", ToString(p.kind)}, {"enabled", p.enabled}, {"background", p.backgroundColor}});
            return {{"format", ToString(s.format)},
                    {"useFfmpeg", s.useFfmpeg},
                    {"ffmpegPath", s.ffmpegPath},
                    {"outputDirectory", s.outputDirectory},
                    {"namePattern", s.namePattern},
                    {"width", s.width},
                    {"height", s.height},
                    {"fps", json::array({s.frameRate.numerator, s.frameRate.denominator})},
                    {"range", ToString(s.range)},
                    {"customStart", s.customStart},
                    {"customEnd", s.customEnd},
                    {"crf", s.crf},
                    {"preset", s.preset},
                    {"chroma444", s.chroma444},
                    {"jpegQuality", s.jpegQuality},
                    {"queueDepth", s.queueDepth},
                    {"warmupFrames", s.warmupFrames},
                    {"hideEditorOverlays", s.hideEditorOverlays},
                    {"includeAudio", s.includeAudio},
                    {"audioRange", json::array({s.audioRangeStart, s.audioRangeEnd})},
                    {"passes", passes}};
        }

        FrameRate FrameRateFrom(const json& j, FrameRate fallback)
        {
            if (j.is_array() && j.size() == 2 && j[0].is_number_integer() && j[1].is_number_integer())
            {
                FrameRate r{j[0].get<std::int64_t>(), j[1].get<std::int64_t>()};
                if (r.Valid())
                    return r;
            }
            if (j.is_number())
                return FrameRate::FromFps(j.get<double>());
            return fallback;
        }

        RecordingSettings RecordingFrom(const json& j, std::vector<std::string>& warnings)
        {
            RecordingSettings s;
            if (!j.is_object())
                return s;
            const auto format = Get<std::string>(j, "format", "png");
            if (const auto f = OutputFormatFromString(format))
                s.format = *f;
            else
                warnings.push_back("Unknown recording format '" + format + "'; using PNG sequence.");
            s.useFfmpeg = Get<bool>(j, "useFfmpeg", false);
            s.ffmpegPath = Get<std::string>(j, "ffmpegPath", "");
            s.outputDirectory = Get<std::string>(j, "outputDirectory", "");
            s.namePattern = Get<std::string>(j, "namePattern", s.namePattern);
            s.width = std::max(0, Get<int>(j, "width", 0));
            s.height = std::max(0, Get<int>(j, "height", 0));
            s.frameRate = FrameRateFrom(j.value("fps", json()), s.frameRate);
            s.range = RangeModeFromString(Get<std::string>(j, "range", "inOut")).value_or(RangeMode::InOut);
            s.customStart = GetNumber(j, "customStart", 0.0);
            s.customEnd = GetNumber(j, "customEnd", 5.0);
            s.crf = Get<int>(j, "crf", s.crf);
            s.preset = Get<std::string>(j, "preset", s.preset);
            s.chroma444 = Get<bool>(j, "chroma444", false);
            s.jpegQuality = Get<int>(j, "jpegQuality", s.jpegQuality);
            s.queueDepth = Get<int>(j, "queueDepth", s.queueDepth);
            s.warmupFrames = Get<int>(j, "warmupFrames", s.warmupFrames);
            s.hideEditorOverlays = Get<bool>(j, "hideEditorOverlays", true);
            s.includeAudio = Get<bool>(j, "includeAudio", true);
            if (const auto it = j.find("audioRange");
                it != j.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
            {
                s.audioRangeStart = (*it)[0].get<double>();
                s.audioRangeEnd = (*it)[1].get<double>();
            }
            if (const auto it = j.find("passes"); it != j.end() && it->is_array())
            {
                s.passes.clear();
                for (const auto& p : *it)
                {
                    RenderPass pass;
                    const auto kind = Get<std::string>(p, "kind", "beauty");
                    if (const auto k = PassKindFromString(kind))
                        pass.kind = *k;
                    else
                    {
                        warnings.push_back("Unknown render pass '" + kind + "' was dropped.");
                        continue;
                    }
                    pass.enabled = Get<bool>(p, "enabled", true);
                    pass.backgroundColor = Get<std::string>(p, "background", pass.backgroundColor);
                    s.passes.push_back(pass);
                }
                if (s.passes.empty())
                    s.passes.push_back(RenderPass{});
            }
            return s;
        }

        json VisualsJson(const VisualSettings& v)
        {
            return {{"film",
                     {{"override", v.filmOverride},
                      {"brightness", v.filmBrightness},
                      {"contrast", v.filmContrast},
                      {"desaturation", v.filmDesaturation},
                      {"lightTint", ToJson(v.filmLightTint)},
                      {"darkTint", ToJson(v.filmDarkTint)},
                      {"invert", v.filmInvert}}},
                    {"dof",
                     {{"override", v.dofOverride},
                      {"fromLens", v.dofFromLens},
                      {"nearStart", v.dofNearStart},
                      {"nearEnd", v.dofNearEnd},
                      {"farStart", v.dofFarStart},
                      {"farEnd", v.dofFarEnd},
                      {"nearBlur", v.dofNearBlur},
                      {"farBlur", v.dofFarBlur},
                      {"bias", v.dofBias}}},
                    {"sun",
                     {{"override", v.sunOverride},
                      {"color", ToJson(v.sunColor)},
                      {"intensity", v.sunIntensity},
                      {"angles", ToJson(v.sunAngles)}}},
                    {"fog",
                     {{"override", v.fogOverride},
                      {"start", v.fogStart},
                      {"halfDistance", v.fogHalfDistance},
                      {"color", ToJson(v.fogColor)},
                      {"opacity", v.fogOpacity}}},
                    {"hud",
                     {{"hideHud", v.hideHud},
                      {"hideGunInModCameras", v.hideGunInModCameras},
                      {"hideCrosshair", v.hideCrosshair},
                      {"hideKillfeed", v.hideKillfeed}}}};
        }

        VisualSettings VisualsFrom(const json& j)
        {
            VisualSettings v;
            if (!j.is_object())
                return v;
            if (const auto it = j.find("film"); it != j.end() && it->is_object())
            {
                v.filmOverride = Get<bool>(*it, "override", false);
                v.filmBrightness = GetNumber(*it, "brightness", v.filmBrightness);
                v.filmContrast = GetNumber(*it, "contrast", v.filmContrast);
                v.filmDesaturation = GetNumber(*it, "desaturation", v.filmDesaturation);
                v.filmLightTint = Vec3From(it->value("lightTint", json()), v.filmLightTint);
                v.filmDarkTint = Vec3From(it->value("darkTint", json()), v.filmDarkTint);
                v.filmInvert = Get<bool>(*it, "invert", false);
            }
            if (const auto it = j.find("dof"); it != j.end() && it->is_object())
            {
                v.dofOverride = Get<bool>(*it, "override", false);
                v.dofFromLens = Get<bool>(*it, "fromLens", true);
                v.dofNearStart = GetNumber(*it, "nearStart", v.dofNearStart);
                v.dofNearEnd = GetNumber(*it, "nearEnd", v.dofNearEnd);
                v.dofFarStart = GetNumber(*it, "farStart", v.dofFarStart);
                v.dofFarEnd = GetNumber(*it, "farEnd", v.dofFarEnd);
                v.dofNearBlur = GetNumber(*it, "nearBlur", v.dofNearBlur);
                v.dofFarBlur = GetNumber(*it, "farBlur", v.dofFarBlur);
                v.dofBias = GetNumber(*it, "bias", v.dofBias);
            }
            if (const auto it = j.find("sun"); it != j.end() && it->is_object())
            {
                v.sunOverride = Get<bool>(*it, "override", false);
                v.sunColor = Vec3From(it->value("color", json()), v.sunColor);
                v.sunIntensity = GetNumber(*it, "intensity", v.sunIntensity);
                v.sunAngles = AnglesFrom(it->value("angles", json()), v.sunAngles);
            }
            if (const auto it = j.find("fog"); it != j.end() && it->is_object())
            {
                v.fogOverride = Get<bool>(*it, "override", false);
                v.fogStart = GetNumber(*it, "start", v.fogStart);
                v.fogHalfDistance = GetNumber(*it, "halfDistance", v.fogHalfDistance);
                v.fogColor = Vec3From(it->value("color", json()), v.fogColor);
                v.fogOpacity = GetNumber(*it, "opacity", v.fogOpacity);
            }
            if (const auto it = j.find("hud"); it != j.end() && it->is_object())
            {
                v.hideHud = Get<bool>(*it, "hideHud", v.hideHud);
                v.hideGunInModCameras = Get<bool>(*it, "hideGunInModCameras", v.hideGunInModCameras);
                v.hideCrosshair = Get<bool>(*it, "hideCrosshair", v.hideCrosshair);
                v.hideKillfeed = Get<bool>(*it, "hideKillfeed", v.hideKillfeed);
            }
            return v;
        }

        // Migrations. Each step upgrades a document by one schema version.
        //
        // Version 0 was the pre-release layout of the IW4Iris prototypes:
        // "version" instead of "schemaVersion", times in milliseconds
        // ("tickMs"), a single top-level "campath" array and "fps" as a plain
        // number. Kept so early project files still open.
        void MigrateV0ToV1(json& doc)
        {
            json cameras = json::array();
            if (const auto it = doc.find("campath"); it != doc.end() && it->is_array())
            {
                json keys = json::array();
                for (const auto& k : *it)
                {
                    if (!k.is_object())
                        continue;
                    json key{{"t", GetNumber(k, "tickMs", 0.0) / 1000.0},
                             {"pos", k.value("position", json::array({0, 0, 0}))},
                             {"ang", k.value("angles", json::array({0, 0, 0}))},
                             {"fov", GetNumber(k, "fov", 90.0)},
                             {"interp", "smooth"}};
                    keys.push_back(key);
                }
                cameras.push_back({{"id", "cam-1"},
                                   {"name", "Dolly 1"},
                                   {"mode", "dolly"},
                                   {"path", {{"rotation", "euler"}, {"constantSpeed", false}, {"keys", keys}}}});
                doc.erase("campath");
            }
            doc["cameras"] = cameras;
            doc["activeCamera"] = cameras.empty() ? "" : "cam-1";
            if (const auto it = doc.find("fps"); it != doc.end() && it->is_number())
            {
                const auto rate = FrameRate::FromFps(it->get<double>());
                doc["timeline"] = {{"fps", json::array({rate.numerator, rate.denominator})}};
                doc.erase("fps");
            }
            doc.erase("version");
            doc["schemaVersion"] = 1;
        }
    }  // namespace

    std::string SerializeProject(const Project& p, bool pretty)
    {
        json cameras = json::array();
        for (const auto& c : p.cameras)
            cameras.push_back(CameraJson(c));

        json cuts = json::array();
        for (const auto& c : p.cuts.Cuts())
        {
            json cut{{"t", c.time}, {"camera", c.cameraId}};
            if (!c.angle.empty())
                cut["angle"] = c.angle;
            cuts.push_back(cut);
        }

        json angles = json::array();
        for (const auto& a : p.angles)
            angles.push_back({{"id", a.id},
                              {"name", a.name},
                              {"demo", DemoRefJson(a.demo)},
                              {"recorder", a.recorder},
                              {"offsetMs", a.offsetMs},
                              {"sync", ToString(a.sync)},
                              {"confidence", a.confidence}});

        json markers = json::array();
        for (const auto& m : p.markers)
            markers.push_back({{"id", m.id}, {"t", m.time}, {"name", m.name}, {"color", m.color}, {"kind", m.kind}});

        json tracks = json::array();
        for (const auto& t : p.tracks)
            tracks.push_back({{"property", t.property}, {"enabled", t.enabled}, {"curve", CurveJson(t.curve)}});

        json trackStates = json::object();
        for (const auto& [id, s] : p.trackStates)
            trackStates[id] = {{"locked", s.locked}, {"hidden", s.hidden}, {"collapsed", s.collapsed}, {"group", s.group}};

        json lights = json::array();
        for (const auto& l : p.lights)
            lights.push_back({{"id", l.id},
                              {"name", l.name},
                              {"kind", l.kind},
                              {"position", ToJson(l.position)},
                              {"angles", ToJson(l.angles)},
                              {"color", ToJson(l.color)},
                              {"intensity", l.intensity},
                              {"radius", l.radius},
                              {"coneAngle", l.coneAngle},
                              {"coneSoftness", l.coneSoftness},
                              {"temperature", l.temperatureK}});

        json queue = json::array();
        for (const auto& job : p.renderQueue)
            queue.push_back({{"id", job.id},
                             {"name", job.name},
                             {"camera", job.cameraId},
                             {"angle", job.angle},
                             {"status", job.status},
                             {"message", job.message},
                             {"settings", RecordingJson(job.settings)}});

        json doc{{"format", kProjectFormatName},
                 {"schemaVersion", kProjectSchemaVersion},
                 {"generator", p.generator.empty() ? std::string(kVersionString) : p.generator},
                 {"name", p.name},
                 {"created", p.createdUtc},
                 {"modified", p.modifiedUtc},
                 {"map", p.mapName},
                 {"timeline",
                  {{"fps", json::array({p.timeline.frameRate.numerator, p.timeline.frameRate.denominator})},
                   {"start", p.timeline.start},
                   {"end", p.timeline.end},
                   {"in", p.timeline.inPoint},
                   {"out", p.timeline.outPoint},
                   {"loop", {{"enabled", p.timeline.loop.enabled}, {"start", p.timeline.loop.start}, {"end", p.timeline.loop.end}}},
                   {"display", p.timeline.display == TimeDisplay::Seconds ? "seconds"
                               : p.timeline.display == TimeDisplay::Frames ? "frames"
                                                                           : "timecode"},
                   {"snap", p.timeline.snapToFrames}}},
                 {"timeRemap",
                  {{"timelineStart", p.timeRemap.timelineStart},
                   {"sceneOffset", p.timeRemap.sceneOffset},
                   {"speed", CurveJson(p.timeRemap.Speed())}}},
                 {"cameras", cameras},
                 {"activeCamera", p.activeCameraId},
                 {"useCameraCuts", p.useCameraCuts},
                 {"cuts", cuts},
                 {"markers", markers},
                 {"tracks", tracks},
                 {"trackStates", trackStates},
                 {"visuals", VisualsJson(p.visuals)},
                 {"lights", lights},
                 {"scene", detail::SceneToJson(p.scene)},
                 {"recording", RecordingJson(p.recording)},
                 {"renderQueue", queue},
                 {"export",
                  {{"unitsPerMetre", p.exports.unitsPerMetre},
                   {"blenderKeysOnly", p.exports.blenderIncludeKeysOnly},
                   {"aeUnitsPerPixel", p.exports.aeUnitsPerPixel}}},
                 {"editor", {{"layout", p.editorLayout}, {"workspace", p.workspace}}}};
        if (p.demo)
            doc["demo"] = DemoRefJson(*p.demo);
        if (p.audio)
        {
            const auto& a = *p.audio;
            doc["audio"] = {{"path", a.path},          {"name", a.name},
                            {"size", a.fileSize},      {"start", a.start},
                            {"trimIn", a.trimIn},      {"trimOut", a.trimOut},
                            {"duration", a.fileDuration}, {"volume", a.volume},
                            {"muted", a.muted},        {"bpm", a.bpm},
                            {"tempoConfidence", a.tempoConfidence},
                            {"beats", a.beats},        {"beatsEdited", a.beatsEdited}};
        }
        if (!p.angles.empty())
            doc["angles"] = angles;
        return pretty ? doc.dump(2) : doc.dump();
    }

    LoadResult ParseProject(std::string_view text)
    {
        LoadResult result;
        json doc;
        try
        {
            doc = json::parse(text.begin(), text.end());
        }
        catch (const json::parse_error& e)
        {
            result.error = std::string("The project file is not valid JSON: ") + e.what();
            return result;
        }
        if (!doc.is_object())
        {
            result.error = "The project file does not contain a JSON object.";
            return result;
        }

        int version = 0;
        if (doc.contains("schemaVersion") && doc["schemaVersion"].is_number_integer())
            version = doc["schemaVersion"].get<int>();
        else if (doc.contains("version") && doc["version"].is_number_integer())
            version = doc["version"].get<int>();
        else
        {
            result.error = "The file has no schema version; it is not an IW4Iris project.";
            return result;
        }
        if (const auto fmt = doc.find("format");
            fmt != doc.end() && (!fmt->is_string() || fmt->get<std::string>() != kProjectFormatName))
        {
            result.error = "The file is not an IW4Iris project (format '" + doc["format"].dump() + "').";
            return result;
        }
        result.originalSchemaVersion = version;
        if (version > kProjectSchemaVersion)
        {
            result.error = "The project was made by a newer IW4Iris (schema " + std::to_string(version) +
                           "); this version reads up to schema " + std::to_string(kProjectSchemaVersion) + ".";
            return result;
        }
        if (version < 0)
        {
            result.error = "Invalid schema version.";
            return result;
        }
        if (version == 0)
        {
            MigrateV0ToV1(doc);
            version = 1;
            result.migrated = true;
            result.warnings.push_back("The project was upgraded from schema 0 to schema 1.");
        }
        if (version == 1)
        {
            // Schema 2 adds the Director scene (actors, props, effects,
            // graphics); schema 1 actors are converted below.
            version = 2;
            result.migrated = true;
        }

        Project p;
        auto& w = result.warnings;
        p.schemaVersion = kProjectSchemaVersion;
        p.name = Get<std::string>(doc, "name", "Untitled");
        p.generator = Get<std::string>(doc, "generator", "");
        p.createdUtc = Get<std::string>(doc, "created", "");
        p.modifiedUtc = Get<std::string>(doc, "modified", "");
        p.mapName = Get<std::string>(doc, "map", "");

        if (const auto it = doc.find("demo"); it != doc.end() && it->is_object())
            p.demo = DemoRefFrom(*it);
        if (const auto it = doc.find("audio"); it != doc.end() && it->is_object())
        {
            AudioTrack a;
            a.path = Get<std::string>(*it, "path", "");
            a.name = Get<std::string>(*it, "name", "");
            a.fileSize = Get<std::uint64_t>(*it, "size", 0);
            a.start = GetNumber(*it, "start", 0.0);
            a.trimIn = std::max(0.0, GetNumber(*it, "trimIn", 0.0));
            a.trimOut = std::max(0.0, GetNumber(*it, "trimOut", 0.0));
            a.fileDuration = std::max(0.0, GetNumber(*it, "duration", 0.0));
            a.volume = std::clamp(GetNumber(*it, "volume", 1.0), 0.0, 4.0);
            a.muted = Get<bool>(*it, "muted", false);
            a.bpm = std::max(0.0, GetNumber(*it, "bpm", 0.0));
            a.tempoConfidence = std::clamp(GetNumber(*it, "tempoConfidence", 0.0), 0.0, 1.0);
            if (const auto beats = it->find("beats"); beats != it->end() && beats->is_array())
                for (const auto& b : *beats)
                    if (b.is_number() && std::isfinite(b.get<double>()) && b.get<double>() >= 0.0)
                        a.beats.push_back(b.get<double>());
            std::sort(a.beats.begin(), a.beats.end());
            a.beatsEdited = Get<bool>(*it, "beatsEdited", false);
            if (!a.path.empty())
                p.audio = std::move(a);
        }
        if (const auto it = doc.find("angles"); it != doc.end() && it->is_array())
            for (const auto& j : *it)
            {
                if (!j.is_object())
                    continue;
                DemoAngle a;
                a.id = Get<std::string>(j, "id", "");
                a.name = Get<std::string>(j, "name", "");
                if (const auto d = j.find("demo"); d != j.end() && d->is_object())
                    a.demo = DemoRefFrom(*d);
                a.recorder = Get<std::string>(j, "recorder", "");
                a.offsetMs = Get<int>(j, "offsetMs", 0);
                const auto sync = Get<std::string>(j, "sync", "unsynced");
                if (const auto st = SyncStateFromString(sync))
                    a.sync = *st;
                else
                    w.push_back("Angle '" + a.name + "' has an unknown sync state '" + sync +
                                "'; it is marked not synchronised.");
                a.confidence = std::clamp(GetNumber(j, "confidence", 0.0), 0.0, 1.0);
                if (a.id.empty() || p.FindAngle(a.id))
                    a.id = MakeUniqueId(p, "angle");
                if (a.name.empty())
                    a.name = a.demo.name.empty() ? a.id : a.demo.name;
                p.angles.push_back(std::move(a));
            }

        if (const auto it = doc.find("timeline"); it != doc.end() && it->is_object())
        {
            auto& t = p.timeline;
            t.frameRate = FrameRateFrom(it->value("fps", json()), t.frameRate);
            t.start = GetNumber(*it, "start", t.start);
            t.end = GetNumber(*it, "end", t.end);
            t.inPoint = GetNumber(*it, "in", t.start);
            t.outPoint = GetNumber(*it, "out", t.end);
            if (const auto l = it->find("loop"); l != it->end() && l->is_object())
            {
                t.loop.enabled = Get<bool>(*l, "enabled", false);
                t.loop.start = GetNumber(*l, "start", 0.0);
                t.loop.end = GetNumber(*l, "end", 0.0);
            }
            const auto display = Get<std::string>(*it, "display", "timecode");
            t.display = display == "seconds" ? TimeDisplay::Seconds
                        : display == "frames" ? TimeDisplay::Frames
                                              : TimeDisplay::Timecode;
            t.snapToFrames = Get<bool>(*it, "snap", true);
            t.Normalize();
        }

        if (const auto it = doc.find("timeRemap"); it != doc.end() && it->is_object())
        {
            p.timeRemap.timelineStart = GetNumber(*it, "timelineStart", 0.0);
            p.timeRemap.sceneOffset = GetNumber(*it, "sceneOffset", 0.0);
            p.timeRemap.Speed() = CurveFrom(it->value("speed", json()), 1.0);
        }

        std::set<std::string> ids;
        if (const auto it = doc.find("cameras"); it != doc.end() && it->is_array())
        {
            for (const auto& c : *it)
            {
                if (!c.is_object())
                    continue;
                CameraDef cam = CameraFrom(c, w);
                if (cam.id.empty() || ids.contains(cam.id))
                {
                    const std::string old = cam.id;
                    cam.id = MakeUniqueId(p, "cam");
                    w.push_back("Camera '" + cam.name + "' had a missing or duplicate id '" + old + "'; renamed to '" +
                                cam.id + "'.");
                }
                ids.insert(cam.id);
                p.cameras.push_back(std::move(cam));
            }
        }
        p.activeCameraId = Get<std::string>(doc, "activeCamera", "");
        if (!p.FindCamera(p.activeCameraId) && !p.cameras.empty())
            p.activeCameraId = p.cameras.front().id;
        p.useCameraCuts = Get<bool>(doc, "useCameraCuts", false);

        if (const auto it = doc.find("cuts"); it != doc.end() && it->is_array())
        {
            std::vector<CameraCut> cuts;
            for (const auto& c : *it)
            {
                CameraCut cut{GetNumber(c, "t", 0.0), Get<std::string>(c, "camera", ""), Get<std::string>(c, "angle", "")};
                if (!p.FindCamera(cut.cameraId))
                {
                    w.push_back("A camera cut refers to a camera '" + cut.cameraId + "' that does not exist; it was dropped.");
                    continue;
                }
                cuts.push_back(cut);
            }
            p.cuts.SetCuts(std::move(cuts));
        }

        if (const auto it = doc.find("markers"); it != doc.end() && it->is_array())
            for (const auto& m : *it)
                if (m.is_object())
                    p.markers.push_back({Get<std::string>(m, "id", ""), GetNumber(m, "t", 0.0), Get<std::string>(m, "name", ""),
                                         Get<std::string>(m, "color", "#8B7CF6"), Get<std::string>(m, "kind", "note")});
        for (auto& m : p.markers)
            if (m.id.empty())
                m.id = MakeUniqueId(p, "marker");

        if (const auto it = doc.find("tracks"); it != doc.end() && it->is_array())
            for (const auto& t : *it)
                if (t.is_object() && t.contains("property"))
                {
                    PropertyTrack track;
                    track.property = Get<std::string>(t, "property", "");
                    track.enabled = Get<bool>(t, "enabled", true);
                    track.curve = CurveFrom(t.value("curve", json()), 0.0);
                    if (!track.property.empty() && !p.FindTrack(track.property))
                        p.tracks.push_back(std::move(track));
                }

        if (const auto it = doc.find("trackStates"); it != doc.end() && it->is_object())
            for (const auto& [id, s] : it->items())
                if (s.is_object())
                    p.trackStates[id] = {Get<bool>(s, "locked", false), Get<bool>(s, "hidden", false),
                                         Get<bool>(s, "collapsed", false), Get<std::string>(s, "group", "")};

        p.visuals = VisualsFrom(doc.value("visuals", json()));

        if (const auto it = doc.find("lights"); it != doc.end() && it->is_array())
            for (const auto& l : *it)
                if (l.is_object())
                {
                    LightDef d;
                    d.id = Get<std::string>(l, "id", "");
                    d.name = Get<std::string>(l, "name", d.id);
                    d.kind = Get<std::string>(l, "kind", "point");
                    d.position = Vec3From(l.value("position", json()));
                    d.angles = AnglesFrom(l.value("angles", json()));
                    d.color = Vec3From(l.value("color", json()), d.color);
                    d.intensity = GetNumber(l, "intensity", d.intensity);
                    d.radius = GetNumber(l, "radius", d.radius);
                    d.coneAngle = GetNumber(l, "coneAngle", d.coneAngle);
                    d.coneSoftness = GetNumber(l, "coneSoftness", d.coneSoftness);
                    d.temperatureK = GetNumber(l, "temperature", d.temperatureK);
                    p.lights.push_back(d);
                }

        if (const auto it = doc.find("scene"); it != doc.end() && it->is_object())
            p.scene = detail::SceneFromJson(*it, w);
        // Schema 1 stored placeholder actors (one model, animation and
        // weapon each) at the top level.
        if (const auto it = doc.find("actors"); it != doc.end() && it->is_array())
            for (const auto& a : *it)
                if (a.is_object())
                {
                    ActorDef d;
                    d.id = Get<std::string>(a, "id", "");
                    d.name = Get<std::string>(a, "name", d.id);
                    d.bodyModel = Get<std::string>(a, "body", "");
                    d.headModel = Get<std::string>(a, "head", "");
                    d.idleAnim = Get<std::string>(a, "animation", "");
                    d.weapon = Get<std::string>(a, "weapon", "");
                    d.transform.position = Vec3From(a.value("position", json()));
                    d.transform.angles = AnglesFrom(a.value("angles", json()));
                    d.visible = Get<bool>(a, "visible", true);
                    if (d.id.empty() || p.scene.Contains(d.id))
                        d.id = MakeUniqueId(p, "actor");
                    p.scene.actors.push_back(std::move(d));
                }

        p.recording = RecordingFrom(doc.value("recording", json()), w);
        if (const auto it = doc.find("renderQueue"); it != doc.end() && it->is_array())
            for (const auto& j : *it)
                if (j.is_object())
                {
                    RenderJob job;
                    job.id = Get<std::string>(j, "id", "");
                    job.name = Get<std::string>(j, "name", job.id);
                    job.cameraId = Get<std::string>(j, "camera", "");
                    job.angle = Get<std::string>(j, "angle", "");
                    job.status = Get<std::string>(j, "status", "queued");
                    job.message = Get<std::string>(j, "message", "");
                    job.settings = RecordingFrom(j.value("settings", json()), w);
                    // A render that was interrupted by a crash is queued again.
                    if (job.status == "rendering")
                        job.status = "queued";
                    p.renderQueue.push_back(job);
                }

        if (const auto it = doc.find("export"); it != doc.end() && it->is_object())
        {
            p.exports.unitsPerMetre = std::max(1e-6, GetNumber(*it, "unitsPerMetre", p.exports.unitsPerMetre));
            p.exports.blenderIncludeKeysOnly = Get<bool>(*it, "blenderKeysOnly", false);
            p.exports.aeUnitsPerPixel = std::max(1e-6, GetNumber(*it, "aeUnitsPerPixel", 1.0));
        }
        if (const auto it = doc.find("editor"); it != doc.end() && it->is_object())
        {
            p.editorLayout = Get<std::string>(*it, "layout", "");
            p.workspace = Get<std::string>(*it, "workspace", "Cinematic");
        }

        result.project = std::move(p);
        return result;
    }

    bool SaveProjectFile(const std::filesystem::path& file, Project project, std::string* error)
    {
        namespace fs = std::filesystem;
        project.modifiedUtc = CurrentUtcTimestamp();
        if (project.createdUtc.empty())
            project.createdUtc = project.modifiedUtc;
        if (project.generator.empty())
            project.generator = std::string(kVersionString);

        // Store the demo relative to the project's folder when it is inside it
        // or close by, so projects can be moved with their demos.
        auto relativise = [&](DemoRef& ref) {
            if (ref.path.empty())
                return;
            std::error_code ec;
            const fs::path demo(ref.path);
            if (demo.is_absolute())
            {
                const auto rel = fs::relative(demo, fs::absolute(file, ec).parent_path(), ec);
                if (!ec && !rel.empty() && rel.native().find(fs::path("..").native() + fs::path("..").native()) == fs::path::string_type::npos)
                    ref.path = rel.generic_string();
            }
        };
        if (project.demo)
            relativise(*project.demo);
        if (project.audio && !project.audio->path.empty())
        {
            DemoRef music;  // the same rule for the music file
            music.path = project.audio->path;
            relativise(music);
            project.audio->path = music.path;
        }
        for (auto& a : project.angles)
            relativise(a.demo);

        const std::string text = SerializeProject(project, true);
        std::error_code ec;
        if (file.has_parent_path())
            fs::create_directories(file.parent_path(), ec);
        fs::path tmp = file;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                if (error)
                    *error = "Cannot write " + tmp.string();
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.flush();
            if (!out)
            {
                if (error)
                    *error = "Writing " + tmp.string() + " failed (disk full?)";
                out.close();
                fs::remove(tmp, ec);
                return false;
            }
        }
        fs::rename(tmp, file, ec);
        if (ec)
        {
            // rename over an existing file can fail on some file systems
            fs::remove(file, ec);
            ec.clear();
            fs::rename(tmp, file, ec);
            if (ec)
            {
                if (error)
                    *error = "Cannot replace " + file.string() + ": " + ec.message();
                return false;
            }
        }
        return true;
    }

    LoadResult LoadProjectFile(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            LoadResult r;
            r.error = "Cannot open " + file.string();
            return r;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ParseProject(ss.str());
    }

    std::optional<std::filesystem::path> ResolveDemoPath(const Project& project, const std::filesystem::path& file)
    {
        if (!project.demo)
            return std::nullopt;
        return ResolveDemoRefPath(*project.demo, file);
    }

    std::optional<std::filesystem::path> ResolveAudioPath(const Project& project, const std::filesystem::path& file)
    {
        if (!project.audio)
            return std::nullopt;
        DemoRef music;
        music.path = project.audio->path;
        return ResolveDemoRefPath(music, file);
    }

    std::optional<std::filesystem::path> ResolveDemoRefPath(const DemoRef& demo, const std::filesystem::path& file)
    {
        if (demo.path.empty())
            return std::nullopt;
        std::filesystem::path p(demo.path);
        if (p.is_relative())
        {
            std::error_code ec;
            p = std::filesystem::absolute(file, ec).parent_path() / p;
        }
        return p.lexically_normal();
    }

    std::filesystem::path AutosavePathFor(const std::filesystem::path& projectFile)
    {
        auto p = projectFile;
        p += ".autosave";
        return p;
    }

    bool IsAutosaveNewer(const std::filesystem::path& projectFile)
    {
        std::error_code ec;
        const auto autosave = AutosavePathFor(projectFile);
        if (!std::filesystem::exists(autosave, ec))
            return false;
        if (!std::filesystem::exists(projectFile, ec))
            return true;
        return std::filesystem::last_write_time(autosave, ec) > std::filesystem::last_write_time(projectFile, ec);
    }

    std::vector<std::string> PushRecent(std::vector<std::string> recent, const std::string& path, std::size_t cap)
    {
        recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
        recent.insert(recent.begin(), path);
        if (recent.size() > cap)
            recent.resize(cap);
        return recent;
    }

    std::optional<CameraPath> ImportIwxmvmCampath(std::string_view text, std::string* error, bool absoluteTicks)
    {
        auto fail = [&](std::string message) -> std::optional<CameraPath> {
            if (error)
                *error = std::move(message);
            return std::nullopt;
        };
        json doc;
        try
        {
            doc = json::parse(text.begin(), text.end());
        }
        catch (const json::parse_error& e)
        {
            return fail(std::string("Not valid JSON: ") + e.what());
        }
        const auto props = doc.find("properties");
        if (props == doc.end() || !props->is_array())
            return fail("No 'properties' array; not an IWXMVM keyframe file.");

        for (const auto& prop : *props)
        {
            const auto name = Get<std::string>(prop, "property", "");
            if (name.find("Campath") == std::string::npos && name.find("campath") == std::string::npos)
                continue;
            const auto keys = prop.find("keyframes");
            if (keys == prop.end() || !keys->is_array())
                continue;
            std::vector<CameraKey> out;
            double firstTick = 0.0;
            bool first = true;
            for (const auto& k : *keys)
            {
                const double tick = GetNumber(k, "tick", 0.0);
                const auto value = k.find("value");
                if (value == k.end() || !value->is_object())
                    continue;
                const auto values = value->find("values");
                if (values == value->end() || !values->is_array() || values->size() < 7)
                    continue;
                bool numeric = true;
                for (const auto& v : *values)
                    numeric = numeric && v.is_number();
                if (!numeric)
                    continue;
                if (first)
                {
                    firstTick = tick;
                    first = false;
                }
                CameraKey key;
                key.time = (absoluteTicks ? tick : tick - firstTick) / 1000.0;
                key.position = {(*values)[0].get<double>(), (*values)[1].get<double>(), (*values)[2].get<double>()};
                key.angles = {(*values)[3].get<double>(), (*values)[4].get<double>(), (*values)[5].get<double>()};
                key.horizontalFov = Clamp((*values)[6].get<double>(), 1.0, 179.0);
                out.push_back(key);
            }
            if (out.empty())
                return fail("The campath property has no usable keyframes.");
            CameraPath path;
            // IWXMVM interpolates Euler angles per channel.
            path.SetRotationMode(RotationMode::Euler);
            path.SetKeys(std::move(out));
            return path;
        }
        return fail("No campath property found.");
    }

    std::vector<std::string> FindMissingReferences(const Project& project, const std::filesystem::path& projectFile)
    {
        std::vector<std::string> missing;
        std::error_code ec;
        if (const auto demo = ResolveDemoPath(project, projectFile))
        {
            if (!std::filesystem::exists(*demo, ec))
                missing.push_back("Demo file not found: " + demo->string());
            else if (project.demo->fileSize != 0 && std::filesystem::file_size(*demo, ec) != project.demo->fileSize)
                missing.push_back("Demo file differs from the one the project was made with (size changed): " +
                                  demo->string());
        }
        if (const auto music = ResolveAudioPath(project, projectFile))
        {
            if (!std::filesystem::exists(*music, ec))
                missing.push_back("Music file not found: " + music->string());
            else if (project.audio->fileSize != 0 && std::filesystem::file_size(*music, ec) != project.audio->fileSize)
                missing.push_back("Music file differs from the one the beats were found in (size changed): " +
                                  music->string());
        }
        for (const auto& a : project.angles)
            if (const auto demo = ResolveDemoRefPath(a.demo, projectFile))
            {
                if (!std::filesystem::exists(*demo, ec))
                    missing.push_back("Demo of angle '" + a.name + "' not found: " + demo->string());
                else if (a.demo.fileSize != 0 && std::filesystem::file_size(*demo, ec) != a.demo.fileSize)
                    missing.push_back("Demo of angle '" + a.name +
                                      "' differs from the one it was synchronised with (size changed): " + demo->string());
            }
        for (const auto& c : project.cuts.Cuts())
        {
            if (!project.FindCamera(c.cameraId))
                missing.push_back("Camera cut to unknown camera '" + c.cameraId + "'.");
            if (!c.angle.empty() && !project.FindAngle(c.angle))
                missing.push_back("Camera cut to an angle that no longer exists ('" + c.angle + "').");
        }
        return missing;
    }
}  // namespace iris
