# Sample projects

* `rust-trickshot-orbit.iw4iris` — a four-key dolly on mp_rust with eases,
  handheld shake, a speed ramp into slow motion around the "Trickshot" marker,
  a keyed contrast curve and lens-driven depth of field. Open it with
  File > Open in the editor (or `iw4iris_open <path>` in the console), then
  load any demo on mp_rust: the camera coordinates are world positions on
  that map, so the shot needs the map, not a particular demo.

Samples reference maps and demos by name only; they never contain game
files. `tests/test_samples.cpp` checks that every sample here loads.
