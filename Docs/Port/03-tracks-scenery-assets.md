# 03 — Tracks, scenery, visual assets and avatars

Source: `movecade-v2` @ `58bf0476` (read-only). Target: UE 5.8 (`MyProject`).

The JSON exporter, the track-builder actor and the scenery spawner are built from this doc, so it gives
exact constants, the function or array that holds each piece of data, and file:line refs.

**Path shorthands used below**

| Short | Real path (repo root = `movecade-v2/`) |
|---|---|
| `kart/` | `app/src/games/kart/` |
| `legacy/` | `app/src/legacy/Experience/kart/engine/` |
| `avatar/` | `app/src/games/avatar/` |
| `render/` | `app/src/games/render/` |
| `public/` | `app/public/` |
| `protocol/` | `protocol/src/` |

---

## 0. Key facts (read first)

1. **Two tracks ship:** `violet-loop` (default) and `sunset-harbor`. The host picks one in the lobby
   (`protocol/maps.ts:9-11`, `kart/tracks/catalog.ts:7-11`). Both are 3 laps (`legacy/store.ts:444-445`).
2. **Both tracks are completely flat.** They have no banking, no elevation change and no jumps. Harbor's
   road is the plane y=0. Violet's road is y = -1.605 ± 0.01 at all 490 profile samples. The schema below
   still has `y`/`bankDeg` fields so that new tracks can use them.
3. **The two tracks are built differently.**
   - **Sunset Harbor is 100% procedural.** Everything comes from one closed uniform Catmull-Rom spline
     through 29 control points (`kart/tracks/harbor.ts:2-36`). The road, kerbs, walls, beach and every
     prop are built from `harborPoint(t, offset)`. Walls are analytic: the offsets are ±16.3 m.
   - **Violet Loop is an imported GLB** (`public/models/violet-loop.glb`, 1.81 MB, Draco + WebP, about 460k
     tris), mounted at `position [155,-28,15]` and `scale 0.08`. Its walls are the steep faces of the GLB
     meshes. Gameplay data is a hand-authored 55-point racing line (`kart/tracks/violetLoop.ts:35-143`)
     plus a **baked cross-section profile** (`kart/tracks/violetProfile.ts`). The profile has 490 samples
     every 3 m, giving road edge, wall, wall top and ground behind per side.
4. **"Figure-8" is a misnomer.** Neither centreline crosses itself, so there is no at-grade crossing to
   model. Violet's two lobes pass within 38 m of each other ("Upper loop 1" ↔ "S-curve 1"). That is why
   recovery has branch-skip detection (`legacy/trackRecovery.ts:68-76`).
5. **Players are karts with a seated, skinned avatar driver.** The driver is the shared Avatar4 body,
   6 faces × 2 bodies. Identity shows through kart paint (6 colours), vehicle (kart/moto/coupe/truck), the
   avatar's look, and a floating name plate bordered in the paint colour.
6. **Data export can be pure Node.** Every gameplay array is in modules with no DOM dependency. I verified
   this: Node 22 `--experimental-strip-types` imported `trackside.ts`, `violetLoop.ts`, `violetProfile.ts`
   and `harbor.ts` directly and reproduced the Violet trackside plan. Only **canvas-drawn textures** (signs,
   banners, Harbor's procedural textures, the boost-pad arrow) need a canvas: node-canvas or a browser.
   Violet's profile is already baked, so no render-time raycast is needed unless the GLB changes.

### 0.1 Shipped code path: live vs dead files

The shipped race runs on the **legacy engine**. The v2 `kart/` tree is the data and asset layer it imports.
Chain: `kart/KartCanvas.tsx` (configures the track) → `@legacy/games/kart/KartRaceCanvas.tsx` →
`legacy/MultiplayerApp.tsx:395` → `legacy/MultiplayerTrackScene.tsx`. That file mounts either
`legacy/models/violet-loop.jsx` (Violet) or `kart/tracks/HarborTrack.tsx` (Harbor), plus
`legacy/MultiplayerPowerups.tsx`, `legacy/BoostPads.jsx`, `legacy/MultiplayerKart.tsx` ×N
(`MultiplayerTrackScene.tsx:77-101`). All legacy readers get track data through `legacy/trackConstants.ts`,
which is just `export * from '@app/games/kart/tracks/catalog'`. So **the Sunset Harbor spline and every
per-track array the shipped game uses are the `kart/tracks/*` modules documented here.**

| Status | Files |
|---|---|
| **Live (shipped)** — track data | `kart/tracks/catalog.ts`, `violetLoop.ts`, `violetProfile.ts`, `harbor.ts`, `harborBoundary.ts`, `meshBoundary.ts`, `trackColliders.ts`; `legacy/trackConstants.ts` (re-export); `legacy/store.ts` (`TRACK_CHECKPOINTS`, `CHECKPOINT_RADIUS`, `RACE_CONFIG`); `legacy/spawnGrid.ts`; `legacy/trackRecovery.ts`; `legacy/hooks/useCheckpointDetection.ts` |
| **Live** — track rendering/scenery | `legacy/MultiplayerTrackScene.tsx`, `legacy/models/violet-loop.jsx`, `kart/tracks/HarborTrack.tsx`, `HarborMotion.tsx`, `harborGeometry.ts`, `harborTextures.ts`, `VioletTrackside.tsx`, `VioletTrees.tsx`, `violetBanner.tsx`; `kart/scene/trackside.ts`, `tracksideGeometry.ts`, `violetTrees.ts`, `trackSurfaces.ts`, `worldSurface.ts`, `kartLook.ts`, `kartPost.ts`, `kartMaterials.ts` |
| **Live** — gameplay props/actors | `legacy/MultiplayerPowerups.tsx` (item boxes), `legacy/BoostPads.jsx`, `legacy/MultiplayerKart.tsx` (collision, surfaces, name plate), `legacy/models/Kart.jsx`, `kart/vehicles.ts`, `kart/kartPaint.ts`, `kart/kartCollisionMeshes.ts`, `kart/itemCollisionMeshes.ts`, `kart/boostPad.ts`, `kart/cameraGround.ts`; `legacy/misc/KartLighting.tsx`, `KartViewPost.tsx`, `legacy/SplitScreenRenderer.tsx` |
| **DEV-only** (`import.meta.env.DEV`, 'D'-key single player) | `legacy/App.jsx`, `legacy/TrackScene.jsx`, `legacy/Powerups.jsx`, `legacy/PlayerController.jsx`, `legacy/ColorGradingEffect.jsx`, `legacy/Composer.jsx`, `textures/lensDirt.jpg` (`KartRaceCanvas.tsx:26-30`) |
| **Dead** | `legacy/TracksideDressing.jsx` (imported nowhere); `legacy/CheckpointDebugger.tsx` (`SHOW_CHECKPOINT_DEBUG = false`); the barrier recolour in `violet-loop.jsx:79-109` (never matches); GLB node `Object_22` (not rendered); `models/movecade-kart.glb`; `game-assets/kart/avatar.glb` (fallback that is never reached). Duplicates: `violetLoop.ts:283-357` has its own nearest-waypoint, waypoint and SVG helpers, but the game calls the `catalog.ts:92-163` copies, which follow the active track |

---

## 1. Coordinate system and units (source → UE)

Source (three.js): right-handed, **Y up**, **metres**. A kart at yaw θ (`rotation.y`, radians) drives along
`forward = (-sin θ, -cos θ)` in (x, z), so **yaw 0 faces −Z** (`legacy/MultiplayerKart.tsx:1137-1138`,
`kart/boostPad.ts:1`). The lateral "offset" used everywhere (`harborPoint(t, offset)`, `sideNormal(sample, side)`)
is **positive on the driver's right**: `n = (-tz, tx)` (`kart/tracks/harbor.ts:32-34`, `kart/scene/trackside.ts:64-66`).

UE: left-handed, **Z up**, **centimetres**, X forward, Y right. Mapping (keeps forward/right/up):

```
UE.X = -z * 100        UE.Y = x * 100        UE.Z = (y + yRebase) * 100
UE.Yaw(deg) = -θ * 180/π                         (pitch = roll = 0 on both tracks)
UE normal/offset direction for (nx, nz) = (-nz, nx)
```

`yRebase`: the web physics runs on the logical plane y=0 and drops the drawn kart onto the road with a
raycast (`kart/tracks/catalog.ts:28-38`). Violet's tarmac sits at `VIOLET_GROUND_Y = -1.605` and Harbor's at
`HARBOR_GROUND_Y = 0` (`catalog.ts:18-19`). **Recommendation: rebase Violet by +1.605 m** so both roads are at
UE Z=0. Apply this to the GLB, trees, trackside and banner. Spawn grid y=0 and item-box y=1 are then correct
on both tracks. Chase-camera tuning is defined relative to Violet's road (`kart/cameraGround.ts:13-24`), so
camera offsets read as "height above tarmac" once rebased.

The exporter should **emit source coordinates unchanged** (`"coordinateSystem": "three-rh-yup-m"`). The UE
importer converts them once, in one function. Doing it this way keeps the JSON diffable against the TS
source.

---

## 2. Track catalog

### 2.1 Catalog entries (`kart/tracks/catalog.ts:7-11`)

| id | name | description (lobby) | UI colour | default | ground Y | lap length* | surface width |
|---|---|---|---|---|---|---|---|
| `violet-loop` | Violet Loop | The original figure-eight circuit | `#b8a1ff` | yes (`protocol/maps.ts:9`) | -1.605 | 1469.7 m | varies 18–58 m (edge-to-edge) |
| `sunset-harbor` | Sunset Harbor | Lighthouse bends · coral tunnel · marina | `#f3ad74` | no | 0 | 1612.1 m | road 28 m; walls ±16.3 m |

*Arc length of the centreline. Computed from the same math (Violet by `densifyLoop(...,3)`, Harbor by
sampling `harborPoint` 8700×).

`configureKartTrack(id)` (`catalog.ts:52-80`) swaps these module-level bindings, which the rest of the game
reads as live imports: `TRACK_PATH_POINTS`, `TRACK_LANDMARKS`, `STATIC_CAMERAS`, `MINIMAP_BOUNDS/SCALE`,
`DIRECT_POWERUP_POSITIONS`, `TRACK_BOOST_PADS`, `TRACK_SVG_PATH`, `TRACK_GROUND_Y`, `ACTIVE_TRACK_ID/NAME`.
**The exporter must call `configureKartTrack(id)` for each track and then read those bindings.** Harbor's
landmarks, cameras, items and pads exist only after that call. `KartCanvas` configures the track before the
physics scene mounts (`kart/KartCanvas.tsx:72-83`). `MultiplayerTrackScene` renders the Violet GLB or
`<HarborTrack/>` (`legacy/MultiplayerTrackScene.tsx:80-82`).

### 2.2 Race rules shared by both tracks

| Rule | Value | Ref |
|---|---|---|
| Laps | 3 | `legacy/store.ts:445` |
| Countdown | 5 s (5-4-3-2-1-GO, 6000 ms) | `store.ts:446-447` |
| Hard race cap | 300 000 ms | `store.ts:448` |
| Finish grace after leader | 30 000 ms | `store.ts:451` |
| Intro | 5 static cameras × 1200 ms | `store.ts:456-458` |
| Max players | 4 | `kart/index.ts:14` |
| Checkpoint radius | 24 m (2D XZ distance, edge-triggered on entering) | `store.ts:340`, `legacy/hooks/useCheckpointDetection.ts:81-133` |
| Checkpoint list | 9: `finish` (index 0, `isFinishLine`) then 8 landmarks in order | `store.ts:328-338` |
| Starting next-checkpoint | index **1**: the finish only counts after all of 1..8 | `store.ts:343-345` |
| Lap complete | entering `finish` while it is the next checkpoint → lap++ → next = 1 | `useCheckpointDetection.ts:126-133` |
| Lap progress % | `(prevIdx + (1 - dist(next)/segLen)) / 9 * 100`, capped at 99.9 | `useCheckpointDetection.ts:47-71` |
| Off-track recovery | more than 64 m from the nearest waypoint, **or** ≥3 waypoints past the still-required checkpoint (and < half a lap) → respawn at the waypoint before that checkpoint, facing the next waypoint | `legacy/trackRecovery.ts:3-74` |
| Wrong-way | every 15 frames at speed > 5: `dot(facing, toNextWaypoint) < -0.5` | `legacy/MultiplayerKart.tsx:1570-1585` |
| AI / bot steering | nearest waypoint + look-ahead 2 | `catalog.ts:92-115` |

Checkpoints are **points with a radius**, not gates. They are `TRACK_LANDMARKS` in this order:
`START_LINE, TURN_1_APEX, OUTER_CURVE_APEX, NORTH_HAIRPIN_1, CROSSOVER, UPPER_LOOP_EAST, NORTH_HAIRPIN_2, S_CURVE, FINAL_CURVE`.
For parity in UE, use sphere/cylinder triggers of radius 2400 cm at those points. Do not use line gates.

### 2.3 Start grid (both tracks; `legacy/spawnGrid.ts:9-14`)

A staggered 2×2 grid. All four slots have yaw 0 (facing −Z) and y = 0 (logical plane). The start/finish
landmark is at (3, -30.32), so it is **ahead** of the grid. The first crossing does not count because the
next checkpoint starts at 1.

| slot | x | y | z | yaw |
|---|---|---|---|---|
| 0 | -3.06 | 0 | -22.23 | 0 |
| 1 | 2.96 | 0 | -17.26 | 0 |
| 2 | -0.13 | 0 | -15.05 | 0 |
| 3 | 5.97 | 0 | -9.67 | 0 |

Slots go to players in join order (`store.ts:1244`, `store.ts:469-476`).

### 2.4 Violet Loop data

- **Racing line / AI / recovery**: `TRACK_PATH_POINTS`, 55 labelled `{x,z,label}` points, closed. It starts
  at `(3,-7) 'Start/Finish'` and heads −Z (`kart/tracks/violetLoop.ts:35-143`).
- **Landmarks = checkpoints** (`violetLoop.ts:149-159`):

| idx | id | x | z |
|---|---|---|---|
| 0 | finish (START_LINE) | 3 | -30.32 |
| 1 | turn_1 | 40 | -140 |
| 2 | outer_curve | 256 | -142 |
| 3 | north_hairpin_1 | 305 | -88 |
| 4 | crossover | 110 | 6 |
| 5 | upper_loop | 261 | 33 |
| 6 | north_hairpin_2 | 306 | 74 |
| 7 | s_curve | 156 | 81 |
| 8 | final_curve | 64 | 133 |

- **Item boxes** (`DIRECT_POWERUP_POSITIONS`, `violetLoop.ts:269-274`): 4 boxes in one row across the road
  on "East section 3". Coordinates are `[x,y,z]`: `[164.88,1,-59.22] [164.96,1,-62.16] [165.22,1,-71.48] [165.07,1,-66.37]`.
- **Boost pads** (`catalog.ts:42-47`): position `[x,z]`, three.js yaw, `width` 8 (across) × `length` 4 (along):
  `[205.34,-91.97] rot -0.37`, `[262.08,-36.32] rot -4.42`, `[242.92,27.15] rot -1.52`, `[110.18,121.33] rot -3.99`.
  The GLB also has a painted 4×4 m "speed" quad at x 5.7–9.5, z 62–65.8 (`Object_27`). It has no gameplay effect.
- **Static/intro cameras**: 9 entries `{id,name,position,lookAt}` (`violetLoop.ts:176-239`).
- **Minimap**: bounds `minX -15, maxX 320, minZ -155, maxZ 145` (`violetLoop.ts:248-253`). The SVG path is
  generated by `generateTrackSVGPath` (`catalog.ts:124-163`).

### 2.5 Sunset Harbor data (all derived from `harborPoint`; `kart/tracks/harbor.ts`)

- **Spline**: `HARBOR_CONTROL_POINTS`, 29 `[x,z]` points (`harbor.ts:2-9`). `harborPoint(t, offset)`
  (`harbor.ts:13-36`) evaluates the **uniform closed Catmull-Rom** at `t∈[0,1)`. `t·29` is the segment
  index. It returns `{x, z, nx, nz, yaw}`, where `yaw = atan2(-tx, -tz)` is a three.js yaw.
  t=0 is (3,-7) with yaw 0.
- **Road width**: `HARBOR_ROAD_WIDTH = 28` (`harbor.ts:11`).
- **Navigation path**: `HARBOR_PATH`, 87 samples (about 18.5 m apart) at `harborPoint(i/87)` (`harbor.ts:39`).
  Render geometry uses 870 samples (`HarborTrack.tsx:16`), 1.853 m apart.
- **Sectors** (`HARBOR_SECTORS`, `harbor.ts:40-50`) feed the landmarks (`catalog.ts:59-69`) and the static
  cameras. Cameras sit 30 m off the line at y 16 and look at the line at y 0 (`catalog.ts:70-74`).
  Harbor's START_LINE is overridden to (3, -30.32), not sector 0.
  Sector 0 ("Harbor start", t=0.014) is only used for camera 0.

| idx | checkpoint id (shared names) | sector name | t | x | z |
|---|---|---|---|---|---|
| 0 | finish | (START_LINE override) | — | 3 | -30.32 |
| 1 | turn_1 | Lighthouse bend | 5/29 | -7 | -235 |
| 2 | outer_curve | Coral tunnel | 9/29 | 142 | -110 |
| 3 | north_hairpin_1 | Ocean sweep | 13/29 | 326 | -111 |
| 4 | crossover | Marina chicane | 16/29 | 223 | 30 |
| 5 | upper_loop | Palm headland | 19/29 | 164 | 155 |
| 6 | north_hairpin_2 | Village esses | 22/29 | 63 | 72 |
| 7 | s_curve | Sunset hairpin | 25/29 | -65 | 101 |
| 8 | final_curve | Home straight | 28/29 | 3 | 30 |

(Catmull-Rom passes through its control points, so t = k/29 lands exactly on control point k.)

- **Item boxes**: `HARBOR_ITEMS`. There are 4 rows at t = 2/29, 10/29, 17/29, 26/29, and each row has 3
  boxes at offsets −8/0/+8, all at y 1 (`harbor.ts:57-62`). Computed values:
  `[0.43,1,-113.42] [8,1,-116] [15.57,1,-118.58] | [193.51,1,-136.62] [198,1,-130] [202.49,1,-123.38] |
   [252.99,1,79.45] [245,1,79] [237.01,1,78.55] | [-77.27,1,48.66] [-70,1,52] [-62.73,1,55.34]`.
- **Boost pads**: `HARBOR_BOOST_PADS` at t = 6.2/29, 14.4/29, 19.5/29, size 12 × 6, rotation = spline yaw
  (`harbor.ts:52-55`). Computed values: `(78.93,-224.47) yaw -2.14`, `(281.98,-44.38) yaw 2.09`, `(137.94,148.69) yaw 1.15`.
- **Minimap** bounds: `minX -100, maxX 350, minZ -260, maxZ 180` (`catalog.ts:75`).
- Closest approach between non-adjacent parts of the line: 36.9 m (start straight vs t≈27/29). The walls
  (±16.3) never overlap, but the shoulders and beaches do.

### 2.6 Item box and boost pad behaviour (needed for the scenery prefabs)

| | Value | Ref |
|---|---|---|
| Item box | 2 m cube + inner 1.4 m cube + glow sphere r 1.8 + "?" label on 6 faces, colour `#ffcc00` / emissive `#ff9900` | `legacy/MultiplayerPowerups.tsx:27-40` |
| Item box anim | yaw +1.5 rad/s; bob `y + 0.1·sin(2.5t)`; inner cube +0.5 rad/s | `MultiplayerPowerups.tsx:159-164` |
| Collect | 2D distance < 2.5 m; respawn **per player** after 15 s; one held item max | `MultiplayerPowerups.tsx:95,102,152`; `:229-236` |
| Boost pad | lasts 1.5 s, ×1.8 speed, 2 s per-pad cooldown, +1.5 m trigger padding; local −Z forward | `legacy/BoostPads.jsx:19-22`, `kart/boostPad.ts:1-13` |
| Boost pad Y | down-raycast to meshes named ground/track/dirt; normal.y > 0.5 | `BoostPads.jsx:106-139`, render group `:236` |
| Boost pad look | canvas 128² orange→red gradient, diagonal bands, yellow border, yellow up-arrow; emissive pulse 0.35+; rims | `BoostPads.jsx:40-97`, `:140-180` |

---

## 3. Geometry definition and what to export

### 3.1 Sunset Harbor: everything is offsets from one spline

**Ribbons** come from `harborRibbon(inner, outer, innerY, outerY, striped)` (`kart/tracks/HarborTrack.tsx:20-63`).
Each one is a closed quad strip with 871 rows (`SAMPLES = 870`). Every row has a vertex at
`harborPoint(i/870, inner)` (height innerY) and one at `harborPoint(i/870, outer)` (height outerY).
UVs are `u = offset/8`, `v = along/8` (one tile = 8 m). Asphalt carries a per-row tone in 0.95–1.05 as
vertex colour. Curbs alternate red `#d7312a` and cream `#f7f4ec` every 4 rows (about 7.4 m).

| ribbon (`HarborTrack.tsx:843-861`) | inner | outer | innerY | outerY | mesh name / role | collision? |
|---|---|---|---|---|---|---|
| road | -14 | 14 | 0 | 0 | `ground-harbor-road`, asphalt | kart ground |
| leftEdge / rightEdge | ∓13.7 | ∓13.3 | 0.02 | 0.02 | white edge line `#fff0cc` | no (decor) |
| shoulder | -17 | 17 | -0.04 | -0.04 | `ground-dirt-shoulder` → **dirt** | kart ground |
| leftCurb / rightCurb | -15.5/14 | -14/15.5 | 0.035 | 0.035 | `ground-harbor-curb`, striped | kart ground |
| leftWall / rightWall | ∓16.3 | ∓16.3 | 0 | 1.3 | `harbor-barrier` (vertical 1.3 m) | walls + items |
| leftCap / rightCap | ∓16.7/∓15.9 | | 1.3 | 1.3 | wall cap `#fff0cc` | no |
| leftBeach / rightBeach | -40/17 | -17/40 | -6.9/-0.06 | -0.06/-6.9 | sand slope to the sea | no |
| leftSurf / rightSurf | ∓43/39 | ∓39/43 | -6.92 | | surf `#f4f7ec` α 0.85 | no |
| leftShallows / rightShallows | ∓52/43 | ∓43/52 | -6.93 | | water `#7fd0cc` α 0.7 | no |
| sea plane | 5000×5000 at (100,-7,-40) | | | | `#5fb3b3` + scrolling water tex | no (`HarborTrack.tsx:957-960`) |

Beach height is `beachY(o)`: 0 for |o| ≤ 17, −6.9 for |o| ≥ 40, linear in between (`HarborTrack.tsx:438-441`, `HarborMotion.tsx:15-18`).

**Walls** are analytic: `HARBOR_WALL_OFFSET = 16.3`, `HARBOR_KART_RADIUS = 1.15`. The walls are two polylines
of 870 segments at `harborPoint(i/870, ±16.3)`. `constrainHarborMovement` sweeps the kart's circle in
0.65 m substeps and pushes it out of each wall segment (`kart/tracks/harborBoundary.ts:3-62`). The usable
half-width for the kart centre is 15.15 m.

**Other spline-derived geometry** (`kart/tracks/harborGeometry.ts`):
- `tunnelVault(radius, rise)` (`:5-28`): a half-pipe vault over the Coral tunnel, t ∈ [8.05/29, 9.45/29],
  64×32 grid. It is used twice, (21,17) and (25,22) (`HarborTrack.tsx:149-150`), plus 9 torus ribs and
  portals (`:151-163`). It is decoration only and stays out of the drive envelope.
- `palmFrond`, `boatHull` (`:31-73`): procedural meshes for props.

### 3.2 Violet Loop: GLB plus baked profile

**GLB mount**: `<group position={[155,-28,15]} scale={0.08}>` (`legacy/models/violet-loop.jsx:113`).
gltfjsx output, loaded with `useGLTF('/models/violet-loop.glb')` (`:26`). The nodes have no transforms of
their own. World bounds below already include the mount transform:

| node | material | role (`kart/scene/trackSurfaces.ts:83-87`) | rendered name | tris | world bounds (x / y / z) |
|---|---|---|---|---|---|
| Object_10 | material_5 | start-line structure, gantry | `ground` | 2 340 | -16.8..180.8 / -1.8..20.5 / -32.1..66.3 |
| Object_11 | material_6 | sand verge → **dirt** | `ground dirt` | 17 348 | -29.7..336.8 / -1.8..-1.3 / -164.8..155.5 |
| Object_12 | material_7 | (misc ground) | `ground` | 5 196 | -37.4..344.9 / -4.1..10.1 / -172.9..163.3 |
| Object_13 | material_8 | barrier blocks + gantry (**main walls**) | `ground` | 181 224 | -31.6..338.7 / -1.7..17.8 / -166.7..157.4 |
| Object_18 | material_12 | hills, mountains, 53 voxel trees | `ground` | 112 952 | -3403..3169 / -161..1033 / -2824..3060 |
| Object_22 | material_16 | gantry slab detail (**not rendered** by the JSX) | — | 2 908 | -12.3..14 / 12.2..15.7 / -32..-30.5 |
| Object_24 | material_18 | road asphalt | `ground` (also `trackScene`) | 15 380 | -12.9..320.5 / -1.8..-1.6 / -155.7..147.2 |
| Object_25 | material_19 | edge lines + chequers | `ground` | 10 444 | -14.5..320.8 / -1.7..-1.4 / -153.7..143.6 |
| Object_27 | material_21 | painted speed quad | `ground speed` | 4 | 5.7..9.5 / -1.6 / 62..65.8 |
| Object_47 | material_3 | rocks + cliffs | `ground` | 111 972 | -34.6..341.4 / -10.9..15.2 / -169.5..147.8 |

(Measured from the GLB JSON accessors. JSX meshes are at `violet-loop.jsx:114-122`.) Every material is a
tiny WebP palette texture (8×8 atlas; most images are 74–2110 bytes, one is 18 KB) with roughness 0.6 and
metallic 0. Each surface is effectively one flat colour, picked by UV.

**Collision on Violet**: every ground mesh above gets a BVH (`violet-loop.jsx:58-73`). Then
`markTrackCollidersReady()` bumps a revision so the karts re-gather (`kart/tracks/trackColliders.ts:1-20`).
Walls are **any triangle with |normal.y| < 0.7**. `constrainMeshMovement` sweeps a 1.15 m disc in 0.65 m
substeps and pushes it out horizontally; height never separates (`kart/tracks/meshBoundary.ts:27-36`, `:94-173`).

**Baked profile** (`kart/tracks/violetProfile.ts:1-13`): `VIOLET_PROFILE_STEP = 3`. `VIOLET_PROFILE` is a
flat `number[]` with **11 numbers per sample** (`PROFILE_STRIDE`, `kart/scene/trackside.ts:272`) and
**490 samples**. These line up 1:1 with `densifyLoop(TRACK_PATH_POINTS, 3)`:

```
[ roadY,
  side+1: edge, wall(-1 = none), wallTop, back(-1 = none), backY,
  side-1: edge, wall,            wallTop, back,            backY ]
```

All distances are metres from the centreline. `wallTop` and `backY` are heights relative to `roadY`.
The data is decoded by `decodeTrackProfile` (`trackside.ts:278-293`). Measured ranges: roadY -1.61..-1.60;
edge-to-edge road width 18–58.2 m; 101 of the 490 samples have no wall on at least one side (open run-off).
The header says it was made "by down-rays against the live GLB". **The generator script is not in the
repo** (no match in `scripts/` or `tools/`). If the GLB changes, write a new one. The pattern to follow is
`scripts/probe-violet-walls.mjs`: Playwright plus the DEV handle `globalThis.__movecade.games.kart.scene()`
(`legacy/MultiplayerApp.tsx:227-247`).

**Centreline resampler** `densifyLoop(points, step)` (`trackside.ts:26-61`): closed uniform Catmull-Rom (the
same formula as `harborPoint`), subdivided at `step/4`, then resampled at constant arc length `step`. Each
`PathSample` is `{x, z, tx, tz, s}`. Curvature: `curvatures(samples, window=3)` (`trackside.ts:72-85`), signed;
positive means turning toward side +1.

### 3.3 Surfaces and how the kart classifies them

- A downward ray from the kart (from y+1, `far` 3) hits the collision meshes **every ~10 frames**. The first
  near-horizontal hit (|n.y| ≥ 0.7) is "dirt" if its mesh name contains any of
  `dirt, sand, grass, offroad, mud, gravel` (`legacy/MultiplayerKart.tsx:131-132`, `:1588-1609`).
  Dirt → **speed ×0.75** (`MultiplayerKart.tsx:1109-1110`) and a light haptic rumble (`:1552-1568`).
  - Violet: only `ground dirt` (Object_11, sand verge) counts.
  - Harbor: only `ground-dirt-shoulder`. Curbs (|o| 14–15.5) and road (|o| < 14) sit above the shoulder, so
    in practice dirt is only the thin band from 15.5 to 15.15 that the kart centre can reach.
- Kart colliders = every mesh except kart parts, translucent meshes (opacity < 0.5), anything under a
  `userData.kartDecoration`/`kartActor` node, and names matching
  `/start|finish|banner|line|flag|spawn|player|checkpoint|waypoint/` (`kart/kartCollisionMeshes.ts:8-41`).
  Watch out: Violet meshes are all named "ground", so the name filter never removes them.
- Item/projectile colliders: on Harbor, only `harbor-barrier`. On Violet, the same filter as karts
  (`kart/itemCollisionMeshes.ts:4-29`).

### 3.4 Banking, heights, jumps

There are none in source data. Harbor y is constant (0). Violet roadY is constant (−1.61/−1.60). In the
JSON, export `y` per sample anyway, with `bankDeg: 0` and `grade: 0`.

---

## 4. Export pipeline

### 4.1 What runs where

| Data | Source module(s) | Node-importable? | Notes |
|---|---|---|---|
| Catalog, landmarks, cameras, items, pads, minimap, SVG | `kart/tracks/catalog.ts` (+ `violetLoop.ts`, `harbor.ts`) | yes (needs `zustand`) | call `configureKartTrack(id)` first |
| Harbor spline, path, sectors, walls | `harbor.ts`, `harborBoundary.ts` | yes, no deps | |
| Violet centreline + profile | `violetLoop.ts`, `violetProfile.ts`, `scene/trackside.ts` | yes, no deps (**verified** with `node --experimental-strip-types`) | |
| Violet trackside plan (kerb runs, stands, banners, lamps) | `trackside.planTrackside` | yes (**verified**: 34 kerb runs, 2 stands, 100 banners, 37 lamps) | |
| Violet trees | `scene/violetTrees.ts` (`VIOLET_TREES`, `placeTrees`) | yes (module imports `three`) | 53 trees |
| Spawn grid | `legacy/spawnGrid.ts` | yes, no deps | |
| Checkpoint order, radius, laps | `legacy/store.ts` | **no** (big module, logger alias `@/`) | copy 9-entry order + constants (§2.2) into the exporter |
| Prop/trackside **geometry** (kerbs, grandstand, lamp, Harbor scenery batches, tunnel) | `scene/tracksideGeometry.ts`, `HarborTrack.tsx#buildHarborScenery`, `harborGeometry.ts` | yes with `three`; **but `buildHarborScenery` and the inline ribbons are not exported** | use the esbuild stub plugin (below) or a browser dump |
| Canvas textures (Harbor asphalt/sand/water/stone/grain/cobble, both start banners, Violet banner boards, boost-pad arrow, item "?") | `harborTextures.ts`, `violetBanner.tsx`, `VioletTrackside.tsx`, `HarborTrack.tsx:821-841`, `BoostPads.jsx:40-97` | needs a 2D canvas | node `@napi-rs/canvas`, or dump from the browser |
| Violet circuit meshes | `public/models/violet-loop.glb` | file copy + conversion | Draco + WebP (see §4.4) |

### 4.2 Recommended exporter (`MyProject/Tools/track-export/`, never inside the movecade repo)

1. Its own `package.json` with `three@0.181.x`, `zustand`, `esbuild`, `@gltf-transform/core`, `@napi-rs/canvas`.
2. `esbuild.build({ entryPoints:['export.ts'], bundle:true, platform:'node', nodePaths:[<tools>/node_modules] })`
   reading sources straight from `movecade-v2/app/src/...`. Use a small **stub plugin** for
   `react`, `@react-three/fiber`, `@react-three/drei`, `../../render/quality`, `../scene/worldSurface`, `../../render/surface`.
   Each stub exports no-op functions. Add an `onLoad` hook for `HarborTrack.tsx` that appends
   `export { buildHarborScenery };` **to the in-memory source**. That exposes the prop batches without
   editing the repo. Point `render/canvasTexture` at a stub that draws into `@napi-rs/canvas` and records the
   PNG.
3. For each track: `configureKartTrack(id)` → build the JSON (§4.3) → write `<id>.track.json`.
4. Geometry → GLB with `@gltf-transform/core`. GLTFExporter needs `FileReader`, which Node lacks, so write
   the `BufferGeometry` attributes (position, normal, color, uv, index) yourself:
   - `harbor_scenery.glb`: ~35 vertex-coloured batches, one per hex colour (`HarborTrack.tsx:72-90`, `:813-818`).
     The node name is the colour.
   - `harbor_ribbons.glb`: the 17 ribbons in §3.1, each named as in that table.
   - `violet_kerbs.glb`: `kerbGeometry(samples, plan.kerbs)`.
   - Prefabs `grandstand.glb`, `lamp_post.glb`, `lamp_head.glb`, `banner_board.glb`, `tree_canopy.glb`, `tree_trunk.glb`.
5. Optional cross-check / fallback: run the movecade dev server **in a separate clone** (see below) and use
   Playwright on `/host/kart?bots=1` (DEV) or `app/harbor-review.html` (`app/src/debug/HarborReview.tsx`).
   `GLTFExporter` the live scene from `__movecade.games.kart.scene()`, and dump every `CanvasTexture.image`
   with `toDataURL`.
6. Read-only rule: anything that needs `npm ci` or a dev server for movecade runs in a **local clone**
   (`git clone --local movecade-v2 Tools/track-export/.cache/movecade`). The user's checkout stays untouched.

### 4.3 JSON schema (`<trackId>.track.json`, schemaVersion 1)

TypeScript-style definition. Every number is in source units (m, rad, three.js axes). UE converts per §1.

```ts
interface TrackFile {
  schemaVersion: 1;
  coordinateSystem: 'three-rh-yup-m';       // UE: X=-z, Y=x, Z=y(+rebase); yawUE=-yaw
  source: { repo: 'movecade-v2'; commit: string; exportedAt: string; files: string[] };

  track: {
    id: 'violet-loop' | 'sunset-harbor';
    name: string; description: string; uiColor: string;   // catalog.ts:8-11
    laps: 3;
    groundY: number;            // -1.605 | 0  (catalog.ts:18-19)
    suggestedRebaseY: number;   // +1.605 | 0
    closedLoop: true;
    lapLengthM: number;
    trackType: 'analytic-spline' | 'imported-mesh';
  };

  /** Authored curve. UE: USplineComponent, CurveCustomTangent, tangent_i=(P[i+1]-P[i-1])/2, input key = i. */
  spline: {
    kind: 'catmull-rom-uniform-closed';
    controlPoints: [x: number, z: number][];   // Harbor: HARBOR_CONTROL_POINTS (29); Violet: TRACK_PATH_POINTS (55)
    labels?: string[];                         // Violet waypoint labels
  };

  /** Dense, arc-length-indexed cross-sections — what the builder actually sweeps. */
  samples: {
    spacingM: number;   // Violet 3 (profile grid); Harbor 1612.1/870 ≈ 1.853 (uniform t, NOT uniform s — store both)
    points: Array<{
      i: number; s: number; t: number;          // t = normalised spline param (Harbor), s = arc length
      x: number; y: number; z: number;          // y = roadY
      tx: number; tz: number; yaw: number;      // unit tangent, three yaw
      curvature: number;                        // trackside.curvatures, signed (+ = toward right)
      bankDeg: 0; grade: 0;
      surface: 'asphalt';
      right: SideProfile; left: SideProfile;    // right = side +1, left = side -1
    }>;
  };
  // SideProfile: distances from centreline (m), heights relative to roadY
  // { edge: number; curbIn?: number; curbOut?: number; wall: number | null; wallTop: number;
  //   back: number | null; backY: number; shoulder?: number }
  // Harbor: constant {edge 14, curbIn 14, curbOut 15.5, wall 16.3, wallTop 1.3, shoulder 17, back 17, backY 0}
  // Violet: decodeTrackProfile(densifyLoop(TRACK_PATH_POINTS,3), VIOLET_PROFILE)

  navigation: { waypoints: { x: number; z: number; label?: string }[]; lookAhead: 2 };  // TRACK_PATH_POINTS (Harbor: HARBOR_PATH, 87)

  checkpoints: Array<{ index: number; id: string; x: number; z: number; radius: 24; isFinish?: true; sectorName?: string }>;
  startCheckpointIndex: 1;
  startLine: { x: 3; z: -30.32; yaw: 0; width: number };   // width = road width at that sample
  spawnGrid: Array<{ slot: 0 | 1 | 2 | 3; x: number; y: 0; z: number; yaw: 0 }>;

  itemBoxes: Array<{ id: string; x: number; y: number; z: number; row: number }>;
  itemBoxConfig: { size: 2; collectRadius: 2.5; respawnS: 15; spinRadPerS: 1.5; bobAmp: 0.1; bobRadPerS: 2.5; perPlayer: true };
  boostPads: Array<{ x: number; z: number; yaw: number; width: number; length: number }>;
  boostConfig: { durationS: 1.5; multiplier: 1.8; cooldownS: 2; triggerPaddingM: 1.5 };

  staticCameras: Array<{ id: string; name: string; position: [number, number, number]; lookAt: [number, number, number] }>;
  minimap: { bounds: { minX: number; maxX: number; minZ: number; maxZ: number }; svgPath: string };
  recovery: { maxDistanceM: 64; skipWaypoints: 3 };

  collision: {
    kartRadiusM: 1.15; substepM: 0.65; wallNormalY: 0.7;
    walls: Array<{ side: 1 | -1; height: number; polyline: [x: number, z: number][] }>;  // Harbor: 870 pts at ±16.3; Violet: derived from profile (fallback only)
    meshColliders?: Array<{ file: string; node: string; role: 'ground' | 'wall' | 'mixed'; surface: 'asphalt' | 'dirt' }>; // Violet GLB nodes
  };
  surfaces: Record<'asphalt' | 'dirt', { speedMultiplier: 1 | 0.75; meshKeywords?: string[] }>;

  ribbons?: Array<{ name: string; inner: number; outer: number; innerY: number; outerY: number;
                    material: string; collision: 'ground' | 'wall' | 'none'; surface?: string; stripePeriodSamples?: number }>; // Harbor §3.1

  meshes: Array<{ file: string; nodes?: string[]; transform?: { position: [number, number, number]; scale: number };
                  collision: boolean; nanite: boolean }>;   // violet-loop.glb (155,-28,15 ×0.08), harbor_*.glb, violet_kerbs.glb

  scenery: {
    instances: Array<{ prefab: string; x: number; y: number; z: number; yaw: number;
                       scale?: [number, number, number]; tint?: string; variant?: number; side?: 1 | -1 }>;
    // Violet: grandstand×plan.stands, lamp_post+lamp_head×plan.lamps, banner_board×plan.banners (variant = i%4),
    //         tree_trunk/tree_canopy×53 (scale from placeTrees, tint TREE_FOLIAGE[...])
    kerbRuns?: Array<{ side: 1 | -1; fromSample: number; toSample: number }>;
    animated: Array<{ type: 'lighthouseBeam' | 'ferrisWheel' | 'ferry' | 'cargoShip' | 'jetSki' | 'gullFlock'
                          | 'fountain' | 'buoy'; position: [number, number, number]; yaw?: number;
                      params: Record<string, number> }>;            // §5.5
    signs: Array<{ name: string; position: [number, number, number]; size: [number, number]; texture: string }>;
  };

  look: KartTrackLook;   // kart/scene/kartLook.ts:15-32, exported verbatim (+ night variant)
  textures: Array<{ name: string; file: string; tileM?: number; srgb: boolean }>;
}
```

Notes:
- **Keep `t` and `s` apart.** Harbor positions (sectors, items, pads, props) are authored in **spline
  parameter t**, which is not arc length. A UE `GetLocationAtDistanceAlongSpline` will not land on them.
  Either use the exported xyz, or `GetLocationAtSplineInputKey(t*29)` on a spline built with the exact
  Catmull-Rom tangents above.
- The UE spline gives an **exact** match. Uniform Catmull-Rom equals Hermite with tangent `(P[i+1]-P[i-1])/2`
  per unit input key, which is the same formula as `harborPoint` and `densifyLoop`.

### 4.4 Converting the Violet GLB

`violet-loop.glb` uses `KHR_draco_mesh_compression` + `EXT_texture_webp`. Before relying on it, confirm that
UE 5.8's Interchange glTF importer supports both. The safe path is to pre-decode with a gltf-transform
script (decode Draco → plain accessors; WebP → PNG; bake the `[155,-28,15] ×0.08` mount plus the +1.605
rebase into the node transform) and import that. The fallback is Blender (import glTF → export FBX/GLB).
Keep the node names: the importer maps roles (§3.2) to collision, surfaces and materials by name.
**Provenance:** this model is outside the asset-ingest ledger, and `docs/ASSETS.md:124-134` describes it only
as an owner-approved open-source legacy model (named there `mario-circuit-test-transformed.glb`). Verify the
licence before it ships in a commercial UE build, or treat it as a placeholder until the upgrade rebuild.

---

## 5. Scenery, lighting and materials

### 5.1 Violet trackside (decorative; `kart/tracks/VioletTrackside.tsx`, `kart/scene/trackside.ts`, `kart/scene/tracksideGeometry.ts`)

The rule-based planner `planTrackside(samples, DEFAULT_TRACKSIDE_RULES)` (`trackside.ts:172-269`) uses the
rules at `trackside.ts:133-143`:

| Rule | Value |
|---|---|
| corner if \|curvature\| ≥ | 0.018 1/m; kerb run ≥ 4 samples, padded 1 sample, **both sides** |
| straight if \|curvature\| ≤ | 0.006; grandstand needs 14 straight samples centred |
| grandstands | max 4, ≥ 90 m apart; placed `back + 7` m out behind a wall that has ground behind it; nearest wall preferred; clearance 3+8 m |
| banners | every 24 m (8 samples), both sides, on the wall's road face (`wall − 0.05`), at `roadY + wallTop`; skipped where \|curvature\| > 0.0288 or within 14 m of a stand |
| lamps | every 30 m, alternating sides, offset half a period, `back + 1.2` m out at `roadY + backY`; not within 16 m of a stand |
| clearance | `clearOfRoad`: every candidate stays ≥ clearance off **every** sample's road edge, including the other lobe (`trackside.ts:154-165`) |

Current result: **34 kerb runs, 2 grandstands** (`(211.4,-1.91,-48.7) yaw 0.23`, `(38.1,-1.91,-55.9) yaw -1.46`),
**100 banners, 37 lamps**. Placement yaw turns local +Z toward the road (`trackside.ts:145-148`).

Prefab geometry (`tracksideGeometry.ts`):
- **Kerb ribbon** `kerbGeometry` (`:27-87`): `DEFAULT_KERB` = inset 1.3, outset 0.35, stripe 1.5 m, crown 0.09, lift 0.03; red `#d7312a` / white `#f4f1ea`; hard-edged vertex colours.
- **Grandstand** `grandstandGeometry(width=30, tiers=6)` (`:120-156`): 1.5 m tier depth, 0.9 m rise; seat rows alternate `#6d4bd8`/`#4d6ad8`; seeded random box crowd (8 colours, `:112`); white cantilever roof; violet fascia `#8b5cf6`; plinth runs below ground. One vertex-coloured mesh.
- **Lamp post** 9 m pole + arm (`:159-170`, `#3a3f4b`, metalness 0.6). **Lamp head** box (`:172-176`), emissive `#ffe2a8`, intensity 0.4 by day and **4 at night** (`VioletTrackside.tsx:87-94`).
- **Banner board** plane 6×1.2 (`:179-183`). 4 faces, one per `i % 4` (`VioletTrackside.tsx:17-22`): MOVECADE / PARTY GAMES ON ONE SCREEN (`#2b1553`), VIOLET LOOP / GRAND PRIX, KART RACER / DRIFT · BOOST · WIN, yellow chevrons. Each face is a canvas 512×102.
- **Gating:** trackside and trees render only above the `low` quality tier (`violet-loop.jsx:127-129`).

**Start banner** (`kart/tracks/violetBanner.tsx`): plane 26.4×4.4 at `(0.86, 13.94, -30.82)`, facing +Z,
`meshBasicMaterial` (unlit). Canvas 1920×320, drawn by `drawVioletLoopBanner` (`:50-81`): MOVECADE / V I O L E T   L O O P,
violet panel with a glow. The GLB gantry slab face behind it is at z=-30.94, x -12.69..14.41, y 11.48..16.40 (`:21-26`).

### 5.2 Violet trees (`kart/scene/violetTrees.ts`, `kart/tracks/VioletTrees.tsx`)

The GLB's 53 voxel trees are **discarded in the shader**. Palette cells `VIOLET_TREE_CELLS`
(`trackSurfaces.ts:71-73`) are cut out of material_12 via `discardUv`. The geometry stays for collision,
and each tree is redrawn as 2 instanced meshes.
Row `[x, z, groundY, canopyBottomY, canopyTopY, canopyWidth]`. There are 53 rows, 600–1400 m from the road
(`violetTrees.ts:20-74`).
`placeTrees` (`:96-113`): trunk radius `0.1·w`, height up to canopy base + 25% of canopy height. Canopy is
`1.2·w` wide, `1.04·h` tall, with yaw from `treeHash`. Foliage swatches are `#3f8a3a #4f9a3c #357a3d #5c9f45`
and bark is `#6b4a30` (`:116-117`). Canopy = 4 displaced icosahedron lobes with vertex-colour AO (`:128-171`).
In UE: two HISMs (trunk, canopy) with per-instance custom data for tint, or SpeedTree/Fab trees in the upgrade.

### 5.3 Harbor static scenery (`HarborTrack.tsx#buildHarborScenery`, `:67-818`)

Everything is **baked by colour** into about 35 merged meshes. There are no external models or downloads.
Each batch uses `meshStandardMaterial({color, map: grain, roughness 0.86, side: DoubleSide})` (`:961-965`).
Palette: `#e88c68 #e3ba68 #6da9a0 #efcf99 #98b5b5` (`:103`). Inventory by section:

| Section | What | Lines |
|---|---|---|
| Coastline | 110 sandstone rocks under the road (y -10); 74 grass shelves at ±24 with palms on 2 of every 3 (10-segment curved trunks, 9 fronds, nuts) | 104-131 |
| Lighthouse | inside the opening hairpin at (-11, -190): base, 24 m tower, red bands, lantern, roof, gallery rail + 24 posts, finial | 133-145, 339-347 |
| Coral tunnel | two vaults, 9 ribs, portal lamps, flanking rocks; 40 corals/anemones/starfish | 147-163, 741-760 |
| Village | 24 pastel houses (10 on the left near the start, 14 on the right after t 21/29), pitched tile roofs, chimneys, shuttered windows, balconies, awnings, flower pots | 165-239 |
| Festival | pennant strings across the road at t 0.035, 0.069, 0.75, 0.80, 0.85 (y 13, 24 flags each) | 241-257 |
| Seafront cafés | 8 parasol terraces + produce carts at -24 | 259-286 |
| Marina | timber pier at x 163, 5 finger docks, 5 sailboats with cloth sails, 82 planks, ladders, life rings | 288-337 |
| Promenade | 32 lamps + 16 benches at -20.3 | 349-362 |
| Road furniture | 480 bollards on the wall line (±16.3), centre dashes, 6 wayfinding sign triples | 364-387 |
| Start gantry | pylons x -13.8 / 19.8 at z -30.32, beam 36×3×1.6 at y 13, 20×2 checker, 6 grid-box marks | 389-402 |
| Water | 14 channel buoys at ±58 with flags; 16 static gulls; 40 surf boulders | 404-434 |
| Ocean sweep beach club | 9 parasols + loungers + towels, bar shack, lifeguard tower, volleyball net, 4 sandcastles | 476-542 |
| Fishing harbour | 21-rock breakwater + mini lighthouse, 3 moored boats, crate jetty, drying nets | 544-604 |
| Palm headland | 70-plank boardwalk + rail, 6 beach huts, lookout tower, hammock | 606-676 |
| Racing furniture | tyre stacks ×21 at 18.6, 5 marshal posts at -22.6, braking boards, a 34-step covered grandstand with crowd on the home straight at +20.5..31.5 | 678-739 |
| Plaza | fountain with pergola, vines, benches at harborPoint(24.4/29, -37) | 761-798 |
| Horizon | 18 sea stacks on a 520×490 ellipse around (130,-35); 130 sea glint strips | 800-812 |

**Start sign**: plane 32×4 at `(3, 13, -29.48)`, unlit. Canvas 1536×192 reading SUNSET HARBOR /
M O V E C A D E   /   C O A S T A L   G R A N D   P R I X (`HarborTrack.tsx:821-841`, `:948-951`).

### 5.4 Harbor materials and textures

| Surface | Base material | Detail | Ref |
|---|---|---|---|
| Road | `#5c6a6b`, `asphaltTexture`, roughness 0.86, vertex colours | CC0 `asphalt_track` ground-projected, tile 4.5 m, albedo 0.6, normal 0.55, rough 0.6, macro 0.1, desat 0.35 | `HarborTrack.tsx:890-893` |
| Shoulder | `#d5b58a`, `cobbleTexture`, roughness 1 | `gravelly_sand` tile 3 | `:895-897` |
| Barrier | `#8fb0ad`, `stoneTexture`, roughness 0.8, double-sided | `painted_concrete` wall-projected, tile 2.4 | `:899-901` |
| Beach | `#e6cc9e`, `sandTexture`, roughness 1 | `gravelly_sand` tile 4, hue 0.1 | `:903-905` |
| Sea | `#5fb3b3`, `waterTexture` repeat 160, roughness 0.34, metal 0.15; UV scroll `(0.012t, 0.006t + 0.01 sin 0.3t)` | — | `:872-878`, `:957-960` |
| Shallows | `#7fd0cc` + `waterTexture`, α 0.7; UV scroll `(-0.02t, 0.035t)` | — | `:943-946` |

The low quality tier drops the CC0 detail (`HarborTrack.tsx:884-887`).
Procedural canvas textures (`kart/tracks/harborTextures.ts`) are seeded with LCG `noise(seed)` so they look
the same on every host: `asphaltTexture` 512² (seed 7: speckle, tar patches, cracks), `sandTexture` (11:
ripples, shells), `waterTexture` (19: swell bands, caustics), `stoneTexture` (23: coursed blocks),
`grainTexture` 256² (31: shared prop grain), `cobbleTexture` (37). Lines 37/67/91/117/140/158. All of them
tile on 8 m.

### 5.5 Harbor animated decoration (`kart/tracks/HarborMotion.tsx`)

| Actor | Placement | Motion | Lines |
|---|---|---|---|
| Lighthouse beams | (-11, 33, -190) | 2 additive cones (r 9→0.8, length 140, `#ffe9a8` α 0.16) + lamp sphere; yaw +0.7 rad/s (40.1°/s) | 12, 21-42 |
| Ferris wheel | `harborPoint(12.2/29, 38)` at beachY(38), spline yaw | r 9, 10 gondolas, hub at y 11.6, roll +0.25 rad/s (14.3°/s); gondolas counter-rotate to stay level | 45-113 |
| Ferry | ellipse (330, 300) around (130, -35) | angle 0.02·t, y −3.5 + 0.25 sin 0.9t, small pitch/roll | 116-141 |
| Cargo ship | ellipse (420, 400) | angle −0.012t + 2.2, 6 container stacks | 126-153 |
| Jet skis ×3 | lagoon around (110, -45) | r 24 ± 5 sin(0.3t+i), z×0.75, ω 0.55, lean −0.25 rad, wake plane | 159-196 |
| Gull flock ×9 | over the grandstand `harborPoint(27.6/29, 26)` | circling r 14–29, y 18–28, wing flap ±0.55 at 6 rad/s | 199-237 |
| Fountain jet | `harborPoint(24.4/29, -37)` | pulse `0.7 + 0.3 sin 1.6t` scales the jet; spray ring spins 0.8 rad/s | 240-268 |
| Buoys ×4 | t 3.0/3.8/4.6/5.4 /29, offset 50 or 54 | bob 0.35·sin(1.4t+φ), rock ±0.12; lamp on when `sin(2.5t+φ) > 0.6` | 271-304 |

All of it sits under `userData.kartDecoration` and never collides (`:306-317`).

### 5.6 Violet materials (`kart/scene/trackSurfaces.ts:88-153`)

`dressVioletMaterials` clones the GLB materials and adds world-space CC0 detail on top of the palette
(`kart/scene/worldSurface.ts`). The detail is divided by the texture's own linear mean, so the palette hue
is unchanged (`KART_SURFACE_SETS` means, `trackSurfaces.ts:12-18`). Projection is `ground` (planar from
above) or `wall` (dominant normal axis).

| GLB material | Role | Layer (tile m / albedo / normal / rough) | Extra |
|---|---|---|---|
| material_18 | road | asphalt 4.5 / 0.75 / 0.55 / 0.7 | roughness 0.82, macro 0.12, desaturate 0.75 |
| material_19 | lines/chequers | asphalt 4.5 / 0.28 / 0.4 / 0.5 | roughness 0.6 |
| material_6 | sand verge | sand 3.5 / 0.55 / 0.7 / 0.5, hue 0.15 | roughness 0.95 |
| material_8 | barriers/gantry | concrete 2.6 (wall) / 0.42 / 0.6 / 0.55 | `applySurfaceBreakup` + darkening near ground (groundY -1.4) |
| material_12 | hills | grass 4 / 0.55 / 0.6 / 0.4 + **steep** rock 7 (slope cos 0.62→0.42) | macro 0.2, tree-cell discard |
| material_3 | rocks/cliffs | rock 5 (wall) / 0.6 / 1.0 / 0.5 | |

Dead code: `violet-loop.jsx:79-109` recolours meshes whose names contain barrier/wall/fence/rail/guard
(`#8B5CF6`). Every Violet mesh is named "ground", so this never fires. Do not port it.

### 5.7 Sky, light, fog, reflections and post (`kart/scene/kartLook.ts`, `legacy/misc/KartLighting.tsx`, `kart/scene/kartPost.ts`)

One `KartTrackLook` record per track drives the sky dome, sun, hemisphere, fill, fog, reflection environment, grade, bloom and vignette.

| Field | Violet day (`kartLook.ts:34-64`) | Harbor sunset (`:66-96`) | Night (`:98-115`, any track) |
|---|---|---|---|
| sky top / horizon / below | `#3b6fd4` / `#d4e6f5` / `#a9bfd6` | `#4f78b8` / `#ffcf9e` / `#d9b89a` | `#0b1233` / `#2a3565` / `#141a33` |
| sky sun dir, size, glow, clouds | (-0.52,0.34,-0.78) 0.03 0.85 0.55 `#fff6ee` | (0.62,0.22,-0.75) 0.036 1.0 0.42 `#fff0e0` | (-0.4,0.45,0.8) 0.022 0.5 0.25 `#5b6aa0` |
| key sun dir / colour / intensity | (-0.52,0.5,-0.69) `#ffe6c4` 3.1 | (0.62,0.42,-0.66) `#ffdcae` 3.0 | (-0.4,0.62,0.68) `#a8bfff` 1.1 |
| hemisphere sky/ground/int | `#bcd4ff` `#6e5a48` 0.6 | `#cfe0ff` `#9a7a5a` 0.6 | `#4b6ea8` `#1b2233` 0.45 |
| fill (opposite sun) | `#a8b8ff` 0.35 | `#9cc0ff` 0.3 | `#6a7fd0` 0.25 |
| linear fog near/far (colour = sky horizon) | 260 / 2000 m | 260 / 2000 m | 200 / 1500 m |
| env (sky/horizon/ground/sun, int) | `#9fc3ff` `#ffd9c2` `#7a8a52` `#fff1d8` 0.55 | `#a9cbef` `#ffd2a6` `#b89a78` `#fff0d6` 0.55 | `#34487e` `#46538a` `#1c2236` `#dfe7ff` 0.6 |
| grade exp/contrast/sat/vib | 1.02 / 1.08 / 1.10 / 0.22 | 1.00 / 1.07 / 1.08 / 0.20 | as Violet |
| grade lift / gain | (.008,0,.03) / (1.03,1,.96) | (.012,.004,.028) / (1.04,1,.95) | as Violet |
| bloom str/radius/threshold | 0.32 / 0.35 / 1.0 | 0.34 / 0.38 / 1.0 | as Violet |
| vignette | 0.32 | 0.30 | as Violet |

Rig (`KartLighting.tsx`): `SkyDome` (analytic three-stop gradient + sun disc + clouds; `render/SkyDome.tsx`,
`render/skyShader.ts`), radius 1200, centred on each viewport's camera. Shadow sun: ortho box ±30 m that
follows the viewed kart with a 9 m lead, 140 m away, snapped to texels (`:29-31`, `:56-71`, `kartLook.ts:135-191`).
Bias −0.0004, normalBias 0.035. Neutral tone mapping, exposure = grade.exposure. The PMREM environment is
rendered once from the sky dome + a sun card (×14 day, ×4 night) + ground disc + horizon card (`:178-197`).
Night adds an instanced starfield and a moon (`:207-237`). The post chain is RenderPass → UnrealBloom →
`KartFinishShader` (exposure, neutral TM, grade, vignette, impact flash, dither) (`kartPost.ts:1-60`).
The same chain runs per viewport in split-screen (`legacy/SplitScreenRenderer.tsx:164-203`). The legacy
`ColorGradingEffect.jsx` + `textures/lensDirt.jpg` are used only by the DEV single-player `App.jsx`. Ignore them.

### 5.8 Kart materials (`kart/scene/kartMaterials.ts`)

- Paint (`kartPaintMaterial`, `:27-44`): clearcoat physical material. Colour = paint primary, roughness 0.38,
  metalness 0.12, envMapIntensity 1.15, clearcoat 1, clearcoat roughness 0.07, emissive = colour × 0.045 at
  intensity 0.3. The low tier uses a standard material at roughness 0.28. It applies to `body`, the
  steering wheel (stock kart) and `booster`.
- Tyre (`:46-53`): `#1b1b1d`, roughness 0.9, metalness 0, env 0.3.

---

## 6. Avatars and player identity

### 6.1 What represents a player in the race

- **Vehicle**: the stock kart `public/models/kart.glb`, or a paid mount from `public/game-assets/vehicles/{moto,coupe,truck}.glb`.
  Mounts are purely cosmetic. Physics, probes and handling are identical (`kart/vehicles.ts:1-15`, `protocol/avatar.ts:52-59`).
  `VEHICLE_MOUNT_SPECS` (`vehicles.ts:51-103`) give per vehicle: pose (`seat` / `straddle`), grip node
  (`d_wheel` / `handlebar`), 4 wheel slots (front slots 0/1 steer; moto leaves slots 1 and 3 empty, and they
  follow their axle mate), booster, trim, and exhaust positions.
  The stock kart's seat, grip and wheel positions are fixed in code: seat `[0, 0.17-1.22·0.9, -0.15]`,
  grip `[0,0.355,0.542]` rot `[-1.134,0,0]`, wheels `±0.7/-0.2/0.7` front and `±0.77/-0.137/-0.7` rear
  (`vehicles.ts:45`, `:62-66`). Built vehicles read nodes `seat`, grip, `wheel_N` and `trim` from the GLB.
  The node contract comes from `tools/avatar/build_vehicles.py`.
- **Driver**: `DrivingArcadeAvatar` (`avatar/ArcadeAvatar.tsx:939+`), seated inside the vehicle at `mount.seat`.
  Rotation π (faces +Z toward the wheel), scale 0.9 (`legacy/models/Kart.jsx:649-679`).
  The pose is **procedural**: hands solve onto the grip node, with steering, lateral G, drift, item use,
  bounce and a finish cheer (`avatar/drivingGrip.ts`, `avatar/drivingExpression.ts`). The GLBs carry no animation clips.
- **Name plate**: drei `<Html sprite>` 1 m above the kart. It shows an emoji (legacy `avatarId`, default 🏎️)
  over the player name (fallback `P{slot+1}`), white bold text on a translucent black pill with a
  1 px border and a glow in the paint primary. It is hidden after the race finishes (`legacy/MultiplayerKart.tsx:1769-1820`).
- **Colour use**: paint primary → body/wheel/booster; primary/secondary → celebration confetti
  (`MultiplayerKart.tsx:1715-1748`) and trails. A jersey set to `'seat'` takes the kart accent
  (`avatar/appearanceColors.ts:93`). Drift spark colours come from the drift level (blue/yellow/purple),
  not from identity.

### 6.2 Identity data (room player `avatar: AvatarAppearance`; `protocol/avatar.ts:61-90`)

| Field | Options | Effect in kart |
|---|---|---|
| `kartPaint` | pink `#f0508e`, cyan `#5ec8e5`, gold `#f7c948`, violet `#a67cf0`, white `#f4f0ea`, navy `#2a3558` (secondaries in `kart/kartPaint.ts:10-17`) | body paint; legacy fallback `kartColorId` → `KART_COLORS` (red `#E52521` default…; `legacy/customization/avatarData.ts:200-216`) |
| `vehicle` | kart, moto, coupe, truck | mount GLB |
| `face` × `body` | mira, leo, nia, kai, rose, omar × female, male | `/game-assets/avatar4/{face}-{body}.glb` (`avatar/avatarAsset.ts:452-455`; ArcadeAvatar always passes a face, default mira, `:234`) |
| `hair` / `hairColor` | 8 styles (in-GLB slots) / 9 colours | |
| `skin` | 8 tones (hex in `appearanceColors.ts:20-29`) | |
| `jersey`, `pants`, `shoes` | 7 / 4 / 4 | |
| `accessory` | none, glasses, headband, cap (cap hides hair) | |
| `clothing` | classic (none) or 7 paid overlays → `/game-assets/clothing/{id}-{body}.glb` (`avatar/clothingAsset.ts:7`) | `turbo-racer` is the racing suit |
| `hand` | right/left | not relevant to driving |

**Avatar4 GLB contract** (measured from `mira-female.glb`): Draco-compressed and skinned, 65 meshes, 1 skin.
It has 26 joints: the 14 rig bones `torso, head, left/right UpperArm, Forearm, Hand, UpperLeg, LowerLeg, Foot`
(`avatarAsset.ts:57-72`), plus `neck`, clavicles, fingers/thumbs, and `Spring_hair×3` / `Spring_hemFront/Back`
(spring bones). Morph targets: `eyeBlink, eyeWide, eyeSquint, browInnerUp, browDown, browOuterUp, mouthSmile,
cheekSquint, jawOpen, mouthPress, mouthFrown, elbowFlex, kneeFlex`. Materials: `skin, hair, eyeWhite, iris,
eyeDark, lips, mouth, nostril, teeth, jersey, pants, shoes, rubber, lace, frame, accessoryFabric`, coloured at
runtime from the hex tables. Clothing overlays share the same 26-joint skeleton.
`game-assets/kart/avatar.glb` (older 14-joint outfit model, Draco + KTX2) is only a fallback that the current
code never reaches.
Integration notes: `docs/avatar4/INTEGRATION.md` (12 assets at about 3.4 MB each; seated kart pose keeps one
hand on the wheel during celebrations).

---

## 7. Binary asset inventory (what the kart game loads)

| Path (`app/public/…`) | Bytes | Format / notes | Used by |
|---|---|---|---|
| `models/violet-loop.glb` | 1 808 888 | Draco + WebP, 10 meshes, ~460k tris, 11 tiny palette PNG/WebP | Violet circuit |
| `models/kart.glb` | 911 904 | Draco, 7 PNG textures, nodes `root(s=10) body d_wheel booster wheel_0..3 shape(unused)`, mats `m_Body m_Tire default` | stock kart |
| `models/movecade-kart.glb` | 6 964 | 292-tri placeholder | **unused** |
| `game-assets/vehicles/moto.glb` / `coupe.glb` / `truck.glb` | 63 900 / 92 128 / 132 460 | Draco, untextured (`m_Body m_Accent m_Trim m_Chrome m_Lamp m_Glass m_Tail m_Tire`), 5.3k / 9.1k / 14k tris; `vehicles/catalog.json` 806 B | paid mounts |
| `game-assets/avatar4/{6 faces}-{female,male}.glb` | 12 files, 40 059 920 total (3.31–3.35 MB each) | Draco, skinned, morphs, PNG | driver (only the players' picks load) |
| `game-assets/clothing/{7 outfits}-{female,male}.glb` | 14 files, 18 012 292 total (0.96–1.56 MB each); `clothing/catalog.json` 1 545 B | skinned overlays | paid outfits |
| `game-assets/kart/avatar.glb` | 1 983 932 | Draco + KTX2, legacy outfit body | fallback only (unreached) |
| `textures/surfaces/{asphalt_track,painted_concrete,leafy_grass,gravelly_sand,rock_face}/{color,normal,rough}.webp` | 182 002/233 654/10 474; 236 722/67 952/23 836; 80 590/141 198/5 342; 58 884/120 378/1 116; 19 730/111 974/812 | Poly Haven CC0 1k. **Normals are OpenGL (`nor_gl`) → flip green for UE**. Ledger: `textures/surfaces/LEDGER.json` | track detail |
| `textures/particles/smoke.png`, `spark.png`, `alpha.png` | 114 470 / 160 710 / 2 985 | particle sprites | VFX |
| `textures/noise.png` | 1 900 637 | noise texture (stored in store) | VFX shaders |
| `textures/lensDirt.jpg` | 14 377 | dev single-player only | ignore |
| `audio/kart-*.wav` | 45 files, 7 331 292 total (see `app/src/audio/kartCues.ts:16-60`; 7 engine loops at 384 044 each) | PCM WAV | spec 04 |
| `attract/kart.{mp4,webm,jpg}`, `kart-phone.{mp4,webm,jpg}` | 1 027 978 / 1 093 811 / 58 865; 13 299 / 9 201 / 8 763 | home-page attract loop | not in race |
| `draco/gltf/*`, `basis/basis_transcoder.*` | — | web decoders | not needed in UE |

Not binary, but needed: every sign, banner, boost-pad arrow, item "?" and all six Harbor textures are
**generated at runtime on a canvas**. They must be rendered to PNG during export (§4.1). Items and their
projectiles are procedural primitives with no model files (covered in spec 02).

---

## 8. UE mapping (UE 5.8)

**Project layout.** `Content/Movecade/Tracks/{VioletLoop,SunsetHarbor}/` holds a level per track, the imported
data assets, meshes and materials. `Content/Movecade/Tracks/Shared/` holds prefabs (item box, boost pad,
grandstand, lamp, banner, tree), track materials and the sky.

**Data import.**
- `UTrackDataAsset` (C++ `UPrimaryDataAsset`) mirrors §4.3. A `UFactory` or an editor Python script
  (`unreal.EditorAssetLibrary`, `unreal.JsonObjectConverter`) reads `<id>.track.json`. The axis/unit
  conversion (§1) is done once in `FTrackCoords::ToUE()`.

**Track builder.** C++ `ATrackBuilder` actor, with editor-time "Rebuild" and runtime-static output:
1. A `USplineComponent` built from `spline.controlPoints`: closed loop, `CurveCustomTangent`,
   `tangent_i = (P[i+1]-P[i-1])/2` in UE units, input key = i. This exactly reproduces `harborPoint` and
   `densifyLoop`. The spline carries t→key for Harbor placements and drives AI/recovery lookups.
2. **Road and ribbons** come from `samples.points` (not `USplineMeshComponent`; Violet's width varies).
   Generate with Geometry Script (`UDynamicMesh` → `AppendTriangles`/sweep) or `UProceduralMeshComponent`.
   **Bake to `UStaticMesh`** (Geometry Script `CreateNewStaticMeshAssetFromMesh`). Harbor: the 17 ribbons from
   `ribbons[]`, with UVs in metres/8 to match the source. Violet: use the imported GLB road (the parity
   path), and build ribbons from the profile only for the upgrade/rebuild.
3. **Collision.** Road, curb and shoulder use complex-as-simple. Walls are **simple box/convex strips** along
   `collision.walls[].polyline`: Harbor at ±16.3 m, 1.3 m high; Violet uses the GLB's steep faces
   (complex) plus the profile wall polylines as a backup. For exact parity, port `constrainHarborMovement` /
   `constrainMeshMovement` (radius 115 cm, substep 65 cm, wall if |N.Z| < 0.7) into the arcade kart movement
   component. Spec 01 owns kart movement. Avoid relying on Chaos rigid-body contacts.
4. **Surfaces.** Physical Materials `PM_Asphalt` (×1.0) and `PM_Dirt` (×0.75), assigned to `ground-dirt-shoulder`
   and Violet `Object_11`. The kart reads the PhysMat from its ground trace instead of matching mesh names.
5. **Gameplay actors** spawned from the asset: `ACheckpoint` (sphere trigger r 2400 cm, ordered, index 0 = finish),
   `APlayerStart` ×4 (or a custom grid), `AItemBox` (respawn 15 s per player), `ABoostPad`
   (box 8×4 or 12×6 m, yaw), `ACameraActor` ×9 for the intro, and a minimap render-target or 2D spline widget
   drawn from `minimap.svgPath` / bounds.

**Landscape vs static meshes.** Use **no Landscape for parity**. Both tracks are flat ribbons. Violet's
6.5 km hills mesh (`Object_18`, 113k tris) imports as a Nanite static mesh. Harbor's ground is beach ribbons
plus a 5 km sea plane. Landscape (with a spline-deformed road via Landscape Splines) only makes sense for
new tracks or a Violet rebuild.

**Scenery.**
- Violet trackside, trees and banners are instances from `scenery.instances`, spawned into
  `UHierarchicalInstancedStaticMeshComponent`s (one per prefab or variant). Kerbs are one baked mesh.
  Lamp heads use an emissive MI whose intensity is a parameter (0.4 day / 4 night).
- Harbor props: import `harbor_scenery.glb` (35 colour batches) as static meshes. Use one MI of
  `M_FlatColorGrain` per batch, with a colour parameter, the shared grain texture and roughness 0.86.
  An alternative is to merge into a few meshes with vertex colours.
- **PCG**: re-express `planTrackside` as a PCG graph for new tracks: Spline Sampler (3 m) → curvature
  attribute → filters (corner ≥ 0.018, straight ≤ 0.006) → distance/self-pruning → spawners (kerb spline
  mesh, grandstand, banner every 24 m, lamp every 30 m) → a clearance check against the road spline (3 m).
  For parity, spawn from the exported plan so placements match 1:1.
- **Animated actors** (`scenery.animated`): small C++/BP actors. `URotatingMovementComponent` for the
  lighthouse (40.1°/s) and Ferris wheel (14.3°/s, gondolas counter-rotate). Tick-driven parametric orbits for
  the ferry, cargo ship, jet skis and gulls. A material-parameter pulse for the fountain and buoy lamps.
  Lighthouse beams are additive translucent cones, or a Spot Light + volumetric fog in the upgrade.
- **Signs/banners**: import the exported canvas PNGs as Texture2D. Use unlit MIs for the two start signs.
  An alternative is UMG → render target if the text must change at runtime.

**Materials.**
- `M_TrackWorldDetail` (master): `WorldAlignedTexture` / world-XY projection for `ground` and the dominant
  axis for `wall`, using the CC0 sets. Detail luminance is divided by the set's mean (constants from
  `KART_SURFACE_SETS.mean`) and multiplied over the palette/base colour. Include the tile-size, albedo,
  normal, roughness, hue, macro and desaturate parameters, plus a steep-slope blend (grass→rock between slope
  cos 0.62 and 0.42) for the hills. Flip the green channel of the Poly Haven GL normals on import.
- The Violet tree-cell cut-out becomes a masked material that clips UV rects in material_12. Better still,
  delete those triangles once in Modeling Mode.
- Kart paint: `MI_KartPaint` using the **Clear Coat** shading model. BaseColor = paint, Roughness 0.38,
  Metallic 0.12, ClearCoat 1, ClearCoatRoughness 0.07, a small emissive. Tyre: roughness 0.9.
- Water: parity is a translucent/opaque plane with two panning UV layers (speeds from §5.4).

**Lighting, fog and post (per track from `look`).**
- Directional Light (sun) rotation from `sunDirection`, colour/intensity scaled to lux (calibrate by eye
  against the web capture). Sky Light uses real-time capture. Sky: either a sky-sphere material reproducing
  the three-stop gradient, sun disc and clouds (parity), or Sky Atmosphere + Volumetric Clouds (upgrade).
- Fog: UE has no linear fog. Use Exponential Height Fog with Start Distance 26 000 cm, Fog Cutoff
  ~200 000 cm, inscattering colour = sky horizon, and density tuned until 2000 m reads fully fogged.
- **Lumen** GI and reflections on (TV target, 1–4 viewports). Measure split-screen ×4 cost; fall back to
  Lumen low or SSGI on the 4-player profile.
- Post Process Volume: manual exposure (EV from `grade.exposure`), Bloom (intensity ≈ strength,
  threshold 1), Vignette 0.3. Color Grading: Contrast 1.07–1.08, Saturation 1.08–1.10, Gain/Offset from
  `gain`/`lift`. Choose the tonemapper close to neutral (reduce the Filmic toe/shoulder).
- Night mode = a second lighting preset (data layer or level variant) using the `NIGHT` look, a star dome
  and a moon.

**Nanite.** Turn it on for all opaque static meshes: Violet GLB meshes (barriers 181k, rocks 112k, hills 113k
tris), Harbor scenery batches, grandstands and lamps. Leave it off for translucent/additive meshes (surf,
shallows, lighthouse beams, wake planes) and anything needing vertex animation. Masked foliage (trees) can use
Nanite with the masked-material cost; measure it.

**Avatars and vehicles in UE.**
- Decode Draco, then import the 12 avatar4 bodies + 14 clothing overlays as Skeletal Meshes on **one shared
  USkeleton** (identical 26 joint names). Keep the morph targets. Set
  `SetLeaderPoseComponent(body)` on clothing components. Hair styles are mesh sections or materials toggled
  by `hair`. Colours come from `appearanceColors.ts` as MI parameters.
- Driver AnimBP + Control Rig: two-bone IK hands to the vehicle grip socket (`d_wheel` / `handlebar`),
  steering-wheel rotation from steer input, lean from lateral G, and the drift/item/finish reactions from
  `drivingExpression.ts`. Spring bones use AnimDynamics or Kawaii-style physics on the `Spring_*` chains.
- Vehicles are static meshes with sockets named after the GLB nodes (`seat`, `d_wheel`/`handlebar`,
  `wheel_0..3`, exhaust points). Paint MI on `m_Body`.
- Name plate: `UWidgetComponent` (Screen space) 100 cm above the kart, showing the name + emoji with a
  paint-coloured border, hidden after finish. Split-screen: the plate is visible in other players' viewports
  and not your own (match the web if the spec 04 owner confirms).

---

## 9. Upgrade ideas (after parity, kept short)

- **Materials:** swap the 1k Poly Haven sets for Fab/Megascans asphalt, painted kerb, sand, concrete and rock
  (4k, virtual textures). Add decals for skid marks, oil, sponsor logos and grid numbers. Harbor's flat-colour
  props could get trim-sheet materials; replace the canvas water with the UE **Water** plugin (Ocean body +
  shoreline waves).
- **Violet rebuild:** re-author Violet from its centreline and profile as clean Nanite/Landscape geometry. This
  removes the licence question (§4.4) and allows real elevation, banking and a true figure-8 bridge. The
  schema already carries `y`/`bankDeg`.
- **New track themes** (built with the same exporter schema + PCG dressing): night neon city (reuse the
  Violet banner style), alpine snow with a jump, desert canyon with Landscape erosion, a jungle temple with a
  tunnel, a space station in low gravity.
- **Drivers:** stylised driver characters (helmet + racing suit from `turbo-racer`) with real driving animation
  clips (MetaHuman-lite or stylised Fab characters retargeted with IK Retargeter). Add a winner podium
  scene and an emote on boost.
- **Lighting:** day/night cycle per race, rain-wet road via a puddle mask, volumetric lighthouse beams,
  stadium floodlights for night.
- **Scenery life:** animated crowds (Mass/instanced vertex animation), marshals waving flags, fireworks on
  the final lap.
