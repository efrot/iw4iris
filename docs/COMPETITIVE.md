# Competitive matrix (evidence-based)

How IW4Iris compares with the established MW2 / Black Ops 2 moviemaking
tools, capability by capability. **No superiority is claimed here**: the
IW4Iris column gives the verification state from
[BACKLOG.md](BACKLOG.md) (nothing is runtime-tested yet), and the other
columns only state what was verified from the tools' own public sources.

Sources and how far they were checked:

* **IW4Cine** (GSC mod for IW4x, by sortileges): its public help page and
  scripts were studied (behaviour only; no explicit licence, so no code is
  reused). <https://sortileges.github.io/iw4cine/help>
* **T6Iris** (Black Ops 2): its public source and README were studied in
  the research phase ([RESEARCH.md](../RESEARCH.md) section 5); it runs on
  a different engine (T6) with a theater mode IW4 does not have.
* **IW4MVM**: closed source; its feature set was **not verified** in this
  project (no public documentation was retrieved). Its column is left
  empty rather than guessed.

Legend: ✓ = available (per the source above), – = not available,
? = not verified.

| Capability | IW4Iris 0.5 | IW4Cine | T6Iris (BO2) | IW4MVM |
| --- | --- | --- | --- | --- |
| Install | one-click launcher, isolated copy (Windows-built) | manual mod install | own launcher | ? |
| Editor | native docked UI, timeline, graph editor, command palette (Windows-built) | console commands / binds | native UI | ? |
| Demo playback, pause, speed | ✓ (experimental: client demo clock) | – (works in a private match) | ✓ (theater) | ? |
| Rewind | restart and replay to the target (exact, slow) | – | ✓ (theater snapshots) | ? |
| Player browser and identities | ✓ (unit-tested; Windows-built) | – | ✓ | ? |
| Other players' first person | reconstructed, labelled (Windows-built) | – | ✓ (theater has full player states) | ? |
| Third person / chase of any player | ✓ (Windows-built) | – | ✓ | ? |
| Kill / event browser, trickshot candidates | ✓ (unit-tested; Windows-built) | – | partial (kill markers) | ? |
| Several recordings of one match (angles), sync from shared kills | ✓ (unit-tested; cross-compiled) | – | – (theater has every player in one recording) | ? |
| Dolly / orbit / follow / static cameras | ✓ | camera paths in GSC | ✓ | ? |
| Bone camera | – (needs DObj bone bindings) | – | ✓ | ? |
| Actors (characters, animations, weapons) | ✓ via the Director Mod (protocol-reconciled; **film set loading not yet confirmed in the game**) | ✓ (bots and script models) | ✓ | ? |
| Animation scrubbing (pose at any time) | – (script models restart clips; pre-roll in recordings) | – | ✓ | ? |
| Native 3D text and logos in the world | ✓ (D3D9 renderer; occlusion when verified) | – | – | ? |
| Render passes (depth, greenscreen) | beauty only | – | ✓ | ? |
| Frame-exact recording, ProRes / FFmpeg | ✓ (virtual render clock) | – | ✓ | ? |
| Blender / After Effects export | ✓ (Blender add-on tested against the exporter) | – | ? | ? |

## What this says

* Where T6Iris is ahead (rewind, other players' exact first person, bone
  cameras, render passes, animation scrubbing), the gap is mostly the
  engine: BO2's theater records and restores full player states; IW4x
  client demos do not. IW4Iris reconstructs what the demo contains and
  labels the rest.
* Where IW4Iris adds things none of the others list (native 3D graphics,
  player identities across slot reuse, trickshot candidates, render
  preflight, diagnostics), none of it is runtime-verified yet. The
  in-game acceptance procedure in [TESTING.md](TESTING.md) decides.
