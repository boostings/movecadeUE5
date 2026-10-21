# The other games: shared framework and port plan

movecade-v2 ships five games: Bowling, Samurai Slice, Sky Stacker, Bullseye Bash (darts) and Kart Racer. Kart is
done (00-04). This page specifies what the other four share in the web build, and how the UE port is organised.
Per-game specs: [06-bowling.md](06-bowling.md), [07-darts.md](07-darts.md), [08-samurai.md](08-samurai.md),
[09-stacker.md](09-stacker.md).

## Jimmy's decisions (2026-10-21)

| Topic | Decision |
|---|---|
| Fidelity | **Parity + AAA**, as with Kart: each engine and gesture rule-for-rule with golden tests from the web suite; scenes, avatars, FX, UI and audio rebuilt to Kart's standard |
| Controls | **Phones (Party Mode) plus gamepad and mouse** for local play, mapped per game |
| Order | **One at a time, in this session**: shared groundwork, then Bowling, Darts, Samurai, Stacker |

## 1. The web host framework

**Plugin contract** (`app/src/games/types.ts:88-159`): `GamePlugin = {manifest, countdownMs?, loadCanvas(),
loadController?()}`; canvas props `{client, room, players, isHost, resultMode?}`; controller props `{client, room, me,
isActive, sample (10 Hz readout), subscribeSamples (full rate)}`. Registry `[bowling, kart, samurai, stacker, darts]`.
Catalog `protocol/game-manifest/catalog.json`.

| Game | Play mode | sendHz | Motion | Score max | Practice | Pre-play countdown |
|---|---|---|---|---|---|---|
| bowling | turn-based | 60 | swing | 300 | 8 s | none |
| samurai | simultaneous | 100 | swing | 999999 | 8 s | 3000 ms |
| stacker | simultaneous | 60 | hybrid | 999999 | 6 s | 3000 ms |
| darts | turn-based (relayed as simultaneous) | 60 | swing | 1170 | 8 s | 3000 ms |

All: reconnect restore-player, rematch same-room, controller payloads orientation + control.

**Split by convention:** `engine.ts` (pure: `createMatch(roster)`, `stepMatch(match, dtMs)`, actions), `runtime.ts`
(a React hook: decodes frames, listens to game events, pushes scores, `advance()`, a HUD store), scene (R3F,
presentation only).

**Clock:** wall-clock `performance.now()`; `advance()` per frame and from a 50 ms heartbeat; `advanceElapsed`
(`runtimeClock.ts`) caps catch-up at 1000 ms and slices into ≤ 100 ms steps (bowling: its own 50 ms slices over a
ball-phase machine). Held while the intro card shows or the room is paused (status paused, or the host connection
lost), with no catch-up burst on resume. HUD every 100 ms; changed scores every 1000 ms.

**Lifecycle:** room status `lobby | countdown | in-progress | paused | complete` (`countdown` is never written;
counts live in `room.countdown{kind, endsAt}`). Lobby: all seats ready (≥ 1 person) → host `LOBBY_COUNTDOWN_MS`
5000 (`lobby.tick` / `lobby.go`) → `start-game`; the server's `beginMatch` sets in-progress and `activePlayerId`
(null if simultaneous, else seat 0). Host shell per match: launch curtain until the scene is ready → intro card
6000 ms (skipped for kart) → `setCountdown('match', countdownMs)` once; the engine counts its own 3-2-1.

**Turns:** simultaneous games send `advanceTurn({activePlayerId: null, phase: 'active'})` once. Bowling sends
`advanceTurn({activePlayerId: bowler})` on each change while aiming. Darts sends null (every phone streams) and
gates input with its own `darts:turn`. The server drops a frame unless in-progress and (no active player or the
sender is it), and appends `seat - 1` as the player index.

**Pause / reconnect:** a disconnect in play pauses (`player-disconnected`); `RECONNECT_GRACE_MS` 135000; the host
may drop after `PAUSE_PROMPT_AFTER_MS` 45000; `RESUME_COUNTDOWN_MS` 3000; `HOST_GRACE_MS` 90000; a returning
player re-readies. Snapshots (`matchSnapshot.ts`): `{g, v, m, ...}` via `save-snapshot` every 1000 ms (server: ≥ 250
ms apart, ≤ 5 KB, depth ≤ 6); hydrate once per match; only committed facts (an unsettled bowling ball is re-thrown
from address); restored items aged 10000 ms; every game's snapshot version is 1.

**Completion:** `completeGame([{playerId, score}])` when the engine ends (bowling at the next aiming; stacker after
its compare hold). The server refuses matches under 10 s or already completed, HMAC-signs scores (v3) and sends
`signed-scores` to the host, which posts them to the Go API (dedup by signature, 2 retries, valid 5 min).
Rematch (`restart-game`) returns to the lobby (`matchNumber + 1`, everyone re-readies). Extra host events:
`play:feedback {gameId, playerId, title, detail, tone, haptic, prominence}` (phone toasts and haptics) and
`match:highlights` (best moment per player).

## 2. Wire protocol

Binary sensor frame (`protocol/src/sensor.ts`, little-endian): type 0x02, flags, screen orientation 0-3, seq;
alpha/beta/gamma i16 deg×100; gyro xyz i16 deg/s×20; accel xyz (gravity removed) i16 m/s²×1000; gravity xyz
i16×1000; dtMs u16; optional quaternion xyzw i16×16384. 30 or 38 bytes (31/39 at the host with the player index).
Flags CALIBRATED 1, SCREEN_COMPENSATED 2, HAS_GYRO 4, HAS_ACCEL 8, HAS_GRAVITY 16, HAS_QUATERNION 32. Quaternion:
W3C Z-X'-Y'', calibration applied as `q_cal⁻¹·q`. The web controller sends calibrated Euler, subtracts accel bias,
leaves SCREEN_COMPENSATED clear. Limits 150 sensor frames/s, 40 control messages/s (burst ×2). Controllers stream
only while active and calibrated.

Calibration (`protocol/src/motionCalibration.ts`): hold still 800 ms (resets on gyro > 8, accel > 0.65, drift >
2°), ≥ 12 samples, freshness 180 ms; stored per `{gameId, hand}`.

Control payloads (`game-event`): `stacker:drop` (no data); `bowling:release {aim, power, curve, loft, confidence,
source, hookUnavailable}`; `darts:release {aimX, aimY, power, spinDegPerSec, rollDegPerSec, confidence, steadiness,
peakMs2, durationMs, source}`; samurai none (cuts resolve on the host from the blade). Host-only:
`darts:turn`, `darts:aim`, `darts:result`, `darts:replay-outcome`, `bowling:replay-outcome`, `practice:ack`,
`match:highlights`.

Practice (lobby only, unscored): phone `practice:action {gameId, actionId, kind}` (roll / cut / drop / throw);
host replies `practice:ack` from a connected, not-ready player and shows it 5.5 s. Completion rules exist only on
iOS (`PracticeRehearsal.swift:40-135`); `practice.targetSeconds` is never read.

## 3. Shared avatar motion (`app/src/games/avatar/motion/`)

Avatar space: right-handed, +Y up, facing -Z, left arm at -X. Per frame (`ArcadeAvatar.tsx` ~607-680):
1. `updateCalibrationBasis`: neutral basis from gravity and a coached forward (samurai (0,0,-1), others (0,1,0));
   needs |g|² ≥ 20.
2. `ingestAttitudeFrame`: wire quaternion or one from Euler; uncalibrated `q·NEUTRAL⁻¹`; earth → avatar (-90° about
   X); hemisphere continuity; gyro (rad/s) and accel rotated into avatar space.
3. `sampleAttitude`: gyro extrapolation ≤ 120 ms (exponential map); stale at 360 ms, presence fades over 400 ms; yaw
   leak 0; sample delta ≤ 100 ms.
4. `updateHandTarget` (`handTarget.ts:349`), shoulder-relative: orientation `basis·q·basis⁻¹` → scene root yaw →
   grip; position a pendulum link blended with the elbow-swivel prior by `elbowFollow`, offset so calibrated identity
   lands on `idle`; plus a leaked double integral of accel (velocity ≤ 3 m/s, zeroed after 0.18 s quiet (|a| < 0.02)
   or stale > 120 ms); offset cap bowling 2×arm, samurai 0.65 m, else reachClamp; live/idle blend
   `1 - e^(-response·dt)` (dt ≤ 1/20); reach clamped to [|L1 - L2| + 0.02, 0.97 arm]; elliptical body keep-out
   fading above shoulder + 0.25.
5. Game `handBias` added. 6. `solveArm` (analytic two-bone IK, `armSolver.ts:456`): pole (-0.45, -0.75, 0.49)
   mirrored; shoulder cone 2.36 rad, blend 0.42, handover 0.65, overflow → torso rotation; bowling uses
   `solveBowlingArm` (wrist ceiling = lung line - hand half-length); support hand by `solveSupportArm`.
7. `updateBodyFollow` (torso, head, eyes, legs, feet) → reactions → `solveLegs`. 8. Bowling: `holdArmUnderCeiling`.

Rig (`armSolver.ts:246-274`, left, m): shoulder (-0.318, 1.83, 0), elbow (-0.352, 1.39), wrist (-0.382, 1.01), hip
(-0.152, 1.22), knee (-0.15, 0.66), ankle (-0.148, 0.12); torso y 1.3; lung y 1.565; hand half-length 0.078.

| Game | Side | Grip ° | Swivel prior | Elbow follow | Reach / clamp | Leak vel / pos | Idle (m) | Response | Keep-out x / z |
|---|---|---|---|---|---|---|---|---|---|
| bowling | L | (-14, 0, 8) | (-.14, -.97, .2) | .28 | .9 / .16 | 6.5 / 4.2 | (-.18, -.74, -.05) | 9 | .42 / .32 |
| samurai | R | (-8, 0, 0) | (.3, -.93, .2) | .7 | 1.15 / .2 | 7.5 / 5 | (.1, -.23, -.28) | 12 | .3 / .22 |
| darts | R | (-28, 0, -6) | (.26, -.7, -.66) | .35 | .8 / .14 | 8 / 5.5 | (.16, -.42, -.42) | 12 | .3 / .22 |
| stacker | R | (-72, 0, 0) | (.44, -.78, -.44) | .45 | .6 / .1 | 9 / 6.5 | (.2, -.38, -.4) | 10 | .3 / .22 |

Neutral basis: darts (-90, ±45, 0); bowling and stacker (-90, 0, 0); samurai identity. Off hand mirrors
(quaternion (x, -y, -z, w), vectors flip x).

| Game | Stance | Steps | strideZ | strideX | Lift | Counter-rot | Weight shift | Breath |
|---|---|---|---|---|---|---|---|---|
| bowling | approach | 4 | -.17 | 0 | .055 | .075 | .55 | 1 |
| samurai | shuffle | 2 | -.09 | .13 | .028 | .11 | .4 | .85 |
| darts | lean | 0 | 0 | 0 | 0 | .05 | .7 | .7 |
| stacker | sway | 0 | 0 | 0 | 0 | .09 | .3 | .9 |

Crouch 0.035; feet planted at y 0.12 (knees bend). Energy: gyro ≤ 24, response 7; counter-rotation inputs ±12,
response 9. Stacker root `(lateral·0.075, crouch - drive·0.085, -drive·0.045)`. Approach bob 0.014, slide 0.02,
twist 0.07. Gaze: head yaw/pitch 55°/40°, eyes 25°/18°, head share 0.6, saccade threshold 7°, hold 220 ms,
response 26. Reduced motion ×0.3.

Reactions (`reactions.ts`): 22 names; hooks — bowling strike → strikePump, spare → relief, gutter → fail; samurai
bomb → fail, miss → nearMiss, combo → comboFocus, finishFirst → respectBow, finishMid → cheer, finishLast →
matchLost; darts bull → bullseye, miss → nearMiss, bust → fail, finishFirst → matchWon, finishLast → matchLost;
stacker perfect → perfectSet, miss → nearMiss, topple → fail, elimination → eliminated, finishFirst → matchWon,
finishLast → matchLost; shared join → welcome, ready → ready, goodShot → acknowledge, turnExit → farewell. Additive
deltas from a rest pose with authored durations (strikePump 2100 ms, bullseye 1500 ms); live motion interrupts
after 100 ms (energy 1.6).

Camera director: hard cut on outcomes, ease on phase changes, never cut during a live gesture, no roll, fov ease
6/s; focus threshold 0.82 / 1400 ms; reaction 1200 ms; turn change 1500 ms.

Idle life: breath period 2π/1.55 s (inhale .38, exhale ends .86, shoulder rise .012 rad); weight shift every
8-13 s over 1.4 s; blinks ≥ 1.6 s apart (mean 3-4.6 s), 12 % doubles, 0.22 s (close 65 ms, hold to 85 ms); glance
period 4.4 s. Expressions neutral, focus, effort, joy, disappointed, surprise over 10 morphs + blink, response 12.
Springs (Verlet on `Spring_*`): hair stiffness .28, damping .86, gravity 3.2, max .55; hem .42/.8/0/.14.

## 4. Sound

One Web Audio service; cues are data (`audio/cues.ts`: gain, throttle, source = synth spec or file with synth
fallback). Recorded WAVs in `app/public/audio/` (74 files, `manifest.json`) cover every cue of the four games plus
`match.over`, `lobby.tick`, `lobby.go`. Each game's `sound.ts` observes the match once per frame and plays cues from
its effects with gain 0..2, rate 0.25..4, lane pan.

## 5. Known issues and intent (movecade-v2 docs)

`docs/plans/DARTS_STACKER_POLISH_PLAN.md` (open): darts boards too large, weak grip/flight, impact grammar, decider
drama; stacker city doesn't sell altitude, dark-sky regression, landing grammar, haptics. `docs/feel/*.md` open
findings: darts impact too quiet, stacker landing quiet and altitude unreadable, samurai bomb telegraph, HUD density
at TV distance. `docs/controller-assist-mode.md`: touch substitutes for motion (bowling power meter, samurai swipe,
darts drag) — the basis for our local mappings.

Discrepancies: the phone decides bowling and darts gestures (only samurai is host-detected); the bowling controller
feeds its detector from the 10 Hz readout; practice is not enforced on web; rematch returns to the lobby.

## 6. The UE port's shape

- **One module, one folder per game:** `Source/MovecadeKart/{Public,Private}/Games/<Game>/` with the pure rules
  and sim (`*Rules`, `*Sim`, gesture detectors) in plain C++ like `Sim/`, tested against the web suite's values.
- **A shared arcade layer** (`Games/Arcade*`): a game mode base that seats players (local pads/mouse, phones or
  bots), runs the intro → countdown → play → results flow on a fixed-step clock, routes inputs per seat, and drives
  the existing UI (front end, HUD painter, results). Kart keeps its own mode.
- **Phones:** `URoomHostSubsystem` gains the game id from the level, generic `game-event` routing, `advanceTurn`,
  `play:feedback` and the generic score/complete calls; the sensor-frame decoder already exists.
- **Avatars:** the existing driver avatars and native anim proxy, extended with a standing body (stance, arm IK to a
  hand target, reactions) ported from the motion tables above.
- **Levels:** `L_Bowling`, `L_Darts`, `L_Samurai`, `L_Stacker`, each built by its game's builder actor in code, like
  the tracks. The front end gains a game picker.
- **Determinism:** each game steps on a fixed clock (bowling's ball phases at 50 ms, the others at 10-16.7 ms) rather
  than wall-clock slices; per-game decisions are listed in each spec.
