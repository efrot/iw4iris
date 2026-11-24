# Exporting cameras for compositing

Export panel → choose a format → **Export**. The shot's camera (camera cuts
included) is sampled at every frame of the recording range, at the
recording frame rate, so the export matches a recording of the same range
frame for frame. Files go to `<environment>\iw4iris\exports\`.

## Coordinate systems

| System | Axes | Units |
| --- | --- | --- |
| IW4 | right-handed, X forward, Y left, Z up | inches (game units) |
| Blender | right-handed, Z up; **world axes shared with IW4** | metres (`unitsPerMetre`, default 39.37) |
| After Effects | right-handed, X right, Y down, Z away from the viewer; IW4 (x, y, z) → AE (x, −z, y) | pixels (`unitsPerPixel`) |

Angles in IW4 are `[pitch, yaw, roll]`, rotation `Rz(yaw)·Ry(pitch)·Rx(roll)`
(`iris::AnglesToAxis`, tested against the engine's `AngleVectors`).

## Blender

1. Install `tools/blender/iw4iris_import.py` (in a release:
   `resources\tools\blender\`): Edit → Preferences → Add-ons → Install.
2. File → Import → **IW4Iris camera (.iw4cam.json)**.

The add-on creates a camera with `sensor_fit = HORIZONTAL` and the export's
sensor width, keys `location`, `rotation_quaternion`, `lens`, depth-of-field
focus distance and f-stop on every frame (linear between frames), and sets
the scene's frame rate (NTSC rates as `fps` + `fps_base`), resolution and
frame range. The camera's rotation is the Blender camera orientation (it
looks down its local −Z): `[−left | up | −forward]` of the IW4 view.

To composite: use the recorded image sequence as the camera background or
as a plane in the compositor; 3D elements placed in Blender line up with
the footage because the camera, lens and timing are the game's.

## After Effects

1. Choose **After Effects (.jsx)** and export.
2. In After Effects: File → Scripts → **Run Script File...** and pick the
   `.jsx`.

The script creates a composition at the export's resolution, frame rate and
duration with a two-node camera: position and point of interest per frame
(the point of interest lies on the view axis at the focus distance), Z
rotation for the roll, zoom from the field of view
(`zoom = width / 2 / tan(hfov / 2)`), and focus distance. Import the
recorded sequence (File → Import, "PNG Sequence") into the same composition.

> The JSX is generated and its structure is tested, but it has not been run
> in After Effects by the authors yet. If the roll turns the wrong way in
> your version of After Effects, negate the Z rotation keys.

## CSV

`frame,time,x,y,z,pitch,yaw,roll,hfov,focus,fstop` in IW4 units and
degrees — for scripts and other tools.
