// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/exporters.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

#include <nlohmann/json.hpp>

#include "iris/version.hpp"

namespace iris
{
    namespace
    {
        std::string Num(double v)
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.6f", std::isfinite(v) ? v : 0.0);
            // trim trailing zeros for compact output
            std::string s(buf);
            if (s.find('.') != std::string::npos)
            {
                while (!s.empty() && s.back() == '0')
                    s.pop_back();
                if (!s.empty() && s.back() == '.')
                    s.pop_back();
            }
            if (s == "-0")
                s = "0";
            return s;
        }

        std::string JsString(const std::string& s)
        {
            return nlohmann::json(s).dump();
        }
    }  // namespace

    BlenderTransform ToBlender(const CameraState& state, double sensorWidthMm, double unitsPerMetre)
    {
        BlenderTransform b;
        b.location = state.position / unitsPerMetre;
        const Mat3 axis = AnglesToAxis(state.angles);
        Mat3 m;
        m.col[0] = -axis.col[1];  // camera local +X (right)
        m.col[1] = axis.col[2];   // camera local +Y (up)
        m.col[2] = -axis.col[0];  // camera local +Z (backwards)
        b.rotation = FromMatrix(m);
        b.lensMm = HorizontalFovToFocalLength(state.horizontalFov, sensorWidthMm);
        b.focusDistanceM = state.focusDistance / unitsPerMetre;
        b.fNumber = state.fNumber;
        return b;
    }

    CameraState FromBlender(const BlenderTransform& b, double sensorWidthMm, double unitsPerMetre)
    {
        CameraState s;
        s.position = b.location * unitsPerMetre;
        const Mat3 m = ToMatrix(b.rotation);
        Mat3 axis;
        axis.col[0] = -m.col[2];
        axis.col[1] = -m.col[0];
        axis.col[2] = m.col[1];
        s.angles = AxisToAngles(axis);
        s.orientation = AnglesToQuat(s.angles);
        s.horizontalFov = FocalLengthToHorizontalFov(b.lensMm, sensorWidthMm);
        s.focusDistance = b.focusDistanceM * unitsPerMetre;
        s.fNumber = b.fNumber;
        return s;
    }

    std::string ExportBlenderJson(const ExportInfo& info, const std::vector<CameraSample>& samples)
    {
        nlohmann::json frames = nlohmann::json::array();
        Quat previous;
        bool first = true;
        for (const auto& s : samples)
        {
            BlenderTransform b = ToBlender(s.state, info.sensorWidthMm, info.unitsPerMetre);
            // Keep consecutive quaternions on the same hemisphere so Blender's
            // f-curves don't flip sign between frames.
            if (!first && Dot(previous, b.rotation) < 0.0)
                b.rotation = -b.rotation;
            previous = b.rotation;
            first = false;
            frames.push_back({{"frame", s.frame},
                              {"time", s.time},
                              {"location", {b.location.x, b.location.y, b.location.z}},
                              {"rotation_quaternion", {b.rotation.w, b.rotation.x, b.rotation.y, b.rotation.z}},
                              {"lens", b.lensMm},
                              {"focus_distance", b.focusDistanceM},
                              {"fstop", b.fNumber},
                              {"iw4", {{"origin", {s.state.position.x, s.state.position.y, s.state.position.z}},
                                       {"angles", {s.state.angles.pitch, s.state.angles.yaw, s.state.angles.roll}},
                                       {"hfov", s.state.horizontalFov}}}});
        }
        nlohmann::json doc{{"format", "iw4iris-camera"},
                           {"version", 1},
                           {"generator", std::string(kVersionString)},
                           {"name", info.name},
                           {"project", info.projectName},
                           {"map", info.mapName},
                           {"fps", {info.frameRate.numerator, info.frameRate.denominator}},
                           {"resolution", {info.width, info.height}},
                           {"sensor_width", info.sensorWidthMm},
                           {"sensor_fit", "HORIZONTAL"},
                           {"units_per_metre", info.unitsPerMetre},
                           {"coordinate_system",
                            "Blender world = IW4 world (X forward, Y left, Z up) scaled to metres; rotation is the "
                            "Blender camera (looks down local -Z)"},
                           {"frame_start", samples.empty() ? 0 : samples.front().frame},
                           {"frame_end", samples.empty() ? 0 : samples.back().frame},
                           {"frames", frames}};
        return doc.dump(1);
    }

    Vec3 ToAeSpace(const Vec3& iw, double unitsPerPixel)
    {
        return Vec3{iw.x, -iw.z, iw.y} / unitsPerPixel;
    }

    AeTransform ToAfterEffects(const CameraState& state, int compWidth, double unitsPerPixel)
    {
        AeTransform t;
        const Mat3 axis = AnglesToAxis(state.angles);
        const double poiDistance = std::max(state.focusDistance, 100.0);
        t.position = ToAeSpace(state.position, unitsPerPixel);
        t.pointOfInterest = ToAeSpace(state.position + axis.col[0] * poiDistance, unitsPerPixel);

        // Camera axes in AE space. AE cameras look along local +Z with +X
        // right and +Y down.
        const Vec3 forward = Normalize(ToAeSpace(axis.col[0], 1.0));
        const Vec3 right = Normalize(ToAeSpace(-axis.col[1], 1.0));
        // The orientation AE's auto-orient gives a camera looking at its point
        // of interest: down is world down (+Y) made perpendicular to forward.
        const Vec3 worldDown{0, 1, 0};
        Vec3 down0 = worldDown - forward * Dot(worldDown, forward);
        if (Length(down0) < 1e-9)
            down0 = Vec3{0, 0, 1} - forward * Dot(Vec3{0, 0, 1}, forward);
        down0 = Normalize(down0);
        const Vec3 right0 = Normalize(Cross(down0, forward));
        // Roll: the right-handed angle about +forward from right0 to right.
        t.zRotation = std::atan2(Dot(Cross(right0, right), forward), Dot(right0, right)) * kRadToDeg;
        const double half = Clamp(state.horizontalFov, 1.0, 179.0) * 0.5 * kDegToRad;
        t.zoom = (compWidth * 0.5) / std::tan(half);
        t.focusDistance = state.focusDistance / unitsPerPixel;
        return t;
    }

    std::string ExportAfterEffectsJsx(const ExportInfo& info, const std::vector<CameraSample>& samples)
    {
        std::ostringstream times, pos, poi, rot, zoom, focus;
        const double fps = info.frameRate.Fps();
        const double t0 = samples.empty() ? 0.0 : static_cast<double>(samples.front().frame) / fps;
        double previousRoll = 0.0;
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto& s = samples[i];
            const auto ae = ToAfterEffects(s.state, info.width, info.unitsPerPixel);
            // Comp time of frame k is exactly k / fps from the first frame.
            const double t = static_cast<double>(s.frame) / fps - t0;
            const double roll = i == 0 ? ae.zRotation : UnwrapDegreesNear(ae.zRotation, previousRoll);
            previousRoll = roll;
            const char* sep = i == 0 ? "" : ",";
            times << sep << Num(t);
            pos << sep << "[" << Num(ae.position.x) << "," << Num(ae.position.y) << "," << Num(ae.position.z) << "]";
            poi << sep << "[" << Num(ae.pointOfInterest.x) << "," << Num(ae.pointOfInterest.y) << ","
                << Num(ae.pointOfInterest.z) << "]";
            rot << sep << Num(roll);
            zoom << sep << Num(ae.zoom);
            focus << sep << Num(ae.focusDistance);
        }
        const double duration = samples.empty() ? 1.0 : std::max(1.0 / fps, static_cast<double>(samples.size()) / fps);

        std::ostringstream js;
        js << "// " << kVersionString << " — After Effects camera import\n"
           << "// Project: " << info.projectName << "  Map: " << info.mapName << "\n"
           << "// Run with File > Scripts > Run Script File... A new composition with a 3D camera is created.\n"
           << "// IW4 (x, y, z) maps to AE (x, -z, y) / " << Num(info.unitsPerPixel)
           << " px; roll is the camera's Z Rotation.\n"
           << "(function () {\n"
           << "  var name = " << JsString(info.name) << ";\n"
           << "  var width = " << info.width << ", height = " << info.height << ", fps = " << Num(fps)
           << ", duration = " << Num(duration) << ";\n"
           << "  var times = [" << times.str() << "];\n"
           << "  var position = [" << pos.str() << "];\n"
           << "  var poi = [" << poi.str() << "];\n"
           << "  var roll = [" << rot.str() << "];\n"
           << "  var zoom = [" << zoom.str() << "];\n"
           << "  var focus = [" << focus.str() << "];\n"
           << "  if (times.length === 0) { alert('IW4Iris: the export contains no frames.'); return; }\n"
           << "  app.beginUndoGroup('Import IW4Iris camera');\n"
           << "  var comp = app.project.items.addComp(name, width, height, 1.0, duration, fps);\n"
           << "  var cam = comp.layers.addCamera(name, [width / 2, height / 2]);\n"
           << "  cam.autoOrient = AutoOrientType.CAMERA_OR_POINT_OF_INTEREST;\n"
           << "  var tr = cam.property('ADBE Transform Group');\n"
           << "  tr.property('ADBE Position').setValuesAtTimes(times, position);\n"
           << "  tr.property('ADBE Anchor Point').setValuesAtTimes(times, poi);\n"
           << "  tr.property('ADBE Rotate Z').setValuesAtTimes(times, roll);\n"
           << "  var opt = cam.property('ADBE Camera Options Group');\n"
           << "  opt.property('ADBE Camera Zoom').setValuesAtTimes(times, zoom);\n"
           << "  opt.property('ADBE Camera Focus Distance').setValuesAtTimes(times, focus);\n"
           << "  comp.openInViewer();\n"
           << "  app.endUndoGroup();\n"
           << "})();\n";
        return js.str();
    }

    std::string ExportCsv(const std::vector<CameraSample>& samples)
    {
        std::ostringstream out;
        out << "frame,time,x,y,z,pitch,yaw,roll,hfov,focus,fstop\n";
        for (const auto& s : samples)
            out << s.frame << ',' << Num(s.time) << ',' << Num(s.state.position.x) << ',' << Num(s.state.position.y)
                << ',' << Num(s.state.position.z) << ',' << Num(s.state.angles.pitch) << ',' << Num(s.state.angles.yaw)
                << ',' << Num(s.state.angles.roll) << ',' << Num(s.state.horizontalFov) << ','
                << Num(s.state.focusDistance) << ',' << Num(s.state.fNumber) << '\n';
        return out.str();
    }
}  // namespace iris
