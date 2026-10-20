# Movecade Kart Racer: case study

**A party kart racer in Unreal Engine 5.8 that up to four players steer by tilting their phones.**
It is a port of a shipped three.js web game, rebuilt to console quality: hardware ray-traced lighting,
original environments, a sprung "game feel" layer, a full Slate front end, and a deterministic simulation with 52
automated tests.

![Hero shot](media/hero.jpg)

| | |
|---|---|
| **Engine** | Unreal Engine 5.8, C++ (one runtime module, ~26k lines), no Blueprint logic |
| **Platforms** | Windows (DX12, SM6); split screen up to 4 players; phones as controllers over WebSockets |
| **Rendering** | Lumen GI and reflections on hardware ray tracing, virtual shadow maps, Nanite, TSR, dynamic resolution |
| **Performance** | About 95 fps solo at a 1440p-equivalent load on an RTX 4070 SUPER; 60+ fps in four-way split screen |
| **Effects** | Niagara boost flames, tiered drift sparks, tyre marks, dust, impact sparks, a stylised explosion, lightning, confetti; per-view screen effects |
| **Tests** | 52 automation tests: sim, determinism, bots, items, effects, network protocol, audio, feel, rendering budgets, UI logic |

## Video

[Trailer, 41 s (MP4)](media/trailer.mp4) · [Gameplay, Violet Loop](media/gameplay_violet.mp4) · [Gameplay, Sunset Harbor](media/gameplay_harbor.mp4)

Every frame here is captured from the game itself. The cinematic shots come from its `-KartCinematic` camera and
fixed-step frame capture (`Tools/video`).

| | |
|---|---|
| ![Violet Loop](media/violet.jpg) | ![Sunset Harbor](media/harbor.jpg) |
| ![Racer select](media/racers.jpg) | ![Results](media/results.jpg) |

## What was built, and the interesting problems

### 1. A deterministic simulation that plays exactly like the original

The web game's handling is arcade maths tuned per frame. A physics vehicle could only approximate it, and would make
the race non-deterministic, so the handling is ported as a plain C++ simulation: no UObjects, fixed 60 Hz steps, in
the original's units. Every constant ports verbatim, and behaviour can be checked against the source.

- **Proof:** `MovecadeKart.Sim.Determinism` races four mixed-skill bots for a minute, twice, through kart contact,
  slipstream, rubber-banding and items. Every kart must end bit-identical, with the same item events on the same steps.
- **The test paid for itself:** it showed that on Violet Loop the bots never collected an item. The box row sits
  2.5 m off the racing line, exactly the pickup radius, and the web game had the same bug. Bots now detour through
  boxes when empty-handed.
- **Parity, with fixes on purpose:** the original had real bugs. Item hits never slowed anyone, walls were sticky,
  and two cameras fought each other. Each one is in a bug table with the decision taken
  ([00-PLAN.md](../Port/00-PLAN.md)). Behavioural fixes keep a switch back to the original (`-KartLegacyWalls`).

### 2. Game feel without touching gameplay

![Boost pad](media/boost.jpg)

The kart, camera and driver are pure presentation, reading the sim and never writing to it, so all the "juice" sits in
its own layer (`FKartFeel`):

- **Kart body:** substepped springs pitch the chassis under throttle and brake, roll it under lateral g, and bounce
  and squash it on landings. Wall hits lift the side that was struck.
- **Camera:** the FOV kicks and the camera pulls back on boosts, swings wide in drifts, banks into corners and dips
  on landings.
- **Driver:** a native animation proxy (no AnimBP) solves two-bone IK onto the steering wheel. On top of that grip
  pose the driver acts out the race:
  - leans into drifts, is pressed back by boosts, and gets thrown about by wall and kart hits (springs kicked by
    the sim's events);
  - glances at rivals alongside and flails through spin-outs;
  - celebrates on the podium and slumps off it.

  The face blends the avatars' expression morphs (focused, grin, shocked, beaming, crestfallen) through a small,
  unit-tested mixer.
- **Transitions:**
  - the countdown cranes down from high over the grid onto each kart, and GO punches the FOV;
  - respawns drop in from above instead of whipping the camera across the map;
  - the finish swings the camera round to the driver's face;
  - the results come in on a flash cut, because a camera blend between two moving cameras flew through scenery.
- **Off switch:** `-KartWebFeel` turns the whole layer off for side-by-side comparison. Unit tests check that the
  springs behave the same at 30 and 240 fps.

### 3. Holding 60 fps with ray tracing on

![Four-way split screen](media/split4.jpg)

- **Settings:** `UKartGameUserSettings` adds presets, a ray tracing switch and a motion blur switch on top of the
  engine's scalability.
- **Budget:** `UKartRenderQualitySubsystem` counts the local views, caps the expensive groups in split screen, and
  steers dynamic resolution toward a 16.6 ms budget. A frame-time guard steps GI, reflections and shadows down (and
  back up) only when resolution is already at its floor, using Scalability's temporary levels so a player's saved
  settings never change.
- **Measured, not guessed:** RT shadows were measured and turned down. They cost about 2 ms for no visible gain.
- **One grade for every track:** the colour grade is a single low-priority, unbound volume, and each track's own
  sun, sky and fog layer on top.

### 4. Vehicles rebuilt from script

All four vehicles are rebuilt by a headless Blender pass (`Tools/art/detail/build.ps1`) on top of the original
mounts, so seats, wheel hubs, steering-wheel grips and FX anchors stay where the code expects them:
- **Detail:** bevelled bodies, tyres with real tread and sidewalls, spoked rims, brake discs, coil-overs,
  exhausts, lamps with lenses, carbon aero, stitched seats and an original racing roundel.
- **Materials:** a flake clear-coat paint and procedural carbon, brushed metal and tyre textures (numpy, fixed
  seeds).
- **Cost:** triangle counts went up about 10x, from 3–14k to 37–89k per vehicle. With Nanite there is no measurable
  GPU cost.

### 5. Tracks as data, art rebuilt from scratch

![Track select](media/tracks.jpg)

A Node exporter turns the web game's track modules into JSON: centreline, walls, checkpoints, pads, boxes and scenery.
The game loads the gameplay from that file and builds the visuals from the same file. So Violet Loop's art could be
replaced entirely with an original twilight crystal garden (it had borrowed a famous franchise's look) while a
fingerprint check proves that not one wall, checkpoint or pad moved.

### 6. A console-style front end in code

![Title screen](media/frontend.jpg)

The title, menus, racer select, track select, settings, HUD, pause and results are Slate, drawn by one painter and
style. That keeps them reviewable as text, with no binary merge conflicts, and makes elastic motion easy with render
transforms. A raw input processor routes keyboard and every gamepad by user index, so pads 2–4 join with Start and the
button hints switch between key caps and pad glyphs. The front end runs inside the track level, with real karts on
the grid wearing each player's choices as they change them.

### 7. Phones as controllers

The game hosts a room on the same PartyKit server the web version uses. Phones scan a QR code (the encoder is
written from the spec and tested against reference matrices), stream tilt frames over WebSockets, and get haptics
and signed scores back. `Tools/net-sim` races a simulated phone against bots end to end.

## Engineering practice

- **Every change is verified by machine:** `Tools/ue/test.ps1` builds and runs every test headless.
  `Tools/ue/play.ps1` runs the real game with an autopilot and screenshots. `Tools/ui/shots.ps1` tours every screen.
  `Tools/video/capture.ps1` records fixed-step gameplay video.
- **A code review, acted on:** a review of the sim, race, network and audio code was followed by a fix for every
  verified bug: unchecked JSON, frame-rate-dependent impact pulses, slow motion below 30 fps, out-of-order sensor
  frames, a phone that kept steering after its screen locked, and per-step heap allocations. The 300-line sim step
  became named phases, and every bot lap time stayed identical to the step.
- **Docs:** [Architecture](../Architecture.md), the port specs in [Docs/Port](../Port), and an asset ledger with a
  licence for every third-party file ([ASSETS.md](../Port/ASSETS.md)).

### 8. Original look, borrowed mechanics

The web game's items followed a famous franchise closely: yellow "?" boxes, shells, a banana, a bullet with eyes,
a star. The mechanics are kept, and every look is original. Item boxes are glass prize orbs, projectiles are buzz
discs and a rotor drone, the dropped hazard is a traffic cone, and the rocket is a white toy rocket. All are
modelled in code (`FX/KartItemMeshes`).

## Credits

Original web game and art direction: Movecade. Unreal port, engineering and technical art: this project. Third-party
assets are CC0 (Poly Haven) or SIL OFL fonts, listed in [ASSETS.md](../Port/ASSETS.md).
