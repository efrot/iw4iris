# Troubleshooting

Always look at `<environment>\iw4iris\logs\iw4iris.log` first (the five
runs before it are `iw4iris.log.previous` to `.previous5`), and at
**Help → Diagnostics** in the editor,
which lists the offline guard, the engine state, the dvars the editor found
and the log.

## The launcher

**"Valid IW4x installation" does not appear.** The folder must contain
`iw4x.exe`, `main\` and `zone\`. Install IW4x first (https://iw4x.io/install)
and pick the folder that contains `iw4x.exe`.

**"client\iw4x.dll is missing".** Keep the release package together; the
launcher needs `client\iw4x.dll` next to it.

**Setup fails with "Could not link main".** Junctions need an NTFS drive.
Choose a cinematic environment folder on an NTFS drive (the default under
`%LOCALAPPDATA%` is).

**Antivirus.** Some antivirus products dislike programs that modify a
game's code at run time. The cinematic client is IW4x with an extra
component; you may need an exception for the environment folder.

## In the game

**"script compile error / bad syntax" when creating a scene on a map.**
Version 0.3.0 and earlier shipped a Director Mod script the game's
compiler rejects (`anim` is a reserved word of the script language). Fixed
in mod 0.3.1. Close the game, delete any `iw4iris_director` folder you
copied into IW4x's own `mods` folder (the launcher installs the mod into
the cinematic environment; a copy in `mods` is not needed), start the
launcher and click **Update & Launch**: it compares the installed scripts
with the package and replaces them. If the error stays, open the console
with ~: the line above the error names the script file and line; please
report it together with a support bundle (**Help → Diagnostics → Save a
support bundle**).

**A film set stays on the loading screen.** Fixed in 0.5.1: the offline
guard also dropped the packets the game sends to its own local server.
Let the launcher update itself (or **Update & Launch**), then try again.
If a set still hangs, the log names the connection stage it stopped at
(every stage is logged while a set loads); please report it with the log.

**A film set has odd weapons, skins or menus (after a demo from a modded
server).** Since 0.6.1 a film set uses the standard game files unless a
mod is chosen under **Create new scene → Game files**; check that choice.

**The editor does not open / F1 does nothing.** The editor opens with the
game unless *Settings → Open the editor when the game starts* is off; F1
shows and hides it. Check the log for "offline guard". If it says FAILED,
the tools stay disabled by design. Make sure you started the game from the
launcher (the environment's `iw4x.exe`, not your normal installation).

**"the client tried to connect to a network server".** The cinematic build
never connects to servers. The tools are disabled until you restart the
game. Use your normal IW4x installation to play online.

**The camera does not move / the view stays on the player.** Make a
cinematic camera active (a Dolly with keys, or the Free camera): the
default "Game view" camera shows the game's own view. If a cinematic camera
is active and the view still does not change, the view hook does not reach
the renderer on your setup — please report it with your log (this is the
first item of docs/TESTING.md).

**Smoke or effects disappear in some camera angles.** The game culls
effects against the player's view, which is computed before IW4Iris
replaces the camera. Known limitation (COMPATIBILITY.md).

**Look controls say "unavailable".** The dvar behind them does not exist in
your game; Diagnostics lists which ones were found.

**Pause or seeking is jittery.** Pause and seek are experimental (they
work through the client's demo clock). Please report what you see.

**Seeking backwards takes a while.** Backward seeks restart the demo and
replay it to the target. This is exact but takes as long as the game needs
to read the demo up to that point.

## Recording

**"The game does not follow the render clock".** The deterministic clock
could not be verified (the game reads time in a way IW4Iris does not
virtualise), so the capture did not start rather than produce mistimed
footage. Please report it with the log.

**"needs FFmpeg, which was not found".** Install FFmpeg (INSTALL.md) or
record a PNG / JPEG sequence.

**"Custom render sizes are not available yet".** Recordings use the game's
resolution: set it in the game's options (`r_mode`) and leave the
recording size on "Game resolution".

**"Freeze frames cannot be rendered offline yet".** A speed of 0 (or a
speed so low that a frame advances the scene by less than 1 ms) is inside
the range. Raise the speed there.

**"Frame sync NOT verified".** The colour tag used to measure the
renderer's latency was not read back (for example because a post effect
changed it). The recording assumes no latency; check its first frames.

**The game is slow while recording.** That is expected: every frame is read
back and encoded, and the game waits for the encoder (the output is still
timed exactly). Lower the queue depth if the game runs out of memory at 4K.
