# Movecade Kart → UE 5.8 port plan

Goal: port the whole kart racer (movecade-v2 `app/src/games/kart`) to UE 5.8. Phones stay the controllers,
and the UE game is the room host. Then upgrade the textures, avatars, powerups and maps.

Rule of the port: **parity first, then upgrade.** Every mechanic is rebuilt from the specs in this folder
with the same constants, so the UE version plays like the web version before anything is changed.
Upgrades are separate phases, so we can always tell a port bug from a design change.

## Specs (source of truth for parity)

| Doc | Covers |
|---|---|
| [01-handling-physics.md](01-handling-physics.md) | Drivetrain, steering, drift and boost, surfaces, collisions, bots, camera |
| [02-items-powerups.md](02-items-powerups.md) | Every item, odds, projectiles, hit effects, item boxes, VFX and SFX |
| [03-tracks-scenery-assets.md](03-tracks-scenery-assets.md) | Track catalog, geometry data and export schema, scenery, avatars, asset inventory |
| [04-flow-network-ui-audio.md](04-flow-network-ui-audio.md) | Room lifecycle, protocol, phone controller, scoring, HUD, audio |

## What ships today (from the specs)

- Kart reaches v2 through `app/src/games/legacyBridge.ts`, which does all its networking: sensor frames,
  `kart-input {isAccelerating|isReversing|isDrifting|useItem|steeringInput}`, score push, completion and the
  countdown publish. Phone buttons: GAS, BRAKE (also reverse), DRIFT, ITEM. There is no music.
- The live kart runs on the **legacy engine**, `app/src/legacy/Experience/kart/engine` (`MultiplayerKart.tsx`,
  `store.ts`, `MultiplayerItems.tsx`), on the host TV, for every kart including bots. `app/src/games/kart` holds
  helpers, track data (`tracks/catalog.ts`) and the phone controller. The specs mark each file as live, DEV-only or dead.
- The partykit server has no item or physics logic. It relays phone input; the host simulates everything.
- Two flat tracks, 3 laps each: **Sunset Harbor** (procedural Catmull-Rom spline, 1612 m lap) and **Violet Loop**
  (Draco GLB plus a baked racing line and cross-section profile, 1470 m lap).
- 11 items with position-weighted odds. Blue Shell and Bullet Bill only appear in 4-player lobbies.

## Bugs in the shipped game: fix or keep?

The port must choose for each one. The default is **fix**, because players can't rely on behaviour that never
worked. Anything marked _verify_ gets checked in a live web race before we decide.

| # | Bug | Source | Default |
|---|---|---|---|
| B1 | Item hits and the bogged rocket start don't slow karts: speed writes never reach the physics, and penalty multipliers below 1 are ignored. | 01, 02 | Fix (this is the core of "better powerups") |
| B2 | Kart-to-kart collision never runs, because the list of other karts is empty. | 01 | Fix |
| B3 | A shared `boostPower` leaks between karts and may give others +40 top speed. | 01 | Fix (_verify_) |
| B4 | Slipstream needs speed ≥ 70, but top speed is 40, so it never fires. | 01 | Fix (retune the threshold) |
| B5 | Handling depends on frame rate: constants are tuned per frame at 60 fps. | 01 | Fix (fixed 60 Hz step that matches the 60 fps feel) |
| B6 | Two cameras fight in single-kart view, and the follow camera sits off-axis at diagonal headings. | 01 | Fix |
| B7 | Results crown the slowest racer (GAMES-01). A DNF signs a 0 ms score (GAMES-02). No race time cap. | QA, 04 | Already fixed on web; copy the fix: 10 s minimum time, ascending ranking, never submit a non-finisher, 5 min cap plus 30 s after the leader |
| B8 | The HUD converts speed as if km/h, but the sim's value is mph. | 04 | Fix |
| B9 | The host never sends the `scores-ack` frame. | 04 | Fix (send it) |
| B10 | The host sends the phone no item, position or lap feedback. | 04 | Upgrade-pass candidate (show the item and position on the phone) |
| B11 | Sticky walls (spec 01 bug 9): any wall within 2.5 m ahead stops forward motion. A kart boosted nose-first into a wall slows to ~2 SU, can't turn away, and waits 2.5 s for the stuck watchdog. The lobby bot does this in Harbor's final hairpin every lap (port test MovecadeKart.Race.BotLaps). | 01, port | Fixed: wall response scales with the angle of impact, and a wedged kart on the gas can still turn (`-KartLegacyWalls` restores the web) |
| B12 | Speed units (B8): the port treats SU as m/s and shows km/h (×3.6, top speed 144 km/h). | port | Decided |
| B14 | Harbor's walls are ±16.3 m offsets of the centreline. Where a bend is tighter than that, the inner offset folds back over the road; in the final hairpin it leaves a wall across the racing line that wedged the lobby bot every lap (this was the real cause of B11's stuck bot). | port | Fixed: folded wall segments (63 of 1740) and ribbon quads are dropped; the analytic sweep still contains karts |
| B13 | Standings: the web sorts by laps, then the raw next-checkpoint index, which puts a kart that only has the finish left last within its lap. The port ranks by laps × 100 + lap progress %. | port | Fixed |

## Key technical decisions

- **Kart physics:** a custom kinematic `UKartMovementComponent` on a sphere-root pawn, not Chaos Vehicles.
  It runs at a fixed 60 Hz with sim state in the web units, so constants port verbatim; the result converts to cm for rendering.
- **Tracks:** a C++ `ATrackBuilder` that reads exported JSON and builds an exact spline match as baked static
  meshes, not Landscape. Scenery comes from the same JSON.
- **Items:** data assets (item, status effect, odds table), an item component and a status component on the kart,
  projectile actors on their own collision channel, Niagara VFX, MetaSound cues.
- **Avatars:** one shared skeleton with Control Rig. Today's avatars are 6 faces × 2 bodies on a 26-joint rig.
- **Naming:** neutral internal item IDs, with display names in a string table. Bob-omb, Bullet Bill, Blooper,
  Shell and Star are Nintendo names, so they need replacing before any commercial release.

## Open risks

- **Native host Origin (confirmed by the network agent).** On Windows, UE's WebSocket client always sends
  `Origin: http://<party host>` and can't replace it (adding our own makes a duplicate, which the server rejects
  with 4003). Local dev allow-lists `http://localhost` / `http://127.0.0.1`. Production would send
  `http://party.movecade.com`, which the https-only allow-list rejects, so shipping phone play needs a server
  change (a native-host credential, or allow-listing that exact origin) or a patched WebSocket module. Phones
  also don't act on the `collision` haptic events yet (controller change).
- **Native host is blocked by the room server.** The server only accepts a host socket that sends an
  allow-listed `Origin` header. The quick path is for UE to send an `Origin` header and add that origin to
  `EXTRA_ALLOWED_ORIGINS` in the PartyKit config. A proper native-host credential needs a server change in
  movecade-v2. Either way it touches the live server, so Jimmy decides before phase 6. Local dev
  (`npm run dev:party`) can be configured freely. Spec 04 lists six more native-host concerns (F2–F7) and
  how to fill seats with bot sockets (F8).
- **Entitlements:** premium gating is enforced only when `ENABLE_MONETIZATION=true`. It isn't set in
  `partykit.production.json`, but it could be set in the PartyKit dashboard, which hasn't been checked.

- The **Violet Loop GLB** licence is unverified (the asset ledger only has an owner note). Settle it before a
  commercial build, or replace the track during the upgrade pass. Its profile generator script is missing, so
  the baked profile can't be regenerated if the mesh changes.
- The **Violet GLB** uses Draco and WebP. Decode both before UE import.
- **Violet Loop's scenery has Mario-style motifs** (green warp pipes, floating brick blocks, the layout of a
  "mario-circuit" test model). Jimmy confirmed the model is theirs to use, but those shapes are an IP risk for a
  commercial build: the maps pass replaces them with original props.
- **Poly Haven normal maps** are OpenGL format: flip the green channel on import.

## Phases

Status as of 2026-10-19 (night). ✅ done · 🔄 in progress · ⏳ not started.

0. ✅ **Setup.** VS 2022 with C++, git + LFS baseline, UE MCP connected, a C++ game module added to the project.
1. ✅ **Data export.** (1b props/meshes/textures: 🔄 tracks agent) A Node exporter, kept in this project under `Tools/`, that reads the movecade repo without
   changing it. It writes each track (centerline, widths, surfaces, checkpoints, grid, item boxes, boost pads,
   scenery placements) to JSON using the schema in spec 03. `store.ts` constants are copied by hand, and an
   esbuild stub exposes `buildHarborScenery`. Canvas-drawn textures are rendered separately.
2. ✅ **Core kart.** Arcade movement component, keyboard/gamepad input, chase camera. Tuned against spec 01.
3. 🔄 **Tracks.** Harbor ✅; Violet Loop and Harbor scenery 🔄 (tracks agent). A track-builder actor driven by the JSON. Harbor and Violet are drivable, with laps and checkpoints.
4. ✅ **Race flow.** GameMode/GameState, countdown, live positions, finish, results (fixing GAMES-01/02 and adding a time cap), bots.
5. ✅ **Items.** (VFX upgrade 🔄 FX agent) Data-driven items with all the current powerups, item boxes, hit effects, VFX and SFX.
6. ✅ **Phone host.** Local PartyKit only; real deployment needs the Origin decision (open risks). WebSocket room client, lobby with join code and QR, tilt frames → steering, host → phone feedback, score submission.
7. ✅ **Audio.** (UAudioComponent layers, not MetaSounds; same maths, golden-tested.) MetaSound engine layering and every cue from spec 04.
8. 🔄 **Upgrade pass.** Real hits and new powerup rules ✅, walls ✅; kart + avatar art 🔄; maps ⏳. Materials (Fab/Megascans), lighting (Lumen), driver avatars, improved and new powerups, new maps.
9. 🔄 **Ship.** Win64 packaging in progress. Win64 packaging, performance, QA against the web version.

Phases 3, 5 and 6 can run in parallel (one agent each) once phase 2 has a drivable kart.
