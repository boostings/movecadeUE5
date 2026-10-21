# Bowling: port spec

Source: movecade-v2 `app/src/games/bowling/` (paths relative to it). Parity first. Part of the multi-game port
([05-games-framework.md](05-games-framework.md)). Units: metres in the sim; "u" are the scene's visual units.

## 1. Rules (`engine.ts`, pure)

- `FRAMES` 10, `PINS` 10 (:26-27). 1-4 players, turn-based.
- State (:52-63): `order` (fixed at kickoff); `cards[id] = {playerId, rolls[], frames[], score, done}`;
  `turnIndex` (-1 when over); `standing` 0..10; `rollInFrame` 1|2|3; `status` playing|complete. Frame
  `{number, rolls, kind: open|spare|strike|null, cumulative|null, complete}`.
- `score()` (94-177) walks the flat rolls: strike = 10 + next two; spare = 10 + next one. A frame whose bonus rolls
  don't exist has `cumulative = null`, and every later frame stays null (latched). 10th: 3 rolls on a strike or a
  first-two total of 10, else 2; total is the plain sum. `done` = 10th complete.
- `roll(state, pinsDown)` (193-243): `felled = clamp(floor(pinsDown), 0, standing)`. 10th: frame complete → turn
  ends; else `standingAfter == 0` → rack resets to 10 (`rackReset`). Frames 1-9: turn ends on a strike or after
  ball 2. Turn end → standing 10, roll 1, `rackReset`. `nextTurn` scans forward through `order` (mod n) for a card
  not done; -1 = complete (253-259). Rotation is frame by frame (P1 frame, P2 frame, ...).
- Helpers: `frameNumberFor = min(completed + 1, 10)` (262); `standings` by score desc, ties keep turn order (268).
  Returns `{state, turnEnded, rackReset}`; no events.
- Runtime rules: `dropBowlingPlayer` (runtime.ts:367-401): a bystander leaving keeps the active player; the lane
  owner leaving resets the rack and scans forward from the vacated seat. Snapshots store rolls plus the rack-down set (447-511).

## 2. Input

Sensor frame (`protocol/src/sensor.ts:14-42`): gyro deg/s (i16 ×20), gravity-free accel m/s² (×1000), gravity
(×1000), `dtMs` u16, optional quaternion (×16384, avatar arm only). Flags HAS_GYRO 1<<2, HAS_ACCEL 1<<3,
HAS_GRAVITY 1<<4, HAS_QUATERNION 1<<5.

**Authority:** the phone's `createThrowDetector()` (`BowlingController.tsx:15`) commits the throw and sends
`bowling:release` on pointer-up (97-105). The host runs a detector per seat only for the live lane guide (aim,
curve, charge of the active player while aiming); its `feed()` result is ignored (runtime.ts:705-723).

`throw.ts` (82-99): onset 7 m/s², full power 26, release fraction 0.45, refractory 900 ms, aim window 220 ms,
aim range 0.45 m, max hook 0.45 m, full turn 400°/s, full loft 75°.

`feed` (215-300): `dt = dtMs > 0 ? min(dtMs, 250) : 16`; with gravity `elevationDeg = -deg(asin(clamp(g.y/|g|)))`;
yaw rate = gyro·ĝ (fallback gyro.z), `yaw += rate*dt/1000`; `roll = gyro.y` (instantaneous); `thrust = |accel|`
(or `|gyro|/25`); `charge = clamp01((thrust - 7)/19)`. Envelope: onset at thrust ≥ 7, track the peak; auto-release
when `thrust < max(5.6, 0.45 peak)` and not gripped; `power = clamp01((peak - 7)/19)`; pose from the first history
entry at or after the peak; 900 ms refractory.

Previews (131-134): `aim = clamp(-(yaw/45)*0.45, ±0.45)` (sign inverted); `curve = clamp(roll/400*0.45, ±0.45)`;
`loft = clamp01((elev - gripElev)/75)`.

Button path (the one used): `grip()` (183-195) zeroes yaw, roll, charge, history; records `gripElevationDeg`;
no auto-commit while gripped. `release()` (196-213): `thrustNow = charge*19 + 7`; `best` = max of thrustNow and
every thrust in the last 220 ms; `power = clamp((best - 7)/19, 0.04, 1)`; aim and curve from the latest entry;
900 ms cooldown. `buildGesture` (150-162): confidence 1 (accel + gravity) / 0.75 (accel) / 0.5 (gyro only), ≤ 0.6
without gyro; no gyro → curve 0, `hookUnavailable`. Confidence, source, hookUnavailable never affect the sim.

Host validation (runtime.ts:339-352): finite power required; clamp aim ±0.45, power 0..1, curve ±0.45, loft 0..1;
confidence default 0.5. `releaseDisposition` (320-329): only while the room plays, from the active bowler, with the
ball `aiming`; during the intro card the release is held and launched when the card closes (787-802). No in-game
practice detection. `carryGrip.ts` (visual): finger curl 0.45, ball y `-(HAND_HALF_LENGTH + 0.19*0.8)`, z 0.035.
`releasePath.ts` (visual): body clearance 0.42, body front 0.62, floor 0.1, lateral blend 0.08;
`beside = clamp01((0.62 - ahead)/0.2)`, `settle = 1 - clamp01((t - 0.6)/0.4)`, `letGo = clamp01(t/0.15)`.

## 3. Simulation (`sim.ts`): custom, deterministic, no physics engine, no RNG

| Constant | Value |
|---|---|
| `PIN_SPACING` | 0.3048 |
| `LANE_WIDTH` | 1.0668 (half 0.5334) |
| `PHYSICAL_LANE_METERS` | 18.288 |
| `OIL_PATTERN_LENGTH` | 12 |
| `BALL_RADIUS` | 0.108 |
| `PIN_RADIUS` | 0.06 |
| `GUTTER_CAPTURE_EDGE` | 0.5334 + 0.108×0.55 = 0.5928 |
| `PIN_TOPPLE_ENERGY` | 0.24 |
| `PIN_TRANSFER_REACH` | 0.3048×1.85 = 0.56388 |
| `PIN_TRANSFER_COEFFICIENT` | 0.66 |

- Rack (67-80): pins row by row, `x = (seat - row/2)*0.3048`, `z = row*0.3048*0.8660254` (0, 0.26397, 0.52794,
  0.79190). Pin 1 (0,0); 2/3 ±0.1524; 4/5/6 -0.3048/0/+0.3048; 7-10 -0.4572..+0.4572. Negative x = bowler's left.
- Kinematics (143-161): `speed = 7 + 2 power` m/s; `rotationRps = 11 + 5 power + (min(0.45,|curve|)/0.45)*4`.
- Path `ballXAt(shot, z)` (168-177): `t = clamp(z, 0, 18.288)/18.288`; `dry = smoothstep(0.65617, 1, t)` (cubic);
  `x = aim + curve * t(0.12 + 0.88t) * (0.18 + 0.82 dry)`; at the deck `x = aim + curve`. z is clamped, so every
  pin sees the ball at `aim + curve` (a straight line through the rack).
- Gutter (`firstGutter` 319-338): out if `|x| ≥ 0.5928`; out at z 0 → capture 0; else step z by 0.05 then 18
  bisections, returning the high bound. A gutter fells nothing, no impacts; side left if x < 0.
- Path samples every 0.36 m plus the end (gutter capture or 18.288) (340-346).
- Resolve (221-316): `powerCarry = 0.18 + 0.82 power`. Direct per standing pin: `frac = max(0, 1 - |ballX -
  pin.x|/0.168)`, `direct = frac^1.18 * powerCarry`. Waves: every alive unfelled pin with energy ≥ 0.24 topples at
  once and passes `carried*0.66*max(0, 1 - gap/0.56388)*(0.82 + 0.18 power)` to every standing pin within 0.56388,
  capped at 1.25; repeat until nothing new topples. Impacts per standing pin `{energy, directEnergy,
  slide = min(1, E/(0.24*1.35)), scatterSign}`; `scatterSign = sign(pin.x - ballX)`, or if 0
  `sign(sin(id*12.9898 + aim*78.233 + curve*37.719))`. `loft` is stripped (presentation only).
- Split (190-201): only on a full rack; some but not all fell, head pin gone, ≥ 2 left, and some pair > 2.35 ×
  0.3048 = 0.71628 m apart. Converted split = `wasSplit && all standing fell` (204-210).

Phase timing (runtime.ts:189-218, 917-1014): delivery 160 ms; rolling `max(1900, 18.288/clamp(speed,7,9)*1000)`
(2612.6 ms at 7 m/s, 2032 at 9); settle 1150; replay 900; deadwood rake 560; new rack 820. Resolved at launch
(755-785), committed to the card at the end of settling (805-887); then always `reset`, then `aiming`. Clock:
`performance.now()`, ≤ 1000 ms per call, 50 ms sub-steps, 50 ms heartbeat; HUD ≤ every 100 ms; scores every
1000 ms. Strike `before == 10 && felled == 10 && !gutter`; spare `before < 10 && felled == before`. Replay: the last
strike only, from `aiming`, returning to the same rack.

### Visual pin solver (`visualPhysics.ts`, presentation only)

Visual pin spacing 0.68 (2.2310 u/m): pin 0.381 m → 0.850 u, belly 0.0605 m → 0.135 u; ball radius 0.19 u;
gravity 9.81×2.231; deck y 0.02. Fixed step 1/120 with an accumulator (max frame 1 s). Tip angle 0.17 rad, down
π/2, max survivor lean 0.085. Assist after `0.9 + baseZ*0.035 + jitter*0.14` s from the ball's arrival, forcing
`leanRate ≥ 3.4`. Ball contact (424-465): swept segment, one hit per pin; reach `0.19 + 0.135 + sin(lean)*0.425`;
normal z +0.55, clamped [-1, 1.6]; strength `(0.62 + 0.72 energy)*clamp(speed/8, 0.35, 1.6)`; arms at ball z > -0.85.
`strike()` (288-321): survivor nudge 0.05×push, wobble ≤ 0.09; felled pin `leanRate += 5.6p`, `vXZ += 0.85p`,
`vY += min(0.5, 0.16p)`, `spin += (jitter - 0.5)*3.4p`. Integration (467-548): past tip `leanRate += (g/halfH)
sin(lean)*0.13 dt`; friction `e^(-5.2dt)` grounded, `e^(-1.6dt)` airborne; bounce 0.18 below vY -1; survivors
spring back at 3.8, nudge decay 2.4, wobble 1.6. Pin-pin: all 45 pairs, `transfer = clamp((sA + sB)*0.4 +
overlap*2.2, 0, 3)*dt*60*0.2`, separation 0.22 overlap. No ball: `fallbackAt = 0.02 + baseZ*0.045 +
jitter*0.035`. `jitter = sin(id*19*12.9898 + 78.233)*0.5 + 0.5`.

### Lane layout (`laneLayout.ts`, world units)

Deck z -13.05; visual length 13.9; lane front z 1.35; pit depth 2.25; machine z -16.82; masking z -15.02. Visual
lane width 3.15 (`BowlingScene.tsx:120`), `SIM_TO_VISUAL = 2.9528`. Ball z `0.7 - t*15.35`. Rack at `(pose.x, y,
DECK_Z - pose.z)`.

Pinsetter (`presentation.ts:196-291`, smoothstep segments): rake drop 0-0.13, sweep 0.10-0.36, lift 0.36-0.52; y
parked 1.42, deck 0.34; z 0.62 → -2.7. Setter table descend 0.28-0.62, retract 0.70-1; y parked 2.2, deck 1.16;
rack reveal 0.50-0.72; gripper lift 0.32. Fallen pins are raked (standing too on `rackReset`) to the pit floor y
-0.55. The new rack installs at reset progress ≥ 0.5 (`BowlingScene.tsx:703`).

## 4. Presentation

Camera (`cameraPlan`, presentation.ts:102-173):

| Shot | Position | Look-at | FOV |
|---|---|---|---|
| Address (aiming, delivery, rolling, next player) | [0, 3.65, 6.55] | [0, 0.86, -9.55] | 39 |
| Settling ≤ 0.55: impact | [0, 2.55, -6.85] | [endX×0.14, 0.58, -13.6] | 39 |
| Settling > 0.55: result | [0, 3.3, -6.3] | [endX×0.16, 0.48, -13.6] | 40 |
| Replay celebrate | [0, 2.95, -6.5] | (result) | 40 |
| Reset < 0.58: result; ≥ 0.58: next player (address) | | | |

Director (`avatar/motion/cameraDirector.ts`, presentation.ts:460-517, BowlingScene.tsx:843-889): damping response
5.2 (result), 3.4 (replay), 3.05 otherwise; dt clamped 1/20 s; fov damp 4; hard cuts snap; no hard cut during
delivery. Aim shot while aiming with a live hand frame and charge > 0.12: subject `(stanceX, 0, 1.72)` (stanceX
-0.18 right-handed, +0.18 left), position `(sx + 0.85, 2.0, 4.32)` → `(sx + 0.2125, 0.924, -5.28)`, fov 35. Reaction:
hard cut on reset, 1200 ms, `(sx + 1.3175, 2.2 - 0.06s, 8.0 + 0.24s)` → `(sx*0.15, 1.1, -7)`, fov 31. Turn-change
dolly (1500 ms): `swing = sin(ease(age/1400)π)`, position + (1.15, -0.55, -1.4)·swing, look + (0.25, 0.35, 0)·swing,
fov +2·swing. No focus cut. Reduced motion: address tripod. Intro and result cameras: `BowlingCanvas.tsx:107-136`
(result `[0, 3.3, -6.3]` → `[0, 0.35, -12.9]` fov 42).

Ball (`syncBall`, BowlingScene.tsx:1690-1861): aiming, parented to the hand (re-attaches once reaction weight ≤
0.02); delivery, detached and blended to the roll start `(pathX(0)*2.9528 + offset, 0.18 + (loft > 0.02 ? 0.1 +
0.45 loft : 0), 0.7)` with an arc `sin(πp)(0.06 + 0.26 loft)`; rolling `x = pathX(t)*2.9528 + lateral`, `y = 0.18 +
air` (air window `0.05 + 0.13 loft`, height `(1 - a²)(0.1 + 0.45 loft)`), spin `-= dt*rps*2π` (×0.72 in replay), yaw
`(endX - aimX)*t*0.45`; gutter after capture (clamped 0.08..0.94) x `±1.755`, y drops 0.52; settling sinks into the
pit `z = DECK_Z - 2.1 - 0.65p` (gutter ball at x ±1.785, z `DECK_Z + 0.1`).

Lane guide (1908-1951): 24 dots along `previewXAt` at `z = 0.15 - t^0.78*13.3`, opacity `0.42 + 0.4 charge`, scale
`(0.65 + 0.3c)(0.75 + 0.45t)`, aiming only, seat colour.

FX (`fxPlan.ts`): impact at rolling → settling with `s = felled/standing`: sparks `round(10 + 30s)`, chips
`6 + 16s`, dust `4 + 6s`, shake `0.1 + 0.26s` (s 0: dust only). Outcome after 0.3 s: strike 22 confetti, 2 rings,
flash 0.24 `#fff1d6`, shake 0.22; spare 11 confetti, 1 ring, flash 0.14 `#e6e2ff`. Gutter dust 7, shake 0.08.
Beats in order (presentation.ts:40-58): gutter, split-convert, strike/turkey (streak ≥ 3), spare, split, good (≥ 7),
open.

Turn handoff (`turnHandoff.ts`): walk-off 2100 ms, walk-on 1900; offstage +10.6 x, 0.7 z, yaw 1.25; waits while
reaction > 0.5 (≤ 2500 ms); appearance swap at the far point; interrupted by delivery/rolling.

Avatar: `[±0.18, 0, 1.72]` facing -Z. Drive: aiming `min(0.68, charge*0.68)`; delivery `0.68 + 0.32p`; reset 0.18;
else 1. Pose coefficients in `avatarMotion.ts` (phases 115-154, delivery/rolling 283-340, celebrate 376-429;
quintic smoothstep). Celebrate: strike, spare, or good (≥ 7). Expression: joy 1 / 0.85 / 0.65; disappointed 1 on a
gutter, 0.82 on 0 pins; effort `min(0.9, 0.18 + 0.9p)` in delivery; focus 0.35 aiming.

Venue: lane centres x [-7.1, -3.55, 0, 3.55, 7.1] (`venue/venueLayout.ts:19`). Four ambient lanes run a seeded
xorshift timeline (`ambientMotion.ts`): cycle `13.4 + 5.4 seed` s; delivery 1.1, rolling 1.72, impact 1.36, reset
2.6 s; 6-10 pins fall; target x ±0.36; rack set at reset 0.44. Lane mirror (planar reflection, strength 1.7, high
tier). Pin lathe profile `pinGeometry.ts:15-42`; neck bands at t 0.658-0.686 and 0.704-0.732, red `#c41632`, ivory
`#f4efe6`.

HUD (`BowlingCanvas.tsx:162-319`): "n / 10", ball number, who is up; speed dial (max 12 m/s, target 7-9); spin;
pins standing; 10-frame table with X, /, – (blank pending cumulatives); "Replay strike" while aiming after a strike.

## 5. Sound (`sound.ts`, `audio/cues.ts:277-365`)

Synth: `bowling.release` (whoosh 260 → 1280 Hz + 118 → 74 Hz sine), `bowling.roll` (1.5 s lowpassed noise
760 → 250 Hz + 96 → 62 Hz triangle), `bowling.pins`, `bowling.gutter`, `bowling.sweep`. Samples:
`bowling-strike.wav`, `bowling-spare.wav`. Triggers (37-67): delivery → release gain `0.7 + 0.3 charge`; rolling →
roll rate `0.88 + 0.25 charge`; settling → gutter, or pins gain `0.5 + 0.44i` rate `0.9 + 0.16i` with
`i = clamp01(felled/standing × (0.72 + (speed - 7)/10))`; reset → sweep; replay → roll 0.82/0.72, pins at ≥ 0.74;
results: turkey → strike (1.08, rate 1.14), strike → strike, split-convert → spare (1.12, 1.18), split → spare
(0.34, 0.72), spare → spare. Ambience: every 240th observation while aiming, roll gain 0.08 rate 0.46.

## 6. Phone controller (`BowlingController.tsx`)

Active: "Grip the ball" / "Swing — let go to release" / "Roll read"; SVG lane (240×310) with a quadratic aim line
(end x `120 + aim*150`, control `120 + aim*95 + curve*180`); aim ±0.11 → Left/Right/Centre pocket; hook ±0.08 →
Hooking left/right/Straight through ("Straight ball" without gyro); speed and rps; power meter; HOLD TO GRIP with
pointer capture: down → `grip()` + buzz 12; up → `release()`, send `bowling:release {aim, power, curve, loft,
confidence, source, hookUnavailable}`, buzz [18, 24, 34]; cancel/lost capture/visibilitychange/pagehide → no send;
"Rolled!" 260 ms. Inactive: waiting text and a replay strip from `bowling:replay-outcome {playerId, pins, label, beat}`.

## 7. Golden tests

engine: 20×0 → 0; 20×1 → 20; 12×10 → 300 done; spare + 3 → frame 13 (total 16); strike + 3,4 → 17/24; turkey
30/50/60; 10th 10,10,10 = 30, spare + fill = 20, open = 9; pending frame null without poisoning closed frames;
kinds [strike, spare, open]; a strike ends the turn; an open first ball keeps the player (standing 6); 10th strike →
rackReset, standing 10, turn continues; [4,6] → spare; negative/fractional → [0,4]; 2 players alternate 0,1,0,1 to 300 each.
sim: `speedForPower(0) = 7`, `(1) = 9`; `{0, 0.65, 0.2}` → 8.3; pocket `{-0.15, 1, 0.22}` → all 10, pin 10 direct 0;
`{0, 0.02, 0}` no gutter, < 10; aim 0.7334 → gutter, none; `{0.3, 0.9, 2.5}` right gutter with 12 < gutterAt <
18.288; `{-0.42, 0.6, 0}` → pin 10 stands; `{-0.49, 1, 0.03}` on [7,10] → [7]; full power at ±0.3, ±0.45 never
strikes; mirror symmetry with map [0,1,3,2,6,5,4,10,9,8,7]; `ballXAt(deck)` curve 0.4 → 0.4.
throw (rest gravity (0,0,-9.81), dt 16, half-sine accel on y): peak 200 → power 1; |curve| ≤ 0.45; no gyro → curve
0; gyro-only confidence ≤ 0.5; no swing → 0.04; full arm rise → loft > 0.9. runtime: durations 18.288/7 and /9 s;
gutter capture progress in 0.6-0.85. presentation: rack reveal 0 at 0.5, 1 at 0.72. visualPhysics: 30 vs 60 Hz
within 1e-4; no tunnelling at 2-10 fps.

## 8. Port decisions

| # | Web | Default |
|---|---|---|
| W1 | Ball and pins use different lateral visual scales (2.9528 vs 2.2310 u/m); the assist hides the mismatch | Fix: one scale, driven from the sim |
| W2 | Hook never changes pin entry (rack sees a straight line at aim + curve) | Keep for parity (it is the scoring model); the upgrade pass may revisit |
| W3 | `strikeStreak` / `lastStrike` global across players | Fix: per player |
| W4 | Turn-change dolly almost never fires | Fix: fire on the next aiming after the reaction hold |
| W5 | 10th-frame "5 / /" notation | Fix |
| W6 | Ambience counter per render frame | Fix: on a timer |
| W7 | Max visual frame step 1 s (comment says 200 ms) | 200 ms |
| W8 | Local play | Gamepad: hold RT to grip, right stick sets aim/curve, flick the stick forward to release (power from flick speed); mouse: drag back and throw |
