# IW4Iris — Blender importer for IW4Iris camera exports (.iw4cam.json)
# Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#
# Install: Edit > Preferences > Add-ons > Install... and pick this file,
# then File > Import > IW4Iris camera (.iw4cam.json).
#
# The export already holds Blender-ready values (see core/src/exporters.cpp):
#   * location in metres, world axes shared with IW4 (X forward, Y left, Z up),
#   * rotation_quaternion (w, x, y, z) of the Blender camera (looks down -Z),
#   * lens in mm for the export's sensor width with a horizontal sensor fit,
#   * focus distance in metres and the f-stop.
# Every frame is keyed, so the camera matches the recorded frames exactly.

bl_info = {
    "name": "IW4Iris camera importer",
    "author": "IW4Iris contributors",
    "version": (0, 1, 0),
    "blender": (2, 93, 0),
    "location": "File > Import > IW4Iris camera (.iw4cam.json)",
    "description": "Imports camera animation exported by IW4Iris (Call of Duty: MW2 / IW4x cinematics)",
    "category": "Import-Export",
}

import json
import math

FORMAT = "iw4iris-camera"


# ---------------------------------------------------------------- pure helpers
# (no bpy: unit-tested outside Blender, see tests/python/)

def load_document(text):
    """Parses and validates an export; returns the document."""
    doc = json.loads(text)
    if doc.get("format") != FORMAT:
        raise ValueError("not an IW4Iris camera export (format %r)" % doc.get("format"))
    if int(doc.get("version", 0)) > 1:
        raise ValueError("made by a newer IW4Iris (version %s)" % doc.get("version"))
    frames = doc.get("frames")
    if not isinstance(frames, list) or not frames:
        raise ValueError("the export contains no frames")
    return doc


def frame_rate(doc):
    """Returns (fps, fps_base) for Blender's scene.render settings."""
    num, den = doc.get("fps", [60, 1])
    num, den = int(num), int(den)
    if den <= 0 or num <= 0:
        raise ValueError("invalid frame rate %r/%r" % (num, den))
    if den == 1:
        return num, 1.0
    # NTSC-style rates: 24000/1001 -> fps 24, base 1.001
    fps = round(num / den)
    return fps, fps / (num / den)


def normalised_quaternions(frames):
    """Quaternions with consistent signs (q and -q are the same rotation)."""
    out = []
    previous = None
    for f in frames:
        w, x, y, z = (float(v) for v in f["rotation_quaternion"])
        n = math.sqrt(w * w + x * x + y * y + z * z) or 1.0
        q = [w / n, x / n, y / n, z / n]
        if previous is not None and sum(a * b for a, b in zip(previous, q)) < 0.0:
            q = [-v for v in q]
        out.append(q)
        previous = q
    return out


def keys(doc, frame_offset=0):
    """(frame, location, quaternion, lens, focus, fstop) per exported frame."""
    frames = doc["frames"]
    quats = normalised_quaternions(frames)
    result = []
    for f, q in zip(frames, quats):
        result.append((int(f["frame"]) + frame_offset,
                       tuple(float(v) for v in f["location"]),
                       tuple(q),
                       float(f["lens"]),
                       float(f.get("focus_distance", 0.0)),
                       float(f.get("fstop", 2.8))))
    return result


# ---------------------------------------------------------------- Blender

def import_camera(context, path, frame_offset=0, set_scene=True, link_to_active=False):
    import bpy

    with open(path, "r", encoding="utf-8") as handle:
        doc = load_document(handle.read())

    name = doc.get("name") or "IW4Iris Camera"
    cam_data = bpy.data.cameras.new(name)
    cam_data.sensor_fit = "HORIZONTAL"
    cam_data.sensor_width = float(doc.get("sensor_width", 36.0))
    cam_data.clip_start = 0.01
    cam_data.clip_end = 10000.0
    cam = bpy.data.objects.new(name, cam_data)
    context.collection.objects.link(cam)
    cam.rotation_mode = "QUATERNION"

    data = keys(doc, frame_offset)
    for frame, loc, quat, lens, focus, fstop in data:
        cam.location = loc
        cam.rotation_quaternion = quat
        cam.keyframe_insert(data_path="location", frame=frame)
        cam.keyframe_insert(data_path="rotation_quaternion", frame=frame)
        cam_data.lens = lens
        cam_data.keyframe_insert(data_path="lens", frame=frame)
        if hasattr(cam_data, "dof"):
            cam_data.dof.focus_distance = max(focus, 0.01)
            cam_data.dof.aperture_fstop = max(fstop, 0.1)
            cam_data.dof.keyframe_insert(data_path="focus_distance", frame=frame)
            cam_data.dof.keyframe_insert(data_path="aperture_fstop", frame=frame)

    # Exact per-frame values: no interpolation between the keys.
    for anim in (cam.animation_data, cam_data.animation_data):
        if anim and anim.action:
            curves = getattr(anim.action, "fcurves", None)
            if curves is None:
                continue
            for curve in curves:
                for point in curve.keyframe_points:
                    point.interpolation = "LINEAR"

    if set_scene:
        scene = context.scene
        fps, base = frame_rate(doc)
        scene.render.fps = fps
        scene.render.fps_base = base
        width, height = doc.get("resolution", [1920, 1080])
        scene.render.resolution_x = int(width)
        scene.render.resolution_y = int(height)
        scene.frame_start = data[0][0]
        scene.frame_end = data[-1][0]
        scene.camera = cam
    return cam


try:
    import bpy
    from bpy.props import BoolProperty, IntProperty, StringProperty
    from bpy_extras.io_utils import ImportHelper

    class IMPORT_OT_iw4iris_camera(bpy.types.Operator, ImportHelper):
        """Import an IW4Iris camera export"""
        bl_idname = "import_scene.iw4iris_camera"
        bl_label = "Import IW4Iris camera"
        bl_options = {"REGISTER", "UNDO"}

        filename_ext = ".json"
        filter_glob: StringProperty(default="*.iw4cam.json;*.json", options={"HIDDEN"})
        frame_offset: IntProperty(name="Frame offset", default=0,
                                  description="Added to the exported frame numbers")
        set_scene: BoolProperty(name="Set scene frame rate, range and resolution", default=True)

        def execute(self, context):
            try:
                import_camera(context, self.filepath, self.frame_offset, self.set_scene)
            except (OSError, ValueError, KeyError) as error:
                self.report({"ERROR"}, "IW4Iris import failed: %s" % error)
                return {"CANCELLED"}
            return {"FINISHED"}

    def menu_import(self, context):
        self.layout.operator(IMPORT_OT_iw4iris_camera.bl_idname, text="IW4Iris camera (.iw4cam.json)")

    def register():
        bpy.utils.register_class(IMPORT_OT_iw4iris_camera)
        bpy.types.TOPBAR_MT_file_import.append(menu_import)

    def unregister():
        bpy.types.TOPBAR_MT_file_import.remove(menu_import)
        bpy.utils.unregister_class(IMPORT_OT_iw4iris_camera)

except ImportError:  # outside Blender (tests)
    def register():
        raise RuntimeError("register() needs Blender")

    def unregister():
        pass

if __name__ == "__main__":
    register()
