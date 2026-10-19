# 01 — Kart handling, physics and camera (parity spec)

Source: `C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2` (read-only, analysed 2026-10-19 at `58bf0476`).
All paths below are relative to that repo. Line numbers are 1-based, `path:line`.
Numbers in code blocks are copied verbatim from source.

> **Read this first.** The kart sim is **not** in `app/src/games/kart`. That folder holds the v2 shell (plugin
> manifest, phone controller, tracks, materials, camera ground helper, audio drivetrain). The physics lives in the
> legacy tree: **`app/src/legacy/Experience/kart/engine/MultiplayerKart.tsx`** (1825 lines, one `useFrame`).
> The constants are in `app/src/legacy/Experience/kart/engine/constants.ts` and in
> `app/src/legacy/Experience/kart/engine/performanceQuality.ts` (boost tiers, despite the file name).

### Live vs dead code paths (what actually ships)

Shipped chain: `games/kart/index.ts` (plugin) → `games/kart/KartCanvas.tsx` → `legacy/games/kart/KartRaceCanvas.tsx` →
**`MultiplayerApp`** (always in prod; single-player `App.jsx` is behind `import.meta.env.DEV`, `KartRaceCanvas.tsx:26-30,137-142`) →
`MultiplayerTrackScene` → `MultiplayerKart` ×N + `models/Kart.jsx`.

| File | Status | Role |
|---|---|---|
| `legacy/Experience/kart/engine/MultiplayerKart.tsx` | **LIVE** | All kart physics. Its own internal state (`speedRef`, `turbo`, `driftPower`… refs) is the truth; the store gets a copy |
| `legacy/Experience/kart/engine/constants.ts` | **LIVE** (`speed`, `response`, `drifts`, `kartPedalTargetSpeed`); `acceleration.*` dead | Speed and drift constants |
| `legacy/Experience/kart/engine/performanceQuality.ts` | **LIVE** | Start, drift and slipstream boosts, quality tiers, turn compensation |
| `legacy/Experience/kart/engine/store.ts` | **LIVE** | `KartState`, `RACE_CONFIG` (`:444-458`), checkpoints, race position. Its `speed` field is output-only for physics |
| `legacy/Experience/kart/engine/{trackRecovery,recoveryWatchdogs,spawnGrid,BoostPads.jsx,useItemAction,MultiplayerItems}.ts*` | **LIVE** | §6–§8; items in spec 02 |
| `legacy/Experience/kart/engine/MultiplayerMobileControlBridge.tsx` | **LIVE** for steering (gamma → steeringInput); its button branch is effectively bypassed (buttons arrive via `legacyBridge.ts` `kart-input`) | §3.1 |
| `legacy/Experience/kart/engine/{KartRaceCamera,HostCameraFollow,SplitScreenRenderer}.tsx`, `misc/KartViewPost.tsx` | **LIVE** | §10 |
| `legacy/Experience/kart/engine/models/Kart.jsx` | **LIVE** (visual only; but it writes the global `boostPower` that leaks into physics, §13.1) | §9 |
| `legacy/Experience/kart/engine/AutoPilot.tsx` | live, promo mode only | §11 |
| `legacy/Experience/kart/engine/{App.jsx,TrackScene.jsx,PlayerController.jsx,MobileControlBridge.jsx}` | **DEAD in prod** (DEV 'D'-key single player; `PlayerController.jsx` is a separate older physics, **do not port**) | — |
| `MultiplayerKart` non-host branch (`:747-834`), `hooks/useKartRaceNetworkSync.ts` kart-sync broadcast | **DEAD in v2** (TV is sole host; `sendData` only forwards countdown) | — |
| `games/kart/kartContact.ts` | code live, **effect dead** (`otherKarts` always empty) | §7.3 step 6 |
| `games/kart/{kartCollisionMeshes,boostPad,cameraGround,kartPaint,vehicles,navigation}.ts`, `tracks/{catalog,harbor,harborBoundary,meshBoundary,trackColliders}.ts` | **LIVE** (imported by the legacy engine) | v2-side helpers the legacy sim calls |
| `games/kart/drivetrain.ts` | live, **audio only** (`sound.ts`) | §2.6 |
| `games/kart/scene/{trackSurfaces,worldSurface}.ts` | live, **render only** | §5 |
| `games/kart/KartController.tsx` | **LIVE** (phone) | §3.1 |

Where the v2 folder and the legacy engine differ or overlap: there is **no second physics** in `app/src/games/kart`. The v2 files are helpers
the legacy sim imports (walls, contact, colliders, pads, paint, camera ground). The only duplicated concepts are the following.
(a) Two waypoint helpers: `games/kart/tracks/catalog.ts` (live, re-exported by `legacy/.../trackConstants.ts:1-2`) and the copies in
`tracks/violetLoop.ts:283-306` (Violet-only, used only via the catalog re-export).
(b) Two steering-sign conventions for autopilots (§11).
(c) Two "speed" stores: `speedRef` (truth) vs `store.karts[id].speed` (published copy; writes to it are ignored, §13.2).

---

## 0. Units and coordinate conversion

| Quantity | Web (three.js) | UE 5.8 | Conversion |
|---|---|---|---|
| Length | 1 unit. The code calls it a metre (`spawnGrid.ts:7`, `kartFxBus.ts:22`). The kart body is about 1.95 × 2.87 units. | cm | ×100 |
| "Speed" | `speedRef` is labelled **mph** (`constants.ts:1`), but movement is `pos += dir * speed * delta` (`MultiplayerKart.tsx:1380,1418`). So it is **units per second**. The HUD treats it as **km/h** and shows `speed * 0.621371` as "MPH" (`KartRaceHUD.tsx:829-831`, `RaceUI.tsx:709-711`). | cm/s | ×100 for motion. **Keep the legacy number as the sim state** ("SU", speed units) so every threshold (20, 30, 40, 70…) ports verbatim. Convert only when moving the actor. |
| Angles | radians | degrees | ×180/π |
| Axes | right-handed, +Y up. Kart forward = **−Z** at `rotation.y = 0` (`MultiplayerKart.tsx:1249-1253`). | left-handed, +Z up, +X forward | `UE.X = −three.z`, `UE.Y = three.x`, `UE.Z = three.y` (all ×100). The determinant is −1, as the handedness flip needs. |
| Yaw | `rotation.y = θ`. Forward = `(−sin θ, 0, −cos θ)`. **+θ turns left** (counter-clockwise from above). | Yaw ψ. Forward = `(cos ψ, sin ψ)`. +ψ turns right. | **`ψ_deg = −θ · 180/π`**. A three.js heading `atan2(−dx, −dz)` becomes UE `atan2(dY, dX)`. |
| Steering input | `steeringX ∈ [−1,1]`, **positive = steer LEFT** (`MultiplayerKart.tsx:1188`, `1207`). | Pick UE convention `SteerRight = −steeringX` | Flip the sign once at the input boundary and nowhere else. Every formula below uses the legacy sign unless it says otherwise. |
| FOV | `PerspectiveCamera.fov` is the **vertical** FOV in degrees. | `UCameraComponent::FieldOfView` is **horizontal**. | `hFOV = 2·atan(tan(vFOV/2)·aspect)`. 75° v → 107.5° h at 16:9. 72° v → 104.5° h at 16:9. 75° v in a 2-player top/bottom viewport (aspect 3.556) → 139.7° h. Recompute it every frame from the viewport aspect. |

Road height: the physics runs on a **logical plane y = 0** for both tracks (`tracks/catalog.ts:28-38`). Violet Loop's drawn
road is at `VIOLET_GROUND_Y = -1.605` (`catalog.ts:18`); Sunset Harbor's is at `HARBOR_GROUND_Y = 0` (`catalog.ts:19`). The drawn
kart is dropped onto the road by four wheel raycasts (visual only, §9). **UE recommendation:** put both roads at Z = 0
and drop the −1.605 offset. The camera formulas below are re-expressed relative to the road surface (§10.2).

---

## 1. Where the simulation runs, tick rate and integration

* **Host TV only.** The phone is a pure input device (`games/kart/KartController.tsx`). It draws a wheel and sends
  pedals and sensor frames. The PartyKit server is only a relay (`partykit/src/engine.ts` has no kart physics; it only stores the map id).
* `MultiplayerKart` simulates when `isLocalPlayer || isHost` (`MultiplayerKart.tsx:739-745`). In v2 the TV is the host
  and simulates **every** kart, including lobby bots. The non-host branch (network interpolation,
  `MultiplayerKart.tsx:747-834`: `INTERPOLATION_SPEED = 10`, `MAX_INTERPOLATION_DISTANCE = 5`, 200 ms prediction cap) is
  effectively dead in v2. The bridge replaces `sendData` and forwards only the countdown (`games/legacyBridge.ts`,
  "sendData: publishKartCountdown"), so the 30 Hz `kart-sync` broadcast (`store.ts:454`) never leaves the TV.
* **Tick:** one R3F `useFrame` per kart. That is **variable-step at the display rate** (requestAnimationFrame, usually 60 Hz on the TV).
  `delta = Math.min(rawDelta, MAX_DELTA)` with `MAX_DELTA = 1 / 30` (`MultiplayerKart.tsx:84,705`). There is no fixed timestep and no substepping, except the wall sweep (§7.7).
* **Integration:** speed is exponentially damped toward a target (`three/src/math/MathUtils.damp`:
  `damp(x, y, λ, dt) = lerp(x, y, 1 − e^(−λ·dt))`). Yaw is integrated with a **per-frame** step (not ×dt!). Position uses explicit
  Euler with the *new* speed and a lagged direction vector.
* **Frame-rate dependence (important).** Many constants are per *frame*, tuned at 60 fps: the turn step, steering assist,
  every collision push-out, the wall speed multipliers, and the probe intervals in frames. Two are frame-rate corrected: the kart bump `pow(0.8, dt·60)` and the wall sweep `exp(−8·dt)`.
  The quality tier (from a smoothed frame time) scales the turn step to compensate *partially* (§3.3). **The reference feel is
  60 fps "ultra" tier.**
* **Per-kart quality tier** (`performanceQuality.ts:150-247`): `smoothFrameMs = prev*0.92 + next*0.08`, re-evaluated every 30 frames
  (`MultiplayerKart.tsx:682-702`). Tiers by average frame ms: ≤18 ultra, ≤24 high, ≤34 balanced, else performance. With ≥4 viewports, ultra/high drops to balanced.
  With 3 viewports, ultra drops to high (`performanceQuality.ts:225-247`).

---

## 2. Speed model ("drivetrain")

### 2.1 Constants — `app/src/legacy/Experience/kart/engine/constants.ts:2-20`
```ts
speed: { min: -15, max: 40, default: 0 },          // reverse target, top speed (SU)
acceleration: { rate: 0.12, deceleration: 0.20, curve: 0.85 },  // UNUSED by MultiplayerKart (legacy single-player only)
response: { throttle: 1.8, brake: 6, coast: 1.1 }, // damp lambdas, 1/s
```

### 2.2 Per frame (`MultiplayerKart.tsx:1155-1181`)
```ts
powerupBoost   = boostMultiplier > 1 ? 40 * (boostMultiplier - 1) : 0;      // :1108  (multipliers < 1 are IGNORED, see bugs)
terrainMult    = isOnDirt ? 0.75 : 1;                                        // :1110
maxSpeed = (40 + (turbo > 0 ? 40 : 0) + powerupBoost + rubberBandBoost + slipstreamBoost) * terrainMult;  // :1155-1161
targetSpeed = backward ? -15 : forward ? maxSpeed : 0;                       // kartPedalTargetSpeed, constants.ts:75-77
lambda = backward ? 6 : forward ? 1.8 : 1.1;                                 // :1170-1174
speed  = damp(speed, targetSpeed, lambda, dt);                               // :1175
if (speed < 20) { driftDirection = 0; driftPower = 0; }                      // :1177-1180
turbo -= dt;                                                                 // :1181
```
* **Brake wins** when both pedals are held. Brake and reverse are the same pedal: target −15 at λ 6.
* Envelope at 60 fps (tested in `games/kart/pedalResponse.test.ts`). The first frame moves 40·(1−e^(−1.8/60)) = **1.18 SU/s**. Throttle reaches
  63 % at 0.556 s and **90 % at 1.279 s**. The brake reaches 63 % in 0.167 s. Coast reaches 63 % in 0.909 s.
* There is no mass, no tyre model and no engine torque. "Acceleration" is entirely the exponential approach.
* `isBoosting = maxSpeed > 40` (`MultiplayerKart.tsx:1614`), so rubber-band and slipstream also light the boost flag.

### 2.3 What can add to `maxSpeed` (all additive SU on top of 40, then × terrain)
| Source | Add | Duration | Ref |
|---|---|---|---|
| Drift mini-turbo (`turbo > 0`) | **+40** plus `40·(mult−1)` from its multiplier | tier table §4.3 | `MultiplayerKart.tsx:1157,1224-1232` |
| Boost pad | mult `1.8` → +32 | `1.5` s; same-pad cooldown `2.0` s | `BoostPads.jsx:19-22,290-298` |
| Rocket start | perfect `1.6`/1.75 s, clean `1.35`/1.25 s, late `1.25`/1.1 s, bogged `0.82`/0.75 s (no effect: mult<1 ignored) | — | `performanceQuality.ts:701-735` |
| Mushroom / Triple / Star / Bullet Bill | mult 2/2 s, 2/5 s, 1.5/8 s, 2.5/6 s | — | `useItemAction.ts:53-116` (detail in spec 02) |
| Rubber band | ≤ **+2** SU | continuous | §2.4 |
| Slipstream | 2.9–~20 SU, only when `speed ≥ 70` | continuous | §2.5 |

The boost timer: `boostTimeRemaining -= dt` every frame, written to the store. At ≤ 0 it resets `{boostMultiplier: 1, activeBoost: null}`
(`MultiplayerKart.tsx:1092-1106`). A new boost **overwrites** the multiplier and time (no stacking and no max), but `turbo` (+40)
runs independently.

Resulting peak target speeds (road): mini 87.2, super 94, clean super 96.8, ultra 102, clean ultra 104.8, apex 108,
pad 72, mushroom 80, star 60, bullet 100. Because of λ = 1.8, the kart never reaches them during a short boost. For example, a 0.85 s mini from 40 peaks at
≈ 40 + 47.2·(1−e^(−1.53)) ≈ 76.9.

### 2.4 Rubber banding (`MultiplayerKart.tsx:1112-1129`)
Only when `playerCount > 1 && status === 'racing'`:
```ts
positionRatio  = (racePos - 1) / (playerCount - 1);                 // 0 = leader, 1 = last
score(id)      = lapCount * 100 + progressPercent;                  // progressPercent 0..99.9 within a lap
leaderGapRatio = min(1, max(0, leaderScore - myScore) / 140);
lobbyScale     = playerCount <= 2 ? 0.4 : playerCount === 3 ? 0.7 : 1;
rubberBandBoost = 40 * positionRatio * leaderGapRatio * 0.05 * lobbyScale;   // max +2 SU
```
`getPlayerRacePosition` sorts by laps, then current checkpoint index, then progress % (`store.ts:1982-2001`).
There is no slowdown for the leader.

### 2.5 Slipstream (`MultiplayerKart.tsx:1131-1153`, `performanceQuality.ts:867-983`)
Target = `getNearestPlayerAhead` (nearest by distance among karts ahead on laps/progress, `store.ts:1943-1980`).
`alignmentDot` = the kart's facing (`−sinθ, −cosθ`) · the unit vector to the rival.
```ts
null if dist < 4.5 || dist > 28 || alignmentDot < 0.72 || speed < 70
isBumpDraft   = dist < 6 && align >= 0.96 && speed >= 105;   if (dist < 6 && !bump) null
distanceScore = clamp01((28 - dist) / 22); alignmentScore = clamp01((align - 0.72) / 0.28); speedScore = clamp01((speed - 70) / 70)
isDraftPocket = !bump && dist <= 8.5 && align >= 0.97 && speed >= 130
isStrongDraft = dist <= 14 && align >= 0.9 && speed >= 110
isComeback    = !bump && !pocket && !strong && leaderGapRatio >= 0.65
boostSpeed = pocket   ? 11.5 + d*3.5 + a*1.3 + s*0.8
           : bump     ?  9.1 + d*2.2 + a*1.1 + s*0.45
           : strong   ?  9.2 + d*3.5 + a*1.3 + s*0.5
           : comeback ?  4.7 + d*4.8 + a*2.4 + s*0.8 + gap*1.45
           :             2.9 + d*5   + a*3   + s*0.5            // each .toFixed(1)
charge: no slipstream → charge - dt*1.15 (≥0); else charge + dt*rate (≤1), rate bump 1.35, pocket 1.15, strong 0.92, comeback/slingshot 1.2, light 0.55   (each .toFixed(3))
charged: charge < 0.65 → boostSpeed; else boostSpeed * (1 + ((charge-0.65)/0.35)*0.35); charge >= 0.95 → 'slingshot'
```
**Effectively dead:** it needs `speed ≥ 70`, while top speed is 40. It only fires while boosted (see bugs).

### 2.6 Engine-note drivetrain (audio only) — `app/src/games/kart/drivetrain.ts`
This does **not** feed physics (`drivetrain.ts:1-12`). It is a fake gearbox for the synth. Port it to MetaSounds as-is, with input = `speed` SU.
```ts
IDLE_RPM = 950; UPSHIFT_RPM = 7600; LIMITER_RPM = 8600; DOWNSHIFT_RPM = 3500; KICKDOWN_RPM = 5200; LAUNCH_RPM = 3300;   // :40-46
GEAR_TOP_MPH = [19.2, 31.7, 42.5, 54.4, 70, 90]; RPM_PER_MPH[g] = 8600 / GEAR_TOP_MPH[g];                            // :53-54
SHIFT_GAP_S = 0.13; SHIFT_LOCKOUT_S = 0.25; LIMITER_CUT_S = 0.055;                                                       // :55-57
approach(v, t, tau, dt) = t + (v - t) * exp(-dt / max(1e-4, tau));  dt clamped to [0, 0.1]
throttle inference: accelEma = approach(accelEma, (|v|-|vPrev|)/dt, 0.08); accelerating → on; ema>1.5 → on; ema<-2.5 or (accelerating===false && |v|<0.5) → off  // :102-109
clutchIn = neutral || |v| < 0.6 → gear 0; v<0 → -1; else ≥1
upshift: throttle && rpm >= 7600 && gear < 6 (gap 0.13, lockout 0.25, 35% chance of a pop at +0.02 s)
downshift: gear>1 && wheelRpm(gear) < (throttle ? 5200 : 3500) && wheelRpm(gear-1) < 7000
rpm target: neutral → pedal ? 9000 : 950 (tau 0.22/0.3); in gear → max(950, wheel + slip), slip = gear1&&pedal ? 3300*max(0,1-wheel/3300) : 0, tau shiftGap ? 0.05 : 0.045
limiter: rpm ≥ 8600 → clamp, if pedal → cut 0.055 s (target ≤ 7900, tau 0.03)
lift-off pops: wasThrottle && !throttle && rpm > 4500 → pop now + 1..3 pops at 0.07 + rng*0.32 s; a 'backfire' event fires if rpm > 2400
load target: gap/limiter -0.35, pedal 1, rpm > 1300 → -1, else 0.1; tau 0.05 rising / 0.09 falling
rng = mulberry32(seed)
```

---

## 3. Steering

### 3.1 Phone → host pipeline
1. **Phone** (`app/src/lib/motion.ts:372-482`, `app/src/lib/wheel.ts:52-60`; iOS `ios/Movecade/Motion/MotionService.swift:176-196,457-471`):
   the kart manifest sets `steering: 'wheel'` and `sendHz: 30` (`games/kart/index.ts:18-19`). The wheel angle is the rotation of
   gravity inside the **screen plane**: `angle = atan2(gRight, −gUp)` in degrees (gravity pointing down). It is `null` when
   `hypot(gUp, gRight) ≤ WHEEL_MIN_IN_PLANE = 1.5` (m/s²; a flat phone), in which case the last value is held. Positive = clockwise = steer right.
   The phone subtracts the calibrated neutral: `gamma_out = normaliseAngle(wheel − calibration.gamma)`. It sends a 30-byte binary
   sensor frame at 30 Hz, and the server appends the seat index (31 bytes) (`games/legacyBridge.ts:274-312`).
2. **Host bridge** (`legacy/Experience/kart/engine/MultiplayerMobileControlBridge.tsx:228-291`), on every received frame:
   ```ts
   d = gamma; if (d > 180) d -= 360; if (d < -180) d += 360;
   raw = clamp(d / 90, -1, 1);                         // MAX_STEER_ANGLE = 90   (utils/kartRaceConstants.ts:14)
   dz  = 2 / 90;                                        // STEER_DEADZONE = 2°    (kartRaceConstants.ts:17)
   raw = |raw| < dz ? 0 : sign(raw) * (|raw| - dz) / (1 - dz);   // rescaled deadzone, linear curve
   raw = clamp(raw * steeringSensitivity, -1, 1);       // sensitivity default 1.0, min 0.1 (store/kartRaceStore.ts:91,132-135)
   raw *= -1;                                           // INVERT_STEERING = true (kartRaceConstants.ts:23) → +ve = LEFT
   steeringInput = raw;                                 // STEER_SMOOTHING = 1.0 → no smoothing (kartRaceConstants.ts:20)
   ```
   The curve is linear, with no expo. Steering is a **30 Hz step function** on the host; the sim reads the latest value each frame.
   Calibration constants (Swift parity): hold `1.25` s, tolerance `15`°, `25` samples at 20 Hz (`kartRaceConstants.ts:30-36`).
   `USE_PITCH_THROTTLE: false`, so the pitch angles are unused.
3. **Buttons**: GAS → `isAccelerating`, BRAKE → `isReversing`, DRIFT → `isDrifting`, ITEM → `useItem`. They are sent as `kart-input`
   game events only on change (`KartController.tsx:81-106`). They are released on `visibilitychange`/`pagehide`/orientation gate
   (`KartController.tsx:57-79`). The host applies them through `syncKartInput` (`legacyBridge.ts:319-333`). The `kart-input`
   payload may also carry `steeringInput` directly (bots use this; it is clamped ±1). The old 50 ms rate-limit path in the
   mobile bridge (`MultiplayerMobileControlBridge.tsx:61-202`) is not on the v2 button path.
4. **Other inputs on the host** (`MultiplayerKart.tsx:908-943`). Keyboard (`MultiplayerApp.tsx:43-50`): forward Up/W, backward
   Down/S, left Left/A, right Right/D, jump Space, item E/LeftShift. Gamepad 0: button0 gas, button1 brake,
   `steeringX ||= −axes[0]`, button5/7 drift. Keyboard left/right add ±1 *on top of* `steeringX`.

### 3.2 Yaw integration (`MultiplayerKart.tsx:1188-1199`)
```ts
inputTurn = (steeringX + (left - right) + driftDirection) * 0.018;          // rad per FRAME per unit input
yaw += (inputTurn * Math.min(speed, 40)) / 40 * turnStepScale;              // TURN_SPEED_REFERENCE = 40 (:111); NOT × dt
```
* Instant: there is no yaw inertia or smoothing ("Direct assignment", `:1190-1199`).
* The speed factor is `min(speed, 40)/40`. Boosts do not add turn rate. At speed 0 there is no turning. **Reverse (negative
  speed) inverts the steering** automatically.
* At 60 fps ultra, full lock with speed ≥ 40 gives **0.018 rad/frame = 1.08 rad/s (61.9°/s)**, a turn radius ≈ 40/1.08 = **37 m**.
  In a drift with full in-turn steer (2.4 units) it is 2.592 rad/s (R ≈ 15 m). Neutral drift (1.4) is 1.512 rad/s. Full counter-steer (0.4) is 0.432 rad/s.

### 3.3 Quality-tier turn compensation — `performanceQuality.ts:540-564`
| tier | turnStepScale | steeringAssistScale |
|---|---|---|
| ultra | 1 | 1 |
| high | 1.08 | 0.96 |
| balanced | 1.22 | 0.88 |
| performance | 1.35 | 0.78 |

At 30 fps "balanced", the kart turns 0.018·1.22·30 = 0.659 rad/s, which is **39 % slower than at 60 fps**. The UE port should target the 60 fps ultra
numbers (§12).

### 3.4 Heading lag ("slide") — `MultiplayerKart.tsx:1249-1270`
```ts
desired = (-sin(yaw), 0, -cos(yaw));
dir.lerp(desired, 12 * dt);          // smoothedDirectionRef; NOT normalised, NOT clamped (alpha 0.2 at 60 fps, 0.4 at 30 fps)
moveDir = speed < 0 ? -dir : dir;
```
Movement uses `dir`, so the velocity lags the nose. In steady state at 60 fps the lag is ≈ 4° at full lock and ≈ 10° in a full drift, and
|dir| shrinks to ≈ 0.982 in a full drift (≈ 1.8 % speed loss). **Replicate it exactly, unnormalised.**

### 3.5 Wall steering assist — see §7.4.

---

## 4. Hop, drift and mini-turbo

### 4.1 Hop (`MultiplayerKart.tsx:435-467,1202-1212`)
* Trigger: `jump` rising edge (`jump && !jumpIsHeld && !isJumping`). `jump` = phone DRIFT button, Space, or pad button 5/7.
* `jumpOffset` tween (gsap): `0 → 0.3` in `0.125` s `power2.out`, `yoyo`, `repeat: 1` (so 0.25 s up and down). `isJumping` clears on complete.
  It is **visual only** (it raises the wheels and body, §9). Physics y never changes. Sounds `kart.hop` / `kart.land`.
* On landing, if drifting, the rear wheel kick is: `backWheelOffset[left|right] → 0.4` in 0.3 s `power4.out`, then `→ 0` in 0.8 s `bounce.out`,
  after a 100 ms delay (`:446-464`).
* While `isJumping || jumpOffset > 0.05`, the heavy collision response is skipped (§7.6).

### 4.2 Drift
```ts
DRIFT_DIRECTIONS = { none: 0, left: 1.4, right: -1.4 };            // :76-80
on hop: driftDirection = (left || steeringX > 0.1) ? 1.4 : (right || steeringX < -0.1) ? -1.4 : 0;   // :1206-1211
while driftDirection != 0: driftPower = min(driftPower + dt, 10);  // seconds held (:1242-1244)
speed < 20 → driftDirection = 0, driftPower = 0 (every frame)       // :1177-1180, a drift cannot restart until a new press
```
The drift only adds ±1.4 to the steering sum (§3.2). There is no lateral physics. The side is locked at the press. Inside a drift the
effective input is `steeringX ± 1.4` ∈ [0.4, 2.4].

### 4.3 Release → mini-turbo (`MultiplayerKart.tsx:1214-1240`, `performanceQuality.ts:737-865`)
On any frame with `!jump`: `jumpIsHeld = false`. Then, **only if `turbo <= 0`**, the drift is evaluated with
`driftDurationMs = driftPower * 1000` and `steeringInput = steeringX + left − right`. Afterwards the drift always resets (`driftDirection = 0, driftPower = 0`).
```ts
base = null if driftPower < 0.35 || durationMs < 650
     : (driftPower >= 0.9  && ms >= 1800) ? ultra { mult 1.55, time 1.6 }
     : (driftPower >= 0.65 && ms >= 1150) ? super { mult 1.35, time 1.25 }
     :                                       mini  { mult 1.18, time 0.85 }
align = sign(steeringInput) * sign(driftDirection)
align < 0 (counter-steer at release) → downgrade: ultra→super(1.35/1.25, 'sloppy'), super→mini(1.18/0.85, 'sloppy'), mini stays
align > 0 && ultra && driftPower >= 0.96 && ms >= 2200 && 0.62 <= |steer| <= 0.9 → APEX { mult 1.7, time 1.88 }
align > 0 && grade != mini → 'clean': mult += 0.07, time += 0.13   (super 1.42/1.38, ultra 1.62/1.73)
align == 0 (wheel centred) → base unchanged
```
Because `driftPower` is seconds, the tiers by hold time are: **mini ≥ 0.65 s, super ≥ 1.15 s, ultra ≥ 1.8 s, apex ≥ 2.2 s**
(plus the steering window). On success: `turbo = time` (+40 SU while > 0), the store gets `boostMultiplier = mult`,
`boostTimeRemaining = time`, and `activeBoost = "DRIFT_[QUALITY_]GRADE"`. If there is no boost, `turbo = useGameStore.getState().boostPower || 0` (a **bug**, §13).

### 4.4 Visual drift levels (spark/glow colour only) — `constants.ts:30-72`
`level1 blue #00ffff ≥ 1 s (15 particles)`, `level2 #FFA22B ≥ 3 s (5)`, `level3 purple #b100ff ≥ 6 s (25)`, none `#ffffff`.
**These do not match the boost tiers** in §4.3. Sparks show blue at 1 s, when the kart is already super. Parity: keep both. Upgrade: align them.

### 4.5 Rocket start (`MultiplayerKart.tsx:850-898`, `performanceQuality.ts:701-735`)
During `countdown`, the time the gas (or keyboard forward) is first held is recorded, and releasing clears it. On the first `racing` frame,
`held = now − timestamp`: `< 500 ms` none; `500–799` clean 1.35/1.25 s; `800–1200` perfect 1.6/1.75 s; `1201–2000` late
1.25/1.1 s; `> 2000` bogged 0.82/0.75 s (no effect, multiplier < 1 is ignored). Countdown: `countdownSeconds: 5`, `countdownDuration: 6000`
ms (`store.ts:446-447`).

---

## 5. Surfaces (off-road)

* **Detection** (`MultiplayerKart.tsx:131-132,1588-1610`): every `ceil(10 × terrainIntervalScale)` frames (ultra 10, high 12, balanced
  16, performance 22), a ray from `(x, y+1, z)` straight down with `far = 3` against the kart collision meshes. The **first** hit with
  `|normal.y| ≥ 0.7` decides. It is dirt if `mesh.name.toLowerCase()` contains any of
  `['dirt', 'sand', 'grass', 'offroad', 'mud', 'gravel']`.
* **Effect:** `maxSpeed × 0.75` (`:1110`). This applies to the whole sum, boosts included. It also sends a phone haptic: `0.075` intensity every `200` ms while `|speed| > 5` (`:1552-1568`).
* **Which meshes count:** Violet: only `name="ground dirt"` (Object_11, `material_6` sand verge) (`models/violet-loop.jsx:115`). The hills/grass
  mesh is named `"ground"`, so it has **no** penalty. Harbor: `"ground-dirt-shoulder"` (`games/kart/tracks/HarborTrack.tsx:919`).
  Violet also has `name="ground speed"` (Object_27, `violet-loop.jsx:121`), and nothing reads "speed".
* `games/kart/scene/trackSurfaces.ts` and `scene/worldSurface.ts` are **render-only** (CC0 world-space detail textures over the
  Violet 8×8 palette GLB; surface sets asphalt/concrete/grass/sand/rock, tile sizes 4.5/2.6/4/3.5/7 m). They have no physics meaning.
  Spec 03 covers them.
* **UE:** use a `UPhysicalMaterial` per surface (`SurfaceType1 = Dirt`) and a down trace from the kart. Keep the 10-frame cadence for parity,
  or evaluate every tick (it makes no difference to the feel).

---

## 6. Boost pads — `legacy/Experience/kart/engine/BoostPads.jsx`, `games/kart/boostPad.ts`
```ts
BOOST_DURATION = 1.5; BOOST_MULTIPLIER = 1.8; COOLDOWN = 2.0 /* s, per pad per player */; TRIGGER_PADDING = 1.5;   // BoostPads.jsx:19-22
isInsideBoostPad(p, pad, pad=1.5): dx = p.x - pad.x; dz = p.z - pad.z; c = cos(rot), s = sin(rot);
  across = dx*c - dz*s; along = dx*s + dz*c; inside = |across| <= width/2 + 1.5 && |along| <= length/2 + 1.5   // boostPad.ts:2-13
```
It is checked every frame against the store position (`BoostPads.jsx:208-230`) and calls `syncKartBoost(id, {boostMultiplier: 1.8,
boostTimeRemaining: 1.5, activeBoost: 'BOOST_PAD', isBoosting: true})` (`:290-298`). The pad does **not** set `turbo`.
Pads: Violet `[205.34,-91.97] rot -0.37`, `[262.08,-36.32] -4.42`, `[242.92,27.15] -1.52`, `[110.18,121.33] -3.99`, all `8×4`
(`tracks/catalog.ts:42-47`). Harbor: `harborPoint(t)` at `t = 6.2/29, 14.4/29, 19.5/29`, `rotation = yaw`, `12×6` (`tracks/harbor.ts:52-55`).
**UE:** a `UBoxComponent` trigger (half-extents `(width/2+1.5, length/2+1.5)` m, rotated by −rot) with a per-player cooldown map.

---

## 7. Collisions

### 7.1 Collider set — `games/kart/kartCollisionMeshes.ts`
* Every scene mesh is a collider **except**: names matching `/wheel|body|booster|tire/` or
  `/start|finish|banner|line|flag|spawn|player|checkpoint|waypoint/` (`:20-21`); translucent materials with
  `opacity < 0.5` (`:36`); names containing `powerup` or starting `item-`; anything under a node with `userData.kartDecoration` or
  `userData.kartActor` (`:8-18`). Floors and walls share one list.
* BVH: `three-mesh-bvh` `new MeshBVH(geometry, { targetLeafSize: 10 })` plus `acceleratedRaycast` (`:52-53`; Violet also builds one in
  `violet-loop.jsx:57-71`). The list re-gathers on mount, at +`1000` ms, and whenever a track calls `markTrackCollidersReady()` (`tracks/trackColliders.ts`).
* **Wall vs floor:** a face is a wall if `|normal.y| < 0.7`; otherwise it is floor or ceiling (used by the rays and by the sweep).

### 7.2 Probes (`MultiplayerKart.tsx:470-497`)
`checkHorizontalCollisionFast(pos, dir, far)`: one ray from `(x, y + 0.5, z)` along the horizontal `dir`. It returns the first hit that is not a
non-collision object, has a face, and has `|face.normal.y| < 0.7`. **Note:** the ray height is `0.5` above the *logical* plane. On Violet that is 2.1 m above the drawn road.

### 7.3 Per-frame collision pipeline (only when not hopping; `MultiplayerKart.tsx:1280-1425`)
Constants (`:88-109`):
```ts
BARRIER_COLLISION_DISTANCE = 2.5; KART_COLLISION_DISTANCE = 1.8; KART_BUMP_FORCE = 0.5;
COLLISION_PUSH_STRENGTH = 0.8; COLLISION_SOFT_DISTANCE = 0.85; COLLISION_HARD_DISTANCE = 0.7; COLLISION_CRITICAL_DISTANCE = 0.4;
STEERING_ASSIST_STRENGTH = 0.01; MIN_COLLISION_SPEED_MULT = 0.3; BOUNCE_STRENGTH = 1.2;
CONTINUOUS_PROBE_DISTANCE = 1.5; NUM_RADIAL_PROBES = 4; EMERGENCY_PUSH_STRENGTH = 1.5;
CONTINUOUS_CHECK_INTERVAL = 6; FAST_CHECK_INTERVAL = 3;   // frames, × collisionIntervalScale (1 / 1.15 / 1.45 / 1.9), ceil
```
**Step 1: radial probe** (throttled). The interval is `wasNearWall (cached nearest < 0.85) ? 3 : 6` frames, scaled (`:1288-1303`).
Four rays at angles `i·π/2` → dirs `(sin, 0, cos)` = **world** +Z, +X, −Z, −X (not kart-relative), each `far 1.5` (`:611-652`):
```ts
d < 0.4 : critical++; m = (0.4 - d) * 1.5; push -= dir * m; total += m
d < 0.7 : m = (0.7 - d) * 0.8; push -= dir * m; total += m * 0.5
isCritical = criticalCount >= 2; nearest = min d (999 if none)
```
**Step 2: apply the cached radial result, EVERY frame** (it is reused until the next probe, `:1306-1320`):
`if critical → pos += push; speed *= 0.3; else if total > 0 → pos += push`.
**Step 3: barrier rays along `moveDir`** (every frame, `:515-600`): forward `far 2.5`; left `(-dz, 0, dx)` and right `(dz, 0, -dx)` `far 2.0`;
forward-left and forward-right (normalised `dir+side`) `far 2.25`.
```ts
leftMin = min(left.d, fwdLeft.d) ; rightMin = min(right.d, fwdRight.d)          // 999 if no hit
assist  = (leftMin<0.85 || rightMin<0.85) ? ((0.85-leftMin)/0.85·[leftMin<0.85] - (0.85-rightMin)/0.85·[rightMin<0.85]) * 0.01 : 0
pen     = max( fwd.d<0.7 ? 0.7-fwd.d : 0, leftMin<0.7 ? (0.7-leftMin)*0.5 : 0, rightMin<0.7 ? (0.7-rightMin)*0.5 : 0 )
normal  = forward hit face normal → transformDirection(object.matrixWorld)   (only if the forward ray hit)
```
**Step 3b: steering assist** (`:1330-1355`). This is a yaw nudge **added after** the normal yaw step:
```ts
yaw += assist * min(|speed|/40, 1) * (reversing ? -1 : 1) * (nearest < 0.85 ? 1.2 : 1.0)
           * (playerSteersAgainst ? 0.15 : 1.0) * steeringAssistScale;
playerSteersAgainst = (steeringX > 0.1 && assist > 0) || (steeringX < -0.1 && assist < 0)
```
**Step 4: directional push-out** (`:1359-1366`): if `pen > 0 && normal`: `pos += normalize(normal.xz) * pen * 0.8 * 60`.
This is per frame and not ×dt, and it can be up to 33.6 units in one frame (see bugs).
**Step 5: move** (`:1369-1425`), with `isNearWall = nearest < 0.7`:
```ts
if (blocked /*forward ray hit within 2.5*/ && |speed| > 0) {
  closeness = 1 - min(fwd.d / 2.5, 1);                               // 0.5 if no distance
  if (normal && |speed| > 30) pos += normal * (|speed| / 40) * 1.2;  // bounce, per frame
  speed *= isNearWall ? max(0.1, 0.3 - closeness*0.5) : max(0.3, 1 - closeness*0.6);   // per frame
  slide = normalize(moveDir - normal * dot(moveDir, normal));
  pos += slide * |speed| * (isNearWall ? 0.15 : 0.4) * dt * (reversing ? -1 : 1);       // NO forward motion in this branch
} else if (!isNearWall /* || nearest > 0.85 (unreachable) */) {
  pos += dir * speed * dt;
} else {
  pos += dir * speed * dt * min(1, nearest / 0.85);                  // "caution"
}
```
**Step 6: kart vs kart** (`:658-675,1428-1434`, `games/kart/kartContact.ts:2-12`):
```ts
kartContact: null if same id || |ay - by| > 2; d = hypot(dx, dz); null if d >= 1.8;
  n = d > 0.0001 ? (dx, dz)/d : (aId < bId ? (-1, 0) : (1, 0)); depth = 1.8 - d
pos += n * min(depth, 0.5 * dt * 60); speed *= 0.8 ** (dt * 60);     // one-sided: each kart pushes itself
```
**DEAD IN PRODUCTION:** `otherKarts` is always empty. `MultiplayerTrackScene.tsx:38,64-72,99` reads a `kartGroupRefs`
map that nothing ever fills. **Karts pass through each other today.** Spec it, but decide explicitly whether parity means "ghost karts".

### 7.4 Hop-time collision (`:1374-1382`)
While hopping: steps 1–4 are skipped. One forward ray `far 1.0`. If it hits at `d < 0.6`, then `speed *= 0.3` and the kart does not move. Otherwise
it moves normally.

### 7.5 Swept wall constraint — always on, after steps 1–6 (`MultiplayerKart.tsx:1440-1445`)
```ts
hitWall = ACTIVE_TRACK_ID === 'sunset-harbor' ? constrainHarborMovement(start, pos) : constrainMeshMovement(start, pos, pos.y, meshes);
if (hitWall) speed *= Math.exp(-8 * dt);
```
`start` is the position captured **before** step 1 (`:1246`), so the sweep covers the frame's whole displacement, including the push-outs.
* **Harbor (analytic)** — `games/kart/tracks/harborBoundary.ts`: `HARBOR_WALL_OFFSET = 16.3`, `HARBOR_KART_RADIUS = 1.15`
  (`:3-4`). There are two wall polylines at `harborPoint(i/870, ±16.3)`, `i ∈ [0, 870)` (`:21-26`). If the start is more than `16.3 + 1.15` from the
  centreline, it is pulled back to the `16.3 − 1.15` radius first. Then the movement is substepped at `ceil(len / 0.65)` steps. For each substep and **each** of the 1740 segments:
  find the closest point; if the distance is under 1.15, push along the segment normal, oriented to the side of the *previous substep* position, by
  `1.15 − (v·n)` (`:33-62`). Road width `HARBOR_ROAD_WIDTH = 28` (`harbor.ts:11`). The spline is a uniform Catmull-Rom over 29 control
  points (`harbor.ts:2-36`).
* **Mesh tracks (Violet)** — `games/kart/tracks/meshBoundary.ts`: `MESH_KART_RADIUS = 1.15`, `WALL_NORMAL_Y = 0.7`, `SUBSTEP =
  0.65` (`:27,33,36`). The kart is a **disc** on the ground plane. For each substep and each mesh, a BVH `shapecast` with a sphere (radius scaled into mesh
  space), centre `(x, y_logical, z)`. For each wall triangle, `closest = triangle.closestPointToPoint(center)`, the push is horizontal only,
  `(radius − d)/d` along `center − closest` (or the face normal if d ≈ 0) (`:94-135`). Write back only if something collided (`:142-173`).
  Caveat: the BVH bounds test is a 3D sphere at the *logical* y (0), radius 1.15, so wall geometry whose box lies entirely below y −1.15 is skipped.

### 7.6 Collision feedback (presentation and haptics, host only; `MultiplayerKart.tsx:1457-1569`)
* `isCollidingBarrier = blocked || pen > 0 || isCritical || total > 0 || isNearWall`.
* `intensity = min(1, pen*2 + (|speed|/40)*0.5 + (critical?1:0) + (isNearWall?0.3:0))`. On first contact: sound `playKartImpact(impactStrength(...))`,
  FX `wall` at `max(0.3, intensity)`, phone event `collision {intensity: max(0.3,i), collisionType: 'barrier', sustained: false}`.
  Sustained: every `150` ms, `max(0.2, intensity*0.6)`.
* Kart-kart (dead): `intensity = min(1, (|speed|/40)*0.8 + 0.2)`, sent to both players.

### 7.7 Item hits (detail in spec 02)
The item code writes `speed: 0` and `boostMultiplier: 0.3/0.5/0.2` to the store (`MultiplayerItems.tsx:160-211`,
`useItemAction.ts:88-97`). **Neither reaches the physics**: `speedRef` is local and never re-read from the store, and multipliers < 1 are
ignored (§2.2). In the current web build, **shells, bananas, lightning and bombs do not slow karts** (they only play FX/SFX and the
screen effect). Parity decision needed (see §13).

---

## 8. Recovery, respawn, wrong way and checkpoints (gravity and falls)

* **No gravity, no vertical physics, no jumps or ramps, no falling.** The physics y stays at the spawn y = 0 forever. Both tracks are flat
  (Harbor `beachY` is scenery, `tracks/HarborMotion.tsx:15-18`). A "fall" is only the off-track timer below.
* **Off-track** (`MultiplayerKart.tsx:862-880`, `trackRecovery.ts:35-93`): `KART_RECOVERY_DISTANCE = 64`. Recovery is needed if the distance to the nearest
  path waypoint is over 64, **or** the kart is ≥ 3 waypoints past the still-required checkpoint's nearest waypoint (and less than half a lap; the anti-shortcut on the
  figure-eight). The first frame plays `kart.fall`. After **2000 ms** it plays `kart.respawn` and teleports to the *checkpoint approach pose*: the waypoint before the
  checkpoint's nearest waypoint, yaw `atan2(p.x − next.x, p.z − next.z)`, with speed 0 and the drift reset (y is unchanged).
* **Stuck watchdog** (`recoveryWatchdogs.ts:24-38,70-81`, `MultiplayerKart.tsx:1046-1067`): `STUCK_ANCHOR_DISTANCE = 4`,
  `STUCK_ANCHOR_MS = 2500`, `WATCH_GAP_MS = 1000`. `powered = forward && !backward`. The anchor re-arms on ≥ 4 units of movement, when not
  powered, or when not stepped for over 1 s. Recovery happens after 2.5 s powered within 4 units → teleport to the next checkpoint's approach pose (or spawn).
* **Checkpoint watchdog** (`recoveryWatchdogs.ts:30,94-108`, `MultiplayerKart.tsx:1069-1089`): `CHECKPOINT_STALL_MS = 8000`.
  If the kart is powered for 8 s without its next checkpoint index changing → teleport to that checkpoint's approach pose.
* **Checkpoints** (`store.ts:328-345`, `hooks/useCheckpointDetection.ts`): 9 landmarks in order: finish, turn_1, outer_curve,
  north_hairpin_1, crossover, upper_loop, north_hairpin_2, s_curve, final_curve. `CHECKPOINT_RADIUS = 24` (2D distance). A checkpoint
  counts on entry (inside now, not inside last frame). The race starts with next = index 1. Crossing the finish when it is next completes a lap. `totalLaps: 3`.
  `progressPercent = ((prevIdx + (1 − min(dNext/segLen, 1))) / 9) * 100`, capped at 99.9. Violet landmarks: `(3,-30.32) (40,-140) (256,-142)
  (305,-88) (110,6) (261,33) (306,74) (156,81) (64,133)`. Harbor uses `harborPoint(HARBOR_SECTORS[i].t)` (`catalog.ts:59-69`).
  Violet path: 55 waypoints, ≈ 1460 units per lap. Harbor: 87 waypoints (≈ 18 m apart).
* **Wrong way** (`MultiplayerKart.tsx:1571-1586`): every 15 frames when `|speed| > 5`: target = waypoint `nearest+1`; wrong way if
  `dot(facing, toTarget/|toTarget|) < −0.5`.
* **Spawn grid** (`spawnGrid.ts:9-14`), the same for both tracks, yaw 0 (facing −Z):
  `(-3.06,0,-22.23) (2.96,0,-17.26) (-0.13,0,-15.05) (5.97,0,-9.67)`. Reset to it on `race.status → 'waiting'`.
* **Sim gating** (`MultiplayerKart.tsx:719-725,846-860`): paused → frozen; `waiting`/`intro` → no sim; `countdown` → only the rocket-start timer.

---

## 9. Visual kart (presentation only) — `legacy/Experience/kart/engine/models/Kart.jsx`

Hierarchy: `playerRef` (physics pose: x, z, yaw; y = 0) → `kartRef` (visual drift yaw, shrink scale `0.5` when shrunk,
`MultiplayerKart.tsx:1669,1675`) → `groupRef` (extra yaw) → `group position-y -0.5 rotation-y π` → `body` mesh (+ wheel probe anchors, grip rim,
booster, avatar, exhaust) + 4 wheels (front pair in `frontWheels` for steer).

* **Visual yaw layers**:
  * `kartRef.rotation.y = damp(·, angle·1.3 + driftDirection·0.1, 6, dt)`, where `angle` is the signed angle between `desired` and the lagged `dir` (`MultiplayerKart.tsx:1259-1263`).
  * `groupRef.rotation.y = damp(·, driftDirection·0.4 + bodyYaw, 4, dt)` (`Kart.jsx:499-504`).
  * At full drift that adds up to ≈ 0.14 + 0.56 = 0.7 rad (40°) of slip angle.
* **Steering visuals** (`Kart.jsx:194-196,202-242`): `yRot = damp(yRot, inputTurn, 14)`; front wheels `rotation.y = damp(·, yRot·30, 18)`;
  rim `= damp(·, clamp(yRot·64, ±1)·1.1, 20)` (`STEERING_INPUT_SCALE = 64`, `MAX_WHEEL_ROTATION = 1.1`, `games/avatar/drivingGrip.ts:101,107`);
  `speedFactor = min(1, speed/50)`; `bodyYaw = damp(·, yRot·6·speedFactor, 6)`; lean `= damp(·, −yRot·3·speedFactor, 5)`.
  Wheel spin `rotation.x += speed·0.6·dt`.
* **Visual suspension** (`Kart.jsx:244-401`): each wheel's anchor sits at `(x, 0, z)` of its slot on the body. The ray origin is the anchor's world position `+2`, straight down,
  `far 10`, `firstHitOnly`. It considers only meshes named ground/track/floor/road/dirt and skips barrier/wall/collision/fence (fallback: an upward
  face normal with `n.y > 0.9`). `targetY = hit.y + 0.78 + jumpOffset + backWheelOffset`. The wheel y follows with `damp` λ = 15 (|v| < 20), 30 (< 50), 50 (else). With no hit,
  it damps to the last safe y at λ 8. Body: `pitch = clamp((c.y+d.y−a.y−b.y)/2, ±0.25)`, `roll = clamp((b.y−a.y+d.y−c.y)/2, ±0.2)`, damped
  at λ 10/20/35 (same speed bands), `body.y = 0.65 + avg(wheel y) + jumpOffset·0.1`, `body.rotation.z = roll + lean`. Probes run every
  `wheelProbeStride` frames (ultra/high 1, balanced 2, performance 3). Result: the wheels sit on the road, and the body centre is about 0.93 m above the road.
* **Wheel slots, stock kart** (`games/kart/vehicles.ts:51-66`): wheels 0..3 = `[-0.7,-0.2,0.7] [0.7,-0.2,0.7] [-0.77,-0.137,-0.7] [0.77,-0.137,-0.7]`
  (0/1 front, steer). Seat `[0, 0.17 − 1.22·0.9, −0.15]`. Grip `d_wheel` at `[0,0.355,0.542]` rot `[-1.134,0,0]`. Booster at `[0,0.25,-0.55]` rot
  `[0.279,0,0]`. Exhaust L `[0.5,0.65,-1.5]`, R `[-0.5,0.65,-1.5]`. These are pre-`rotation-y π` coordinates (the model's +Z faces backward in the game).
* FX (sparks, glow, skate marks, trails, dust, smoke, boost flames) live in `Kart.jsx:573-751`, `misc/KartFxLayer.tsx` and
  `particles/**`. They are presentation (spec 03 for assets). Smoke emits while `speed > 15 && !isBoosting` (`Kart.jsx:485`).

### 9.1 Kart model asset
* **`app/public/models/kart.glb`** (911,904 B, **KHR_draco_mesh_compression**, 7 PNG textures). Nodes: `root` (scale 10, **ignored**:
  `Kart.jsx` renders `nodes.body.geometry` etc. directly at scale 1) → `body` → `d_wheel, booster, wheel_0..3`. There is also a `shape` (unused,
  commented out at `Kart.jsx:747`). Triangles: body 1287, d_wheel 182, booster 284, each wheel 440 → **≈ 3.5 k tris**.
  Body bounds `x ±0.976, y −0.40..0.44, z −1.11..1.76` units. Front wheel radius ≈ 0.289, rear ≈ 0.352. Materials: `m_Body` (baseColor + metallicRoughness
  textures, metallic 1, roughness 1) and `m_Tire` (baseColor + MR textures, metallic 0). There is a placeholder `app/public/models/movecade-kart.glb` (6.9 KB, 292 tris, not loaded).
* Cosmetic vehicles (same physics, `vehicles.ts:3-11`): `app/public/game-assets/vehicles/{moto,coupe,truck}.glb`, triangles 5332 / 9112 /
  14044 (`catalog.json`), node contract `seat, d_wheel|handlebar, wheel_*, trim` (built by `tools/avatar/build_vehicles.py`). The moto has only
  `wheel_0, wheel_2`; the empty slots copy their axle mate. **There are no per-kart or per-vehicle stats anywhere.** Every kart has identical handling.
* UE import: decompress Draco first (`npx @gltf-transform/cli decompress kart.glb kart_raw.glb`) unless your glTF importer reads
  KHR_draco. Import at 1 unit = 100 cm, drop the root ×10, and rotate so the model's −Z (game forward after the π flip) becomes UE +X. Import the
  wheels as separate meshes or sockets so the visual suspension and steer can drive them.

### 9.2 Paint — `games/kart/kartPaint.ts`, `games/kart/scene/kartMaterials.ts`
* v2 paint palette (`kartPaint.ts:10-17`, primaries from `games/avatar/appearanceColors.ts:43-60`):
  pink `#f0508e`/`#f8a8c8`, cyan `#5ec8e5`/`#b7edf5`, gold `#f7c948`/`#ffe49b`, violet `#a67cf0`/`#d9ccfa`, white `#f4f0ea`/`#b8bed0`,
  navy `#2a3558`/`#7180b4` (primary/secondary). The secondary only tints confetti and trails. Fallback for old rooms: legacy `KART_COLORS`
  (`customization/avatarData.ts:200-221`; default `'red' #E52521`).
* Paint material (`kartMaterials.ts:27-44`). Non-low tier: MeshPhysical clearcoat. `color = paint` (it **multiplies** the baseColor
  texture), `roughness 0.38`, `metalness 0.12` (both multiply the MR texture's G and B channels), `envMapIntensity 1.15`, `emissive = paint·0.045`,
  `emissiveIntensity 0.3`, `clearcoat 1`, `clearcoatRoughness 0.07`. Low tier: standard, `roughness 0.28`, `metalness min(0.18, orig)`,
  env 0.9, emissive intensity 0.45. The grip rim and booster share the body paint. Tyre (`:46-53`): `color #1b1b1d`, `roughness 0.9`, `metalness 0`, env 0.3.
* UE: one `M_KartPaint` (Clear Coat shading model) with a `PaintColor` vector parameter. BaseColor = Tex·PaintColor, Roughness = 0.38·MR.G,
  Metallic = 0.12·MR.B, ClearCoat 1, ClearCoatRoughness 0.07, Emissive = PaintColor·0.045·0.3. Use one MID per kart.

---

## 10. Camera

### 10.1 Which camera runs
* **One kart on screen** (`MultiplayerTrackScene.tsx:46,77`): `HostCameraFollow` **and** `KartRaceCamera` (follow mode, mounted in
  `MultiplayerApp.tsx:384`) both drive the default camera every frame (both use priority 0, KartRaceCamera first). The result is two stacked lerps. HostCameraFollow wins the
  final `lookAt` and FOV. `KartViewPost` renders it with post (`misc/KartViewPost.tsx:45-62`).
* **≥ 2 karts (bots included!)**: `SplitScreenRenderer` renders one viewport per kart, up to 4, each with its own `PerspectiveCamera(75, aspect,
  0.1, 2500)` with layer 1 enabled, its own composer, and render priority 2. Layouts (`SplitScreenRenderer.tsx:55-75`): 2 = top/bottom halves; 3 = top-left,
  top-right, bottom-centre (x 0.25, w 0.5); 4 = 2×2.
* All of them are skipped while `isCameraOverrideActive` (intro, static cams, results) or in promo mode.

### 10.2 Chase rig (follow mode)
`CAMERA_MODES.follow = { offset: {x:0, y:2.5, z:6}, lookAtOffset: {x:0, y:0.5, z:-8}, smoothing: 8, fov: 75 }` (`KartRaceCamera.tsx:45-54`).
Kart pose = store `position`/`rotation.y` (the physics pose, not the visual drift yaw).
```ts
// split-screen and KartRaceCamera (SplitScreenRenderer.tsx:~485-510, KartRaceCamera.tsx:356-414)
camTarget  = (kx + sinY*6, kartCameraY(ky, 2.5), kz + cosY*6)          // 6 behind (behind = +Z rotated)
lookTarget = (kx - sinY*8, kartCameraY(ky, 0.5), kz - cosY*8)
cam.pos.lerp(camTarget, 8*dt); look.lerp(lookTarget, 8*dt); cam.lookAt(look)   // alpha = 8*dt, NOT exponential, dt ≤ 1/30
fov = 75 (vertical)

// HostCameraFollow.tsx:66-102 (single kart)
chase = 5.4 + min(1.35, |v|*0.018);  ahead = 7.2 + min(2.2, |v|*0.045);
camTarget  = (kx + sinY*6,  kartCameraY(ky, 2.5), kz + cosY*chase)   // x uses 6, z uses chase: inconsistent, see bugs
lookTarget = (kx - sinY*8,  kartCameraY(ky, 0.5), kz - cosY*ahead)
fov = 72 + min(8, |v|*0.05)
kartCameraY(y, off) = y + off + (TRACK_GROUND_Y - (-1.605))        // games/kart/cameraGround.ts:13-24
```
**Relative to the road surface** (both tracks): eye **4.105** m above the road, **6** m behind, aimed at a point **8** m ahead and **2.105** m
above the road (≈ 8° down). UE (road Z = 0, cm): `Eye = KartXY − Fwd·600 + Z 410.5`, `Look = KartXY + Fwd·800 + Z 210.5`.
There is **no ground clipping or collision** for the camera (no spring-arm trace). The only "camera ground" is `kartCameraY`.

### 10.3 Shake, flash and FOV effects — `games/kart/scene/kartFxBus.ts`
```ts
KART_FX_PROFILES = { 'projectile-hit': {shake 0.34, flash 0.22, #fff1dc}, explosion: {0.55, 0.32, #ffb070},
                     spinout: {0.14, 0}, wall: {0.2, 0}, bump: {0.12, 0} }         // :31-37 (shake in metres)
scale = intensity === undefined ? 1 : clamp(intensity, 0.15, 1)
SHAKE_DECAY = 6.5; shake(t) = peak*exp(-6.5 t), 0 below 0.001;  FLASH_SECONDS = 0.28; flash(t) = peak*(1-t/0.28)^2
re-hit: newPeak = max(profile, remaining envelope);  global (no player) events shake every view at 0.5×
offset: x = (sin(t*47.3)*0.6 + sin(t*83.1+1.3)*0.4)*amp ; y = (sin(t*53.7+2.1)*0.6 + sin(t*91.9+0.4)*0.4)*amp*0.7 ;
        roll = (sin(t*39.1+0.7)*0.6 + sin(t*71.3+2.9)*0.4)*amp*0.06      // camera-right, camera-up, about the view axis
```
The shake is applied just before render and removed just after (`applyKartCameraKick`/`releaseKartCameraKick`), so it never accumulates into the follow.
Speed FOV and chase distance exist **only** in `HostCameraFollow` (single viewport). There is no speed effect in split-screen. The unused
`KartRaceCamera.shake()` gsap jitter is not wired up.

### 10.4 Other camera modes (debug/promo/results) — `KartRaceCamera.tsx:44-146`
aerial `{0,25,5}/{0,0,-5}` s4 fov60 (world-fixed); trackside `{12,3,0}/{0,0,-3}` s3 fov70; hood `{0,1.2,2.5}/{0,0.5,-10}` s12 fov90;
cinematic `{5,4,8}/{0,0,-4}` s4 fov65; orbit `{0,3,8}/{0,0,0}` s6 fov70. Static cams: gsap 0.8 s `power2.inOut`.
Results: `offset {6.4, 4.6, 8.4}` (world), `lookAt +{0,0.9,0}`, `fov 64`, `alpha = 1 − exp(−4.6·dt)` (instant with reduced motion), y ≥ 4.6.
Canvas: `camera far 3000`, `dpr [1, min(1.25, tier max)]` (`games/kart/KartCanvas.tsx:26-27`).

### 10.5 Post-processing — `games/kart/scene/kartPost.ts`, `scene/kartLook.ts`
Chain per viewport: `RenderPass → UnrealBloomPass(strength, radius, threshold) → KartFinishShader`, rendered to a HalfFloat target with MSAA
`quality.msaaSamples` (split-screen caps at 2). Finish shader (`kartPost.ts:53-111`): `exposure` → **Khronos PBR Neutral** tonemap
(`startCompression 0.76, desaturation 0.15`) → gamma 1/2.2 → contrast around 0.5 → `display*gain + lift*(1-display)` → saturation + vibrance
`(sat + vib*(1-smoothstep(0,0.6,chroma)))` → vignette `1 - smoothstep(0.32, 0.95, len(centred)*1.25)` mixed by `uVignette` → flash mix →
±0.5/255 dither → back to linear.
| look | exposure | contrast | sat | vib | lift | gain | bloom s/r/t | vignette | fog near/far |
|---|---|---|---|---|---|---|---|---|---|
| Violet day (`kartLook.ts:34-64`) | 1.02 | 1.08 | 1.1 | 0.22 | [0.008,0,0.03] | [1.03,1,0.96] | 0.32/0.35/1.0 | 0.32 | 260/2000 |
| Harbor sunset (`:66-96`) | 1.0 | 1.07 | 1.08 | 0.2 | [0.012,0.004,0.028] | [1.04,1,0.95] | 0.34/0.38/1.0 | 0.3 | 260/2000 |
| Night (`:98-115`) | Violet values with night sky; sun 1.1 | | | | | | | | 200/1500 |

The sun rig follows each viewport's kart, with the shadow target snapped to texels (`kartLook.ts:135-191`, `misc/KartLighting.tsx:56-72`).

---

## 11. Bots and autopilots

* **Lobby bots (the real opponents)** — `app/src/host/bots/kartPilot.ts`. These are headless clients that read their own kart pose from the host store and send
  `kart-input` (no sensor frames). They tick every `KART_TICK_MS = 100` and send only on change (steer Δ > 0.04, drift/gas change) or every `KART_REFRESH_MS
  = 1000`.
  ```ts
  KART_LOOKAHEAD = 24; KART_GAIN = 5; KART_ALIGNMENT_COST = 20; KART_DRIFT_TURN = 1.4; DRIFT_ENTER = 1.4; DRIFT_EXIT = 0.9;   // :20-32
  lookAhead: closest path segment by cost = dist + 20*(1 - dot(facing, segDir)); then walk 24 units forward along the polyline
  demand = clamp((atan2(-(tx-x), -(tz-z)) - yaw wrapped) * 5, ±2.4)
  wheel: if !drifting && |demand| > 1.4 → steer = side (±1), drift = side
         holding = drifting && demand*side >= 0.9 ? side : 0;  steer = clamp(demand - 1.4*holding, ±1)
  gas = racing; item: use 1200 + rand*2500 ms after one is held
  ```
  They always hold gas (bots are the "auto-throttle" the watchdog comments refer to). The bots' drift releases go through the same mini-turbo rules.
  Bots get **no** catch-up help other than the generic rubber band (§2.4).
* **Bot sensor-path fallback** (`host/bots/botClient.ts:721-766`, `'drive'` playbook for tests): sends `gamma = clamp(−error·90, ±90)` for 32
  frames at 30 ms, with `error = atan2(x − tx, z − tz) − yaw` toward waypoint `nearest+3`.
* **Bullet Bill autopilot** (in the sim, `MultiplayerKart.tsx:996-1042`): `forward = true`, no brake, no drift. Target = waypoint
  `nearest + 3`. `steer = clamp((atan2(−dx, −dz) − yaw) * 2.5, ±1)`. If a player ahead is within 30:
  `steer = steer·(1−b) + clamp(diff·3, ±1)·b`, `b = max(0, 1 − dist/30)`.
* **Promo AutoPilot** (`AutoPilot.tsx`, promo mode only): `steer = clamp(diff·(2 + aggressiveness) + noise·0.1, ±1)`, sent as
  **`−steer`**. That is the opposite sign of the Bullet Bill and kartPilot conventions (a probable bug; it only matters for promo capture).
* `games/kart/navigation.ts` is only a pose-reader seam (`publishKartPoses`/`readKartPose`) for bots. It holds no AI.

---

## 12. UE mapping (recommendation)

### 12.1 Choice: a custom kinematic arcade movement component, **not** Chaos Vehicles
**Recommend:** `AKartPawn : APawn` with a `USphereComponent` root (radius **115 cm**, matching `MESH/HARBOR_KART_RADIUS = 1.15`; used as the
query shape only, `SimulatePhysics = false`) and a skeletal or static kart mesh child. Add a `UKartMovementComponent : UPawnMovementComponent` that ports
§2–§8 line for line.

Why not Chaos Vehicles (`UChaosWheeledVehicleMovementComponent`):
* The web feel *is* the formulas. Speed is an exponential approach to a target (λ 1.8/6/1.1). Yaw is a direct per-frame step scaled by
  `min(v,40)/40`. A drift is +1.4 added to the steering sum. A boost is an additive top-speed term. A tyre, torque-curve and suspension model cannot reproduce
  this without fighting it, and tuning "until it feels similar" breaks the parity rule.
* The game has no vertical physics (flat logical plane), no mass and no kart-kart impulses (they are currently dead). Chaos adds non-determinism and cost
  for 4 karts plus split-screen with nothing to gain.
* `CharacterMovementComponent` is also a poor fit (walking/floor logic, its own accel/braking/friction). `UPawnMovementComponent` plus
  `SafeMoveUpdatedComponent`/sweeps gives full control.
* Later ("improve" phase): this component can grow real slopes and ground-follow, and Chaos can be used *only* for cosmetic suspension or debris. The UE **Mover**
  plugin is an option if networked prediction is ever needed. It is not needed now, because the UE app is the sole authoritative host, like the TV today.

### 12.2 Tick
* Run the sim at a **fixed 60 Hz** in `TickComponent` (TG_PrePhysics) with an accumulator. Clamp the frame delta to 1/30 before accumulating, cap
  the substeps (e.g. 4), and interpolate the visual transform between the last two sim states. Use `dt = 1/60` inside, and use **turnStepScale = 1,
  steeringAssistScale = 1** (ultra). This reproduces the reference 60 fps feel exactly, including every per-frame constant and the frame-count
  probe intervals (radial 3/6, terrain 10, wrong way 15). Do **not** port the quality-tier compensation for physics.
* Keep the sim state in legacy units (metres and SU) inside the component (`FKartSimState { FVector2D Pos; float Yaw /*three.js θ*/; float Speed; FVector2D Dir; float Turbo; float DriftDir; float DriftPower; ... }`).
  Convert to UE (×100, axis swap, yaw sign) only when writing the actor transform. All formulas in this doc can then be copy-ported.

### 12.3 Per-piece mapping
| Mechanic | UE implementation |
|---|---|
| Input | `UKartInputRouter` per kart. Phone frames: WebSocket (spec 04) → gamma → §3.1 mapping (deadzone 2/90, invert) → `SteeringX`. Buttons → bools. Local: Enhanced Input (W/S/A/D/Space/E, gamepad A/B/LS/RB/RT) with the same OR-combination as §3.1.4. Keep the steering a sample-and-hold of the latest 30 Hz value. |
| Speed / pedals | `Speed = Target + (Speed − Target)·exp(−λ·dt)` with §2.2 verbatim. |
| Yaw | `Yaw += (SteeringX + Keys + DriftDir)·0.018·min(Speed,40)/40` per 60 Hz step. |
| Heading lag | `Dir = Dir + (Desired − Dir)·(12·dt)`, unnormalised. Move with `Dir·Speed·dt`. |
| Hop | A curve asset (0→0.3 m in 0.125 s power2.out, mirrored) on the visual mesh only. `bIsHopping` gates the collisions as in §7.4. |
| Drift / turbo | §4 verbatim, including the "only if Turbo ≤ 0 at release" rule. Do **not** port the global `boostPower` fallback (§13). |
| Boost timer / multipliers | A per-kart `BoostMultiplier`/`BoostTimeRemaining` on the movement component (not a global store). |
| Rubber band / slipstream | A `UKartRaceSubsystem` gives race position, progress score and nearest-ahead. Port verbatim (slipstream stays dormant below 70 SU). |
| Off-road | A down trace (`ECC_Visibility` or a `KartGround` channel) every 10 sim steps → `PhysMat->SurfaceType == Dirt` → ×0.75. |
| Boost pads | `AKartBoostPad` with a `UBoxComponent` overlap (padded) and a per-player cooldown of 2 s. |
| Walls | **Prefer analytic 2D walls for both tracks.** Harbor: port `constrainHarborMovement` as-is (two offset polylines from the spline, 870 samples; use a spatial grid instead of testing 1740 segments). Violet: export the steep-face (|n.z|<0.7) wall edges of the GLB offline into 2D segment lists (spec 03 exporter), then run the *same* disc-vs-segment sweep (substep 0.65 m, radius 1.15 m, speed ×exp(−8·dt) on a hit). It is deterministic, cheap and identical to the web build. Fallback: substepped `SweepSingleByChannel` sphere (r 115 cm) against a `KartWall` channel with simple collision, resolving `bStartPenetrating` via the MTD (`PenetrationDepth`, `ImpactNormal`) projected onto XY. |
| Legacy ray system (§7.3) | Port it for parity: 5 `LineTraceSingleByChannel` along moveDir/sides/diagonals at +50 cm, and 4 world-axis radial traces every 3/6 steps, with the same thresholds, including the stale cached push. Put it behind `bLegacyWallProbes` so the upgrade phase can delete it and keep only the sweep. |
| Kart vs kart | Implement `kartContact` (1.8 m, |Δz| ≤ 2 m) but ship with `bKartKartCollision = false` for parity (it is dead on the web). Turn it on as the first upgrade. |
| Recovery / watchdogs / wrong way / checkpoints | `UKartRaceSubsystem` plus a component on the kart, with §8 constants. Teleport with `SetActorLocationAndRotation(..., ETeleportType::TeleportPhysics)` and reset the sim state. |
| Visual kart | 4 wheel line traces (+200 cm up, 1000 cm down), damped wheel Z, body pitch/roll/lean/yaw layers per §9 on child components. Drive it from the interpolated sim state. Pure cosmetics, on the game thread. |
| Haptics / FX | Events from the movement component (`OnBarrierHit(intensity, sustained)`, `OnTerrainRumble`) → the network layer and Niagara (spec 04/03). |

### 12.4 Camera in UE
* `AKartChaseCamera` (or a `UCameraComponent` on a separate actor per viewport). It is **not** a `USpringArmComponent` (it has no probe, and its lag
  differs). Each tick: compute `camTarget`/`lookTarget` per §10.2 from the **sim** pose (not the drift-yawed mesh), then
  `Pos = Lerp(Pos, Target, FMath::Min(1, 8·dt))` and the same for the look point, then rotation = `FRotationMatrix::MakeFromX(Look − Pos)`. Keep
  `dt ≤ 1/30`. For identical feel, run it at the same 60 Hz sim step or use `alpha = 8·dt` with the clamped frame dt (the web uses frame dt).
* Pick **one** canonical rig. The web has two that disagree (split-screen fixed 6 m and 75°; single-kart HostCameraFollow with speed chase and FOV). Recommend:
  split-screen values for every viewport count, plus the HostCameraFollow speed FOV `72 + min(8, |v|·0.05)` **only** when there is one viewport.
  Fix the x/z mismatch (use `chase` on both axes).
* FOV: store the vertical FOV and set `FieldOfView = 2·atan(tan(vFOV/2)·ViewportAspect)` each frame (or use project-wide
  `AspectRatioAxisConstraint = Maintain Y-Axis FOV`, then verify the effective vertical angle).
* Split-screen: one `ULocalPlayer` per kart (`UGameInstance::CreateLocalPlayer`, no input device; phones route input by player id). Use
  `UGameViewportClient` split-screen. 2P = `ESplitScreenType::TwoPlayer_Horizontal` (top/bottom). 3P needs a custom `SplitscreenInfo` layout
  (TL, TR, bottom-centre at half width; UE's stock 3P layouts differ). 4P = grid. As on the web, **every kart, bots included, gets a viewport**. Make
  bot viewports optional in the upgrade phase.
* Shake/flash: apply the §10.3 offsets in `APlayerCameraManager::UpdateViewTarget` after the follow (do not use the stock `UCameraShakeBase`
  oscillators; they do not match). Flash is a post-process material parameter.
* Post: a PostProcessVolume with Manual exposure (fixed EV), bloom intensity tuned to match strength 0.32 (Standard bloom, threshold ≈ 1.0), and the
  per-track grade plus vignette plus flash plus dither as a **post-process material at the "Replacing the Tonemapper" blendable location**, porting
  `KartFinishShader` (PBR Neutral) verbatim. UE's Filmic tonemapper will not match otherwise. Linear height-less fog: Exponential Height Fog cannot do linear
  260–2000 m; approximate it, or add a distance-fog term in the same PP material.

### 12.5 Suggested C++ skeleton
```cpp
USTRUCT() struct FKartTuning {   // all legacy units; DataAsset so designers can A/B later
  float MaxSpeed=40, ReverseSpeed=-15, LambdaThrottle=1.8f, LambdaBrake=6, LambdaCoast=1.1f;
  float TurnPerFrame=0.018f, TurnSpeedRef=40, DriftTurn=1.4f, DriftMinSpeed=20, DirLerp=12, TurboBonus=40, DirtMult=0.75f;
  float KartRadius=1.15f, SweepSubstep=0.65f, WallSpeedDecay=8; /* + §7.3 probe constants, §4.3 tiers, §8 watchdogs */ };
UCLASS() class UKartMovementComponent : public UPawnMovementComponent {
  FKartSimState Prev, Cur; float Accum=0; static constexpr float Step=1.f/60.f;
  void TickComponent(float Dt, ...) { Accum += FMath::Min(Dt, 1.f/30.f); int N=0;
    while (Accum >= Step && N++ < 4) { Prev = Cur; SimStep(Step); Accum -= Step; }
    ApplyVisual(FMath::Clamp(Accum/Step, 0.f, 1.f)); }
  void SimStep(float dt); // order exactly as §12.6
};
```

### 12.6 Exact sim-step order (`MultiplayerKart.tsx:681-1657`)
1. Pause → return. 2. `waiting`/`intro` → return. 3. `countdown` → track the gas-hold timestamp → return. 4. Off-track recovery check (2 s).
5. Rocket start on the first racing frame. 6. Gather input (§3.1). 7. Item edge. 8. Status-effect expiry. 9. Bullet Bill override.
10. Stuck watchdog, then checkpoint watchdog. 11. Boost timer. 12. Rubber band and slipstream. 13. `maxSpeed` → `targetSpeed` → speed damp.
14. `speed < 20` drift cancel; `turbo -= dt`. 15. Yaw step. 16. Hop or drift start; release → turbo; driftPower += dt.
17. `start = pos`; desired/dir lerp; visual yaw. 18. moveDir. 19. Collision steps 1–5 (or the hop variant). 20. Kart-kart. 21. Wall sweep from `start`.
22. Haptics/FX. 23. Wrong way (every 15). 24. Terrain probe (every 10). 25. Publish state. 26. Checkpoint test.

---

## 13. Bugs, oddities and QA notes (decide per item: keep for parity or fix)

**Physics bugs (verified by reading the code):**
1. **Global `boostPower` leak between karts.** Every `Kart.jsx` writes `boostPower = driftLevel.threshold / 2` into one shared store
   field each frame (`Kart.jsx:539-545`, `store.ts:1067-1092`). Every kart that is not drifting, has no turbo and has `jump` released sets `turbo = boostPower`
   (`MultiplayerKart.tsx:1235`). With 2+ karts, while any kart holds a drift ≥ 1 s, the others can get repeated `turbo` 0.5–3 s, which means **+40 SU top speed**.
   *Recommend: do not port* (keep the per-kart turbo only). Verify on the web with 2 bots before deciding.
2. **Slowdowns never reach the physics.** Multipliers < 1 are ignored (`MultiplayerKart.tsx:1108`), and `speed: 0` written to the store is never
   read back (`speedRef` is local). So spin-outs, slow, explode, stop, lightning and the bogged rocket start have **no handling effect**
   (`MultiplayerItems.tsx:172-208`, `useItemAction.ts:88-97`, `performanceQuality.ts:706-712`). *Recommend: fix in UE (apply `maxSpeed *= mult` for
   mult < 1 and honour speed resets), and coordinate with spec 02.* Strict parity would mean "items don't slow you".
3. **Kart-kart collision is dead** (`MultiplayerTrackScene.tsx:38,64-72`: `kartGroupRefs` is never filled). Karts ghost through each other. The
   `bump` FX, kart haptics and `kart.bump` audio never fire.
4. **Slipstream is unreachable** at normal speed (it needs ≥ 70 SU; top speed is 40). The thresholds look like they were written for a 0–150 speed scale.
5. **Drift boost inputs are in mixed units.** `driftPower` is seconds, but `getRaceDriftBoostFeedback` treats it as a 0–1 charge (`driftPower >= 0.9`,
   `0.96`), so those checks are redundant. Also the visual drift levels (1/3/6 s) do not match the boost tiers (0.65/1.15/1.8/2.2 s).
6. **Frame-rate-dependent handling.** The turn step, assist, push-outs and wall multipliers are per frame. The tier compensation is partial, so a 30 fps TV turns ~39 % slower
   than a 60 fps one, and on 120 Hz displays the karts turn **2×** faster (ultra tier, scale 1). Fixed 60 Hz in UE removes this.
7. **Stale radial push re-applied every frame.** The cached `pushDirection` (and `speed *= 0.3` when critical) is re-applied every frame until
   the next probe (3–12 frames), up to 6× overshoot (`MultiplayerKart.tsx:1306-1320`).
8. **Step 4 push-out is not ×dt** (`pen·0.8·60`, up to 33.6 units in one frame if the forward ray is within 0.7). The normal comes from the
   raycast face, which may be a back face on double-sided meshes. The later sweep keeps karts out of walls, but this can make jerky "teleport" bounces.
9. **Blocked branch has no forward motion.** Any wall within 2.5 m along `moveDir` stops progress and only slides (0.4/0.15·|v|·dt), with
   per-frame speed multipliers. This is a big part of the "sticky wall" feel. Keep it for parity; it is a prime upgrade target.
10. `else if (!isNearWall || nearest > SOFT)`: the second clause is unreachable (`MultiplayerKart.tsx:1416`).
11. Radial probes are world-axis aligned, not kart-relative (`:619-620`). Probe rays sit at logical y + 0.5, which is 2.1 m above Violet's road, so low walls
    are only caught by the mesh sweep. The sweep's 3D BVH bounds test skips wall geometry entirely below y −1.15.
12. Off-road detection only matches mesh *names*. Violet's grass hills are named `ground` and are full speed. Only the sand verge (`ground dirt`) slows you.
13. **Checkpoint watchdog teleports forward.** A powered kart that has not reached the next checkpoint in 8 s is placed just before it, which can be a
    shortcut for a slow kart (`recoveryWatchdogs.ts:94-108`). The off-track recovery also aims at the next checkpoint, not at where you left the track.
14. Speed units: labelled mph, used as units/s, displayed as km/h → "MPH" (25 "mph" at top speed).
15. **Camera:** in single-kart mode two components fight over the default camera (`KartRaceCamera` follow and `HostCameraFollow`).
    `HostCameraFollow` uses offset 6/8 on x but chase/lookAhead on z (`HostCameraFollow.tsx:78-89`), so the camera is skewed off the kart axis at
    diagonal headings. The lerp `alpha = 8·dt` is frame-rate dependent. Split-screen gets no speed FOV.
16. The promo `AutoPilot` sends `−steer`, the opposite of the convention used by Bullet Bill and kartPilot (`AutoPilot.tsx:155`).
17. `BoostPads.jsx` subscribes to the whole `karts` map (it re-renders on every kart write). This is a perf issue only.

**From `docs/audits/qa-2026-09-23/games.md`** (kart items; none are handling bugs, but they matter for the race layer):
* GAMES-01 (P0): results sort the highest time first, which crowns the slowest racer (`host/GameOverScreen.tsx:78`). This is for spec 04.
* GAMES-02 (P1): a DNF signs 0 ms, which tops the MIN leaderboard. This is for spec 04.
* "No time cap": `maxRaceTime 300000` is now enforced by `endRaceAtDeadline` (`store.ts:448`, sync tick 250 ms). Re-verify.
* `kart-drive:145` e2e: the spec never presses GAS. There is no auto-throttle for humans (the phone needs a held GAS; `practice/guides.ts:17`), so the
  watchdog comments about "auto-throttle" really only apply to bots.
* Not tested by QA: walls, items, lapping. Treat every collision number above as untested in the field.
