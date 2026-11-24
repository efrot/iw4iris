"""Tests for tools/blender/iw4iris_import.py helpers (run without Blender).

    python3 -I tests/python/test_blender_addon.py
"""
import importlib.util
import json
import math
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON = os.path.join(HERE, "..", "..", "tools", "blender", "iw4iris_import.py")

spec = importlib.util.spec_from_file_location("iw4iris_import", ADDON)
addon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(addon)


def rotate(q, v):
    w, x, y, z = q
    # v' = q v q*
    ix = w * v[0] + y * v[2] - z * v[1]
    iy = w * v[1] + z * v[0] - x * v[2]
    iz = w * v[2] + x * v[1] - y * v[0]
    iw = -x * v[0] - y * v[1] - z * v[2]
    return (ix * w + iw * -x + iy * -z - iz * -y,
            iy * w + iw * -y + iz * -x - ix * -z,
            iz * w + iw * -z + ix * -y - iy * -x)


def doc(frames, fps=(60, 1)):
    return {"format": "iw4iris-camera", "version": 1, "fps": list(fps), "sensor_width": 36.0,
            "resolution": [1920, 1080], "frames": frames}


class BlenderAddonTests(unittest.TestCase):
    def test_rejects_other_files(self):
        with self.assertRaises(ValueError):
            addon.load_document(json.dumps({"format": "something"}))
        with self.assertRaises(ValueError):
            addon.load_document(json.dumps(doc([])))
        with self.assertRaises(ValueError):
            addon.load_document(json.dumps(dict(doc([{"frame": 0}]), version=9)))

    def test_frame_rates(self):
        self.assertEqual(addon.frame_rate(doc([], (60, 1))), (60, 1.0))
        fps, base = addon.frame_rate(doc([], (24000, 1001)))
        self.assertEqual(fps, 24)
        self.assertAlmostEqual(fps / base, 24000 / 1001, places=9)

    def test_quaternion_signs_are_continuous(self):
        frames = [{"frame": 0, "location": [0, 0, 0], "rotation_quaternion": [1, 0, 0, 0], "lens": 50},
                  {"frame": 1, "location": [0, 0, 0], "rotation_quaternion": [-0.99, -0.1, 0, 0], "lens": 50}]
        q = addon.normalised_quaternions(frames)
        self.assertGreater(sum(a * b for a, b in zip(q[0], q[1])), 0)
        self.assertAlmostEqual(math.sqrt(sum(v * v for v in q[1])), 1.0)

    def test_keys_and_offset(self):
        frames = [{"frame": 10 + i, "location": [i, 0, 0], "rotation_quaternion": [1, 0, 0, 0], "lens": 35,
                   "focus_distance": 2.0, "fstop": 4.0} for i in range(3)]
        k = addon.keys(addon.load_document(json.dumps(doc(frames))), frame_offset=-10)
        self.assertEqual([f[0] for f in k], [0, 1, 2])
        self.assertEqual(k[2][1], (2.0, 0.0, 0.0))
        self.assertEqual(k[0][3:], (35.0, 2.0, 4.0))

    def test_export_from_the_cpp_exporter(self):
        """The C++ exporter's output (written by the C++ tests) imports, and its
        camera looks along the IW4 forward vector it was exported from."""
        path = os.path.join(tempfile.gettempdir(), "iw4iris-sample.iw4cam.json")
        if not os.path.exists(path):
            self.skipTest("run the C++ tests first (they write %s)" % path)
        with open(path, encoding="utf-8") as handle:
            d = addon.load_document(handle.read())
        k = addon.keys(d)
        self.assertEqual(len(k), len(d["frames"]))
        for (frame, loc, quat, lens, focus, fstop), f in zip(k, d["frames"]):
            pitch, yaw, roll = (math.radians(a) for a in f["iw4"]["angles"])
            forward = (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch))
            looks = rotate(quat, (0.0, 0.0, -1.0))
            for a, b in zip(looks, forward):
                self.assertAlmostEqual(a, b, places=6)
            # lens and the exported horizontal FOV agree for the sensor width
            hfov = 2 * math.degrees(math.atan(d["sensor_width"] / (2 * lens)))
            self.assertAlmostEqual(hfov, f["iw4"]["hfov"], places=6)
            # metres
            self.assertAlmostEqual(loc[0], f["iw4"]["origin"][0] / d["units_per_metre"], places=9)


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
