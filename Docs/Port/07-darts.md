# Bullseye Bash (darts): port spec

Source: movecade-v2 `app/src/games/darts/` (paths relative to it). Parity first. Part of the multi-game port
([05-games-framework.md](05-games-framework.md)). Board space: double-outer = 1.0, +x right, +y up. World:
metres, three.js Y-up, camera looking along -Z.

## 1. Rules (`engine.ts`)

- `DARTS_PER_ROUND` 3, `ROUND_COUNT` 3: 9 darts each (:66-68). Countdown 3000 ms. Backstop clock
  `180000*max(1, floor(n))` ms (:77-81). `FLIGHT_MS` 620 (:84). Callout effect 1100 ms; round/finished 1600 ms
  (:86, :370); at most 20 effects, oldest out (:362-384).
- **Called shot:** `BOUNTY_SECTORS = [19,16,20]`; `bountyForRound(r)` = 19 for r < 0, else `[r % 3]` (:142-147).
  A dart in the bounty sector with score > 0 scores `hit.score*2` (:512-514): any ring of that sector, never bulls.
- **Round bonuses** when `dartsThrown % 3 == 0` (:553-577): +20 if `roundHits == 3` (a hit is scoreDelta > 0);
  +40 if `roundScore ≥ 100` (bonuses excluded); `bestRound = max(bestRound, roundScore + bonus)`; 'round' effect
  `Round N: X (+b)`; round++, tallies reset; at round ≥ 3 the player is finished ('finished' effect).
- **Max 1170:** 3×T19×2 = 342 +60, 3×T16×2 = 288 +60, 3×T20×2 = 360 +60 (`engine.test.ts:340-366`).
- **Turns:** one shared board. `currentPlayerIndex` throws 3 darts. `throwDart` needs playing, an active unfinished
  player and `turnDarts < 3` (:502-505). The turn passes only when no flight is in the air and `turnDarts ≥ 3`, to
  the next unfinished index with wraparound (:393-408, :473): A(3) B(3) C(3) D(3), then round 2. The match ends with
  no flights in the air and (time up or everyone finished) (:477). Winners: everyone tied at the best score (:387-391).
- **Timing:** the score commits at throw time; the flight's `progress += dt/620`; on arrival the landing effect
  (kind bull / miss (score 0) / bounty / hit; label 'Miss', `T20 ×2` or plain; intensity 0.4 on a miss else
  `min(1, 0.35 + delta/60)`) (:441-496). Landing `ageMs` counts after its flight is gone, capped near 2000
  (:460-468). `stepMatch` clamps dt to [0,100] (:424); the host slices into ≤ 100 ms steps with ≤ 1000 ms catch-up.
- Outcome label: 'Miss', `Bounty T19!` or plain (:584).

### Landing model (`landingPoint`, :317-360), no RNG

```
aimX, aimY = clamp(±1); x = aimX*1.12; y = aimY*1.12
y += (power (NaN → 0.55) - 0.55)*1.9
x += clamp(spin/420, ±1.5)*0.85
y += clamp(roll/520, ±1.5)*0.45
u = 1 - clamp01(steadiness (NaN → 1))
if u > 0: L = hypot(aimX, aimY); L > 0.001 ? (x,y) += (aim/L)*u*0.55 : y -= u*0.55
if hypot(x,y) > 1.32: scale onto radius 1.32 (CATCH_RADIUS)
```
Flight `curve = clamp(spin/420, ±1)`, flight power `clamp01(power)`.

## 2. Board (`board.ts`, `boardWires.ts`)

- Sectors `[20,1,18,4,13,6,10,15,2,17,3,19,7,16,8,11,14,9,12,5]` clockwise from 12 o'clock, 18° each;
  `sectorIndexAt = floor(wrap(π/2 - atan2(y,x) + 9°)/18°)`; centre angle `π/2 - i·18°` (:16, :63-74).
- Radii (normalised mm/170): bull 6.35, outer bull 15.9, treble 99-107, double 162-170 (:25-30).
- Scoring order: r ≤ bull → 50 'Bullseye'; ≤ outer bull → 25 'Bull'; r > 1 → Miss; r ≥ 162/170 → `D{n}`;
  99/170 ≤ r ≤ 107/170 → `T{n}`; else single; non-finite → Miss (:77-112).
- World: `DARTS_BOARD_RADIUS` 0.5 m (a 1 m face, ~2.94× regulation, `pubLayout.ts:14`); centre y 0.45, z 0; floor
  y -1.32. Surround drawn to 1.22 (`DartsScene.tsx:538`), darts land out to 1.32.
- Wires: radius 0.0032 m; six torus rings at the six radii (32 segments below r 0.2, else 120); 20 spokes from
  outer bull to double along sector edges (centre - 9°); 0.012-wide wire beds in the baked face.
- Face: even index cream `#f7ead9` single, red `#f0508e` rings; odd `#2a1440` single, `#5ec8e5` rings; outer bull
  `#5ec8e5`, bull `#f0508e`, surround `#3d1c5c` (:136-141, :542-563). Number ring text at 864/2048 on a 2.72R
  plane; brass torus at 1.12R (:1106-1111).

## 3. Input

**Phone (authoritative):** the controller runs its own `createThrowDetector()` on the unthrottled samples
(`DartsController.tsx:198`). The pad uses pointer capture: down → `grip()` + buzz 8; up → `release()` and sends
`darts:release` + buzz 12 + 220 ms flash (:232-261). Payload `{aimX, aimY, power, spinDegPerSec, rollDegPerSec,
confidence, steadiness, peakMs2, durationMs, source}`. pointercancel, lost capture, visibilitychange, pagehide and
orientation change → `cancelGrip` (release, nothing sent). Landscape-locked. Reticle `local + (server - local)*0.12`
at `translate(aim.x*45%, -aim.y*45%)`, scale `1 + charge*0.5`. Re-centre → `detector.recentre()`. `canThrow` =
my turn (`darts:turn`) && playing && darts left && landscape.

**Host:** one detector per seat, fed only for the active player, for the reticle only, cleared on turn change
(`runtime.ts:579-614`, :718). Release clamps (:209-226): aim ±1.5, power [0,1], spin/roll ±2000, confidence
default 0.5, steadiness default 0.5, peak [0,120], duration [0,1000]. Settle elapsed before `throwDart`
(:639-642); releases dropped during the intro card unless resumed. Emits `darts:result {playerId, label, score,
bounty, roundBonus, finished}`, `darts:replay-outcome`, `darts:aim [{p,x,y}]` (3 dp, every 80 ms), `darts:turn
{activePlayerId, activePlayerName, dartsLeft, round, phase}` on change or every 250 ms. HUD 100 ms, scores 1000 ms,
heartbeat 50 ms.

**Detector** (`THROW_TUNING`, throw.ts:163-234). Sample dt `dtMs > 0 ? min(dt,250) : 16`.
- With gravity (|g| > 1, :389-440): `pitch = -asin(ĝ.y)` deg; neutral captured after 260 ms still and not throwing
  (still: `||g| - 9.81| < 1.2 && |accel| < 1.6`) → `neutralPitch`, yaw 0. `targetY = clamp((pitch - neutral)/26)`;
  `yawDeg += (ω·ĝ)·dt`, leak `yawDeg *= max(0, 1 - yawLeakPerSec·dt)` with **yawLeakPerSec 0**; `targetX = clamp(yaw/22)`;
  no gyro → `targetX = gamma/22`. Without gravity: `targetY = (beta - firstBeta)/26`, `targetX = gamma/22`.
- Smoothing: `speed += (|Δtarget|/dt - speed)*0.3`; blend `0.11 + 0.41*clamp01(speed/2.1)`;
  `steadiness = clamp01(1 - speed/2.1)`; 32-sample history.
- Envelope (:586-657): `env += (|v| - env)*0.5` (v = user accel m/s², or gyro deg/s without accel); onset 6
  (gyro 240); peak keeps yaw `-(ω·g)/|g|` (fallback ω.z) and roll `ω.y`; forward thrust if `v.y/|v| ≥ 0.15`.
  Ungripped auto-release (not used in play: the grip path never auto-commits) at `env ≤ max(peak*0.4, 4)` (gyro
  floor 144) or elapsed > 420 ms; needs peak ≥ 11 (gyro 460), 45-420 ms, thrust seen; 700 ms cooldown.
- `release()` (:493-532): `power = max(0.05, clamp01(peakOrEnv/30))` (gyro ref 900); aim from history at
  `clock - (140 + elapsed)` ms (newest old-enough sample, else oldest); `confidence = clamp01(0.5 + 0.3 overshoot +
  0.2 steadiness)`, capped 0.6 (gyro release) / 0.65 (no gravity) / 0.7 (no gyro); unused by scoring. Sweet spot
  power 0.55 = a 16.5 m/s² peak.

## 4. Flight presentation (`DartsScene.tsx:1281-1295`, `motion.ts`)

No physics: a scripted 620 ms curve to the predetermined point. `travel = 1 - (1-t)³`;
`arc = sin(π·smootherStep(t))`, `smootherStep = t³(t(6t - 15) + 10)`. With `THROW_DISTANCE = 4.3`:
```
z = 4.3(1 - travel) + 0.025 travel
x = -0.57(1 - travel) + X*0.5 travel + curve*0.9 arc
y = 0.23(1 - travel) + (0.45 + Y*0.5) travel + (1 - power)*1.1 arc
```
Pitch `.18 + (1-p)*.18 cos(πt)`, yaw `curve*.4(1-t)`, both blended to (.14, -.08) by smootherStep; Euler
`(-π/2 + pitch, yaw, 0, 'YXZ')`; tip offset `0.62*0.48` along the axis; render scale 0.48.
Stuck dart (:1369-1392): `bounce = sin(age/38) e^(-age/260) (1 - e^(-age/70))`; pos `(X*0.5, 0.45 + Y*0.5 +
bounce*.08, .025 + bounce*.008 + min(1, age/420)*.002)`; pitch `.14 + bounce*.035`, yaw `-.08 + bounce*.025`. Only
the current visit's darts show (`round == floor((thrown - turnDarts)/3)`).
Trails: 3 stamps at lags .08/.16/.24, scale (.018, .07, .018), colour `bright*fade*0.3`. Board wobble: target
`max(target, miss ? 0.25 : 0.72 intensity)`, decay 2.8/s, damped λ 28; `rot.z = sin(id*.001 + a*18)*a*.016`,
`rot.x = cos(id*.001 + a*11)*a*.009`. Reticle ring 0.1-0.15 at z 0.3, breath `1 + .08 sin(3t + lane)`, aim
damped λ 72, hidden in the reaction shot. Callouts: impact ring 520 ms scale `(0.9 + 1.35p)*pulse`; popup
0.96×0.27, 1100 ms, rising .26 → .68; banner 1.5×0.42, 1700 ms at y .62; bounty wedge opacity
`0.3 + 0.22(0.5 + 0.5 sin 4t)` (+0.14 for the leader).

## 5. Impact FX (`impactFx.ts`)

| Tier | Dust | Sparks | Spark speed | Ring m | Shake | Flash |
|---|---|---|---|---|---|---|
| wall | 3 | 0 | 0 | 0 | .10 | 0 |
| surround | 5 | 0 | 0 | 0 | .14 | 0 |
| single | 8 | 0 | 0 | 0 | .18 | 0 |
| double | 9 | 10 | 2.2 | .26 | .28 | .08 |
| treble | 9 | 16 | 2.6 | .30 | .34 | .12 |
| outer bull | 10 | 20 | 2.8 | .34 | .38 | .14 |
| bull | 12 | 32 | 3.4 | .50 | .50 | .22 |

Gold accent for both bulls, else white. Bounty: sparks `max(12, round(s*1.3))`, speed `max(2.4, ·)`, ring
`max(.3, ·)`, shake `min(.6, +.08)`, flash `min(.3, +.06)`, lane colour. Celebrations (confetti/sparks/shake/flash):
finished 48/28/.35/.2; round bonus ≥ 20 36/20/.3/.16; bonus > 0 21/12/.22/.1. Emitters (`DartsScene.tsx:1790-1921`):
gravity dust -1.1, spark -6.5, confetti -2.4; capacities 96/160/16 rings/160.

## 6. Pub

One board, one avatar (laneCount 1, `DartsScene.tsx:1089`). Room (group at z -0.75): floor -1.32, ceiling 5.5,
depth 22, width `max(frustum width, 2*1.15 + 2.8)` (`pubLayout.ts:69-77`); brick back wall, green plaster sides,
1.35 m wainscot; beams at z 1.4/6.2 (high tier 1.4/5.2/9.4); oche slab at local z 5.4, brass line 5.18, label decal
4.62; windows (z 2.6/7.2), art, chalkboard, bar with bottles, foot rail, pints (`DartsPub.tsx:297-494`). Cabinet
back 2.15×2.35×0.34 with rails, sides and a glowing lane accent (`DartsScene.tsx:1233-1263`); lamp hood at
`(0, 1.75, 1.37)`. Patrons 3/2/1 by tier, seated at bar x ±0.78 staggered, scale 0.72, no shadows, sway
`sin(0.8t + 1.7i)`. Light: key `[3.5,9,8]` 1.25 `#ffd2a1`; hemisphere 0.42 `#3c5a6b`; rim 0.4 `#3fc9c2` from
`[-2,8,-8]`; fog 9-34 m; exposure 1.08, contrast 1.12; bloom 0.9 / 0.9 / 0.72; vignette 0.5 (`pubMood.ts`). Board
spot `(0, 2.55, 3.2)` angle .36, penumbra .65, intensity 34, distance 9; pendants oche 16/9 m, bar 7/5 m, decay 2,
`#ffc98c`.

## 7. Avatar (`DartsAvatar.tsx`)

x -0.85, floor, z 4.65, yaw 0.42, scale 0.92, seed lane + 41. Drive `min(1, 0.28 + |aimY|*0.38)`; wrist bias
`(aimX*.17, aimY*.2 + flick*.1, -flick*.26)`, `flick = sin(πp)(1 - .35p)` over 320 ms from a new flight; look-at
`(aimX*0.5, 1.77 + aimY*.12)`. Held dart only while active with no flight (scale 0.52, dominant hand; support hand
offset (-.2, -.08, .02)). Reactions: bull/bounty 'bullseye', miss 'nearMiss', hit 'goodShot', end matchWon /
matchLost. Profiles: lean, weightShift .7 (`bodyFollow.ts:178`); grip (-28, 0, -6), reach .8, clamp .14 (`handTarget.ts:211`).

## 8. Camera (`avatar/motion/cameraDirector.ts:231-317, 646-668`, overrides `DartsScene.tsx:859-914`)

Home `[0, 1.1, 6.7]` → `[0, .55, 0]`, fov 44, x drift `0.16 sin(0.24t)`. Aim (playing, no flight, pose < 360 ms
old): `(sx + .48, sy + 1.87, sz + 1.9)` → `(sx + .12, sy + .8525, sz - 7)`, fov 40. Reaction: hard cut on a new
landing, 1200 ms, refused during a flight; `settle = smoothstep(age/900)`; `(sx + .52 + hx*.12, sy + 1.55, sz + .15
- .28 settle)` looking at the landing ×0.3, fov `34 - 2 settle`. turnChange 1500 ms when the leader lane changes.
Board focus needs strength ≥ .82 but strength ≤ .72: never fires. Easing `1 - e^(-8dt)`; fit
`z ≥ (0.69 + |x|)/(tan(fov/2)*aspect*.92)`. Intro `[1.7, 1.7, 8.2]` fov 46; results `[2.15, 1.5, 6.4]` →
`[-.3, .55, .3]` fov 40.

## 9. Sound (`sound.ts`, `app/src/audio/cues.ts:377-545`)

New flight → `darts.release` rate `.9 + .35p` gain `.6 + .4p`. Arrival score 0 → `darts.miss`; else `darts.thud`
rate `.92 + .18 min(1,r)`, plus bull → `darts.bull` (outer bull: rate .75 gain .55), treble → `darts.wire` gain 1,
double → gain .8, bounty → `darts.bounty`. Round → `darts.round` (rate 1.12 gain 1 with a bonus, else 1 / .7).
`darts.pub-chatter` at 2500 ms then every 9000 ms. `match.over` at the end. Pan `((lane - (n-1)/2)/((n-1)/2))*.55`.
Gain/throttle: release .4/80, thud .6/40, chatter .18/7000, wire .34/60, bull .62/120, bounty .55/120, miss .3/80,
round .36/200. Files `app/public/audio/darts-*.wav`; synth fallbacks for release, thud, chatter, wire, miss.

## 10. Golden tests

engine: aim 0 at the sweet spot → (0,0), 50; `aimY = 0.5/1.12` → single 20; 3×S20 = 80; 3×bull = 210; miss + 20 +
20 = 40 (no bonus); bounty doubles only in round 2 (20 → 40); perfect match 1170; wild input stays ≤ 1.32; NaN →
finite; rotation only after flights settle (`run(FLIGHT_MS + 100)`). board: sector layout, treble/double order.
throw: neutral after 40 × 16 ms still; yaw kept ≥ 95% after 300 still frames; captured sector-19 trace
(:221-255); grip never auto-commits; a lift without a flick → power in [0.05, 0.1); a wind-up > 420 ms accepted.
pubLayout `cameraZ(2) = 8`.

## 11. Port decisions

| # | Web | Default |
|---|---|---|
| D1 | `yawLeakPerSec` 0 (doc says 0.55; tests need 0) | Port 0 |
| D2 | The throw is the phone's detector at thumb-lift (several comments say otherwise) | Port the phone path; the host only shows the reticle |
| D3 | Steadiness is measured at release, not before the thrust | Keep |
| D4 | Board focus shot can never fire | Keep inert (camera polish may revisit) |
| D5 | Board is 2.94× regulation, oche 4.65 m from the face | Keep (it is the game's readability) |
| D6 | Hard-coded 3 instead of `DARTS_PER_ROUND` in HUD/turn payload | Use the constant |
| D7 | Gyro in deg/s; UE motion APIs give rad/s | Convert at the boundary |
| D8 | Local play | Gamepad: stick aims, RT pull-and-release sets power; mouse: hover aims, drag-flick |

## 12. Port status (2026-10-21)

- **Rules and throw:** `Games/Darts/DartsRules` (board, landing model, match) and `DartsThrow` (the detector),
  with golden tests `MovecadeKart.Darts.{Board,Landing,Match,Scoring,Throw}` (the perfect match is 1170; the
  captured sector-19 trace lands in 19). The engine steps on a fixed 60 Hz clock.
- **Pub:** `ADartsPub` builds the room in code: brick, plaster, wainscot, beams, oche and brass line, night windows,
  prints, the bar with bottles and pints, a chalkboard that chalks the live scores, pendants, and the cabinet with its
  lamp hood. The board is modelled from its sectors and rings (sisal colours, wire spider, brass hoop, number ring)
  on a pivot that wobbles on landings; the bounty wedge pulses on the called sector. The pub holds exposure to
  EV 7.6-8.6 (a lit board in a dim room) and scales its emissives by `GlowGain`.
- **Game:** `ADartsGame` flies the scripted 620 ms arc and sticks and bounces the darts. It shows the reticle and
  the held dart, poses the thrower (wind-up, the 320 ms flick, reactions), and directs the camera (home, aim, the
  reaction cut, results). It plays the cues and the impact tiers (dust, sparks, ring, flash; confetti on bonuses).
  The finished visit lingers 1.6 s after the board passes (the web clears it at once).
- **Controls (D8):** WASD, the left stick or the mouse aim. Hold Space, RT or click: the meter swings, with the sweet
  spot marked; let go to throw. A mouse flick sets power from its speed and spin from its drift. Bots aim for the
  called shot by skill.
- **Party Mode:** the lobby's GAME row offers Bullseye Bash. `ADartsGameMode` seats the room and implements the
  runtime.ts protocol: advance-turn null, darts:turn and darts:aim, darts:result, replay outcome and feedback, plus
  scores, countdown and completion. Test: `Tools/net-sim/e2e.ps1 -RoomGame darts -Bots 0 -QuitAfter 60
  -SimSeconds 110` (`phone-sim.mjs --game darts`) plays nine darts to signed scores.
