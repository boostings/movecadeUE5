# Samurai Slice: port spec

Source: movecade-v2 `app/src/games/samurai/` (paths relative to it). Parity first. Part of the multi-game port
([05-games-framework.md](05-games-framework.md)).

**Key finding:** live scoring does not use the gyro swing detector. `runtime.ts:511-518` discards
`detector.feed()` (`void swing`); it only feeds `presence.charging` for the camera. Cuts come from the rendered
katana's sweep (`bladeTrail.tsx`), gated by `bladeIntent`, calling `runtime.contact()` with a fixed gesture
`{direction: 'down', power: 0.25}` (`runtime.ts:610-615`).

## 1. Engine (`engine.ts`)

- Round 60000 ms (:9); countdown 3000; 3 lives; Focus: 3 cuts, 6000 ms, flight scale 0.65 (:10-13); 4 targets per
  lane max; 4 waves of 15 s, `waveNumber = min(4, floor(el/15000) + 1)` (display only, :130).
- `CUT_WINDOW {start .22, end .86, apex .54}` (:20), used only without a contact set (sims, bots).

| Variant | Score | Radius | Preferred | Colour |
|---|---|---|---|---|
| apple | 16 | .42 | left | #f0506e |
| melon | 24 | .55 | down | #4ade80 |
| lantern | 20 | .48 | right | #fbbf24 |
| bamboo | 22 | .40 | down | #34d399 |
| bomb | 0 | .45 | none | #1f2937 |
| scroll (bonus) | 40 | .44 | up | #fde68a |

- RNG mulberry32 (:135-143), exact uint32 maths. Seed `0x5a3c + matchNumber` (`runtime.ts:357`); restored round
  `0x5a3c + matchNumber + round(elapsedMs)` (`runtime.ts:222`).
- `createMatch` (:150): lane = array index; first spawn cooldown `400 + index*220` ms.
- `stepMatch` (:280): (1) clamp dt [0,100]; (2) countdown subtract, at ≤ 0 → playing and the **full dt** goes to
  elapsed; (3) elapsed += dt, remaining -= dt; (4) effect TTLs; (5) focus timers; (6) targets in reverse:
  `progress += speed*dt*(fruit or bonus with owner in Focus ? .65 : 1)`, removed at ≥ 1 — a fallen bomb is dodged; a
  fallen fruit zeroes `focusCharge`, and with combo > 0 pushes `miss` "{combo}x lost" and zeroes combo (**no life
  lost**); (7) each live player: cooldown -= dt; at ≤ 0: ≥ 4 targets → 200; else `spawnTarget` → interval on
  success, 200 on failure; (8) ends at time up or all eliminated; `finish` clears targets; winners tie at max (:268).
- Spawn interval (:183): `(1150 - p*630)*(0.75 + rng()*0.5)`, `p = min(1, elapsed/60000)`.
- `pickVariant` (:189): `roll = rng()`, `bombChance = .12 + .12p`; bomb if `roll < bombChance` and elapsed ≥ 5000;
  else scroll if `roll < bombChance + .08` (before 5 s rolls under .20 are scrolls); else `FRUITS[floor(rng()*4)]`.
- `spawnTarget` (:201): `pair = elapsed ≥ 15000 && launches % 4 == 3` (waits for an empty lane; apple/melon by
  `floor(launches/4) % 2`; no `pickVariant`). `flightMs = 2800 - 700p + rng()*250`, `speed = 1/flightMs`. Preferred
  lateral `[0, -.56, .56, 0][launches % 4]`; candidates `[preferred, -.72, .72, 0]`; valid if for every lane target
  either (same bomb-ness and its progress > .30) or `|Δlateral|*1.1 ≥ (r1 + r2)*1.15 + .10`; none fits → fail (no
  more rng). Pair at ±.60. Per target `spin = (rng()*2 - 1)*.85`. `launches++`. **Draw order:** variant roll(s) →
  flight → spin(s) → interval on success.
- Effects (:249): slash 260 ms, combo 1200, others 800; ids share `nextId` with targets.
- `cutQuality(p) = max(0, 1 - |p - .54|/.32)` (:340).
- `resolveSwing` (:349): needs playing and a live player; pushes `slash`. Cut set = the player's targets in
  `contactIds` (else the window). **None** → combo 0, focusCharge 0, misses++, "{n}x broken" (lost combo ≥ 2) or
  "Missed". **Any bomb** → combo, focusCharge, focusRemaining 0; `bombs += count`; `lives = max(0, lives - 1)` (one
  life however many bombs); eliminated at 0; fruit in the swing voided and removed. **Hits:** per target
  `base += score + round(q*10)`, direction bonus `round(score*.25)` when it matches; `powerBonus = round(power*8)*n`;
  `multi = (n-1)*12`; `mult = 1 + min(12, combo)*0.0833` (12 → 1.9996); `delta = round((base + dir + power +
  multi)*mult)*(inFocus ? 2 : 1)` with the combo before `combo += n`. Perfect = min quality ≥ .85. A scroll gives +1
  life (max 3). Not in Focus: `focusCharge = perfect ? charge + n : 0`; ≥ 3 starts Focus (6000 ms), charge reset.
  Effect `combo` if Focus started, n ≥ 3 or combo ≥ 6, else `hit`. Label priority: `Focus! Double points` >
  `+1 life` > `Focus · +X` > `N cut! +X` > `Perfect!` > `+X`.
- Live golden values (down, power .25): apple at the apex, combo 0 → 16 + 10 + 0 + 2 = 28; melon → 24 + 10 + 6 + 2
  = 42; perfect needs `|p - .54| ≤ .048`.

## 2. Lanes and runtime

Lane span = max lane + 1; lane x `(i - (span-1)/2)*3.6` (`SamuraiScene.tsx:45`). Simultaneous, own lane and targets.
Departures remove the player's targets/effects, never renumber (`runtime.ts:192`). Frames route `seat-1 →
playerId`. Clock: wall-clock deltas, ≤ 100 ms steps, ≤ 1000 ms catch-up, per frame plus a 50 ms heartbeat
(`runtime.ts:577`); nothing during the intro or a pause. HUD 100 ms; scores 1000 ms (changed); `completeGame` once.
Snapshot v1 `v,g,m,ph,el,cd,tr,wi,p[i,n,l,s,c,bc,fc,fr,ln,h,ms,bm,lv,e]` (no targets/effects; hydrate clamps
`fc ≤ 2`, `fr ≤ 6000`; refused after any hit or bomb on this host).

## 3. Motion pipeline (phone → blade → cut)

Phone `sendHz` 100. Frames: alpha/beta/gamma (deg, calibrated), gyro deg/s, gravity-free accel m/s², quaternion,
dtMs, flags, screen orientation.

1. **Blade pose (`presence.ts`):** portrait pitch β yaw γ; landscapeLeft pitch -γ yaw β; upside-down pitch -β yaw
   -γ; landscapeRight pitch γ yaw -β; roll α. Wrap ±180, clamp pitch ±58°, yaw ±34°, roll ±64°, to radians; stale
   after 360 ms. Drives only the avatar: `swing = min(1, |pose|*1.1)`, `drive = .25 + .75 swing`, lateral shuffle by
   yaw sign with a .08 rad dead-band (`SamuraiSwordsman.tsx:202-207`).
2. **Katana (`SamuraiSwordsman.tsx:247-259`):** world rotation `basis * frameQuat * basis⁻¹` (identity basis,
   calibration forward (0,0,-1)). On the hand bone of the shared avatar (scale .64 at `(laneX, 0, 1.15)`, seed lane +
   59). Grip (`handTarget.ts:194`): grip -8°, elbowFollow .7, reachScale 1.15, reachClamp .2, velocity leak 7.5/s,
   position leak 5/s, idle (.1, -.23, -.28), response 12/s, reach cap .65. Body follow shuffle, 2 steps, strideX .13,
   strideZ -.09 (`bodyFollow.ts:166`).
3. **Blade markers:** base (0, .095, 0), tip (.1, 1.85, 0) prop-local; ×.64 ≈ 1.12 m.
4. **Intent gate (`bladeIntent.ts`, fed from `bladeTrail.tsx:186-191`):** once per render frame with a new frame,
   playing, and a trail sample; dt = host time since the last feed. `rate = |gyro|`, `acc = |accel|`; dt outside
   (0, 0.1] or non-finite → reset, false. rate < 45 and acc < 0.8: quiet += dt, reset at ≥ 0.08 s, return active.
   Else quiet 0; not started and rate < 90 and acc < 2.5 → false. Accumulate duration, `travel += rate*dt`,
   `impulse += max(0, acc - .8)*dt`; duration > 0.55 → reset, false. Active once duration ≥ 0.08 and (travel ≥ 18
   or impulse ≥ 0.45); stays active until quiet or timeout.
5. **Contact (`bladeContact.ts`):** swept quad prev (A base, B tip) → current (C, D); rejected if
   `max(|A-C|, |B-D|) < 0.0005`; hit if the target centre is within `radius + 0.02` of triangle ABD or ADC. Target
   centre `((lane - (laneCount-1)/2)*3.6 + lateral*1.1, targetHeight(p), 0.55)`, radius `target.radius*0.75`.
   One `resolveSwing` per render frame per lane with hits (a swing hitting on 3 frames is 3 swings).
6. **Trail:** clears on > 124 ms since the last sample, a base/tip jump > 2.4, version going back, > 140 ms stale, or
   hit-stop. Ribbon 12 samples, 100 ms fade; inner (.68, .78, .9) α×.3, outer (.95, .98, 1) α×.92, highlight .24.

**Swing detector (`swing.ts`), feedback and camera only:** onset 230, commit 420, release `max(peak*.35, 150)`,
45-520 ms, cooldown 240, rate ref 900, accel ref 18, accel weight .35, axis dominance 1.25, smoothing .45,
orientation confidence ceiling .7. Clock += `min(dt,250)` or 16; rate rotated to screen space unless compensated or
portrait; `env += (|rate| - env)*.45`. Direction: |x| ≥ |z| → x < 0 down else up; else z > 0 left else right.
`power = clamp(.65 rate + .35 accel)`; `confidence = .35 + .4 rate + .25 clamp((dominance - 1)/.75)` (+.1 if accel
power > .25). Without gyro: Δβ, Δγ, Δα over dt.

## 4. Presentation

- `targetHeight(p) = -1.6 + 14p - 13p²` (apex ≈ 2.17 at .538); z .55; visual scale .75. Cut pieces 650 ms, fade
  180 ms; `origin ± normal*1.4t + y(.7t - 1.6t²)`; 32 slots.
- Target pool (`SamuraiScene.tsx:409`): 20 slots; `rotY += spin*1.8 dt`, `rotX += spin*1.08 dt`. Bomb telegraph:
  barred octagon (ring .98-1.16, 8 segments, 1.95×.16 bar at -45°) `#d66d50`, pulse `1.16 + .035 sin(7t + id)`.
  Halo for bonus, or non-bomb with q ≥ .85 or in Focus: `#ffd477` (Focus/bonus) or `#fff5d9`, opacity .52 in Focus
  else .32. Launch ring while p < .23. Shadow `max(.08, .25 - .06y)`.
- Impact (`impact.ts`): hit-stop 78 ms (bomb 84; visuals freeze, sim runs); camera punch .16 (bomb .22) ×
  (.72 + .28i) × sign (always -1 live); shake decays `e^(-9dt)`, punch `e^(-14dt)`.
- Halves (`targetClipping.ts`, `cutEffects.ts`): every mesh split at build on local x = 0 (tolerance 1e-7), caps
  stitched and triangulated; hollow parts (lantern, bomb) get a .025 rim wall; apple/melon caps get an inset flesh
  copy (.97/.90) in `#f4dfb5` / `#dc403d` and seeds (5 apple, 12 black melon). Halves move ±normal (local +X), spin
  about local Z ±2 rad/s. Debris 10 per target: `angle = id*.73 + i*2.399963`, `speed = .8 + (i % 4)*.24`,
  `y += .6t - 1.6t²`. Bomb smoke 4 puffs.
- `sliceVfx.ts` data table (:64-174) verbatim. Bomb shake .55, flash `#ff8c42` .3 / decay .26; hit shake .38
  perfect, .32 combo, .16 else; whiff, fallen (p ≥ .85 when it vanished), lead change, elimination (shake .8).

## 5. Geometry and materials

Fruit (`fruitGeometry.ts`) normalised to unit radius, rendered at `radius*.75`: apple lathe (11 control points,
48 samples, 32 segments, 5 lobes ±3%) with stem and leaf; melon sphere .82 squashed .88; lantern sphere .66 ×1.16 y,
9 ribs, caps; bamboo 3 hollow tubes, nodes, leaves; bomb sphere .69, seam, socket, Catmull-Rom fuse, ember; scroll
curved box 1.13×.86×.018 with rollers. Materials (`targetMaterials.ts`): seeded 512² textures (:24-31, :158),
PBR roles :46-78. Katana (`katanaGeometry.ts`): 20 sections y .095-1.85, width .14 → .09, last three .073/.048/.008,
curve `x = .1 t^1.55`, thickness .035; material :41-60. Dojo (`DojoEnvironment.tsx`): stage width
`max(13, lanes*3.6 + 4)`; a tatami per lane (3.6 - .28) × 7.3 at z .15; torii half-width `lanes*1.8 + .55`; shoji and
posts skip the centre; parallax layers z -5.2/-3.6/0 at -.08/-.18/-.32 × camera x; three swaying lanterns at
y 4.55, z -1.3; petals 32/20/12 by tier; mood in `samuraiStage.tsx`, sky ridges `SamuraiSky.tsx:45`, LCG canvas
textures `dojoTextures.ts`.

## 6. Camera (`avatar/motion/cameraDirector.ts:541-588`)

arena `(sin(.24t)*.22, 3.2 + .1 sin(.31t), 9.2)` → `(0, 1.9, 0)` fov 45; laneFocus z 5.6 → 6.1; roundStart sweep
2600 ms; finale push-in 3200 ms; aim and reaction shots. Focus threshold .82, hold 1400 ms; reaction 1200 ms; fov
response 6; no hard cut during a gesture (frame < 120 ms old). Scene easing `1 - e^(-5.5dt)`, fov `1 - e^(-6dt)`.
Fit `z = max(.86z, halfW/(tan(fov/2)*aspect*.98) + .85)`, `halfW = (lanes-1)*1.8 + 1.35`. Focus/aim/reaction only
with one lane. Live hit intensity is .25, so only bombs reach laneFocus.

## 7. Sound (`sound.ts`, `audio/cues.ts`)

Files `samurai-{slice,hit,combo,bomb,miss}.wav`, `match-over.wav`; synth fallbacks for slice, hit, bomb
(`cues.ts:142-203`). Gain/throttle: slice .5/60, hit .62/35, combo .42/40, bomb .85/90, miss .3/90. slash gain
`.55 + .45p` rate `.92 + .22p`; hit/combo gain `.7 + .3p`, bell at combo ≥ 2 rate `2^(min(12, combo-2)/12)` gain
`.6 + .4 step/12`; bomb pan ×.4; miss gain .85; `match.over` once. Pan `((lane - (n-1)/2)/((n-1)/2))*.55` with n =
player count.

## 8. Phone controller (`SamuraiController.tsx`)

Local swing detector for feedback only (no game payload; the shared controller streams sensors). Charge ring
`envelope/700`. Haptic `s = round(18 + 26p)`: vertical one pulse s + 14 ms, horizontal `[s, 40, round(.7s)]`. Score
ticks `min(4, floor(Δ/100) + 1)` × [12, 50]. Flash 240 ms, direction label 450 ms (Rising / Downward / Cross cut);
no-gyro notice.

## 9. Golden tests

engine: determinism (seed 99); ≤ 4 targets per lane; hot-combo ratio 1.9-2.1; a bomb voids the swing; escaped
fruit keeps the life; Focus doubles exactly, fruit progress +.05×.65 per 100 ms in Focus; a pair at 16000 ms with
launches 3 spawns exactly 2; over 3750 × 16 ms with 4 players `|lat|*1.1 + r*1.15 < 1.5` and fruit/bomb separation.
contactReplay: a vertical blade sweeping x -1 → 1 over 600 ms makes exactly one contact frame at 30, 60, 120 Hz,
identical scores; a still blade never hits. bladeContact :5-10 four boolean cases. bladeIntent: 220°/s for 0.2 s
triggers at 30/60/100 Hz; accel 5 m/s² for 18 × 10 ms triggers; a 300 ms dropout resets; three samples at 300°/s
don't. swing: direction for x ±800, z ±800; recoil swallowed; landscape compensation. sound: bell rates strictly
increasing, ≤ 2.

## 10. Port decisions

| # | Web | Default |
|---|---|---|
| M1 | Spawns depend on wall-clock dt slicing (60 vs 144 Hz differ) | Fixed 60 Hz step (deterministic) |
| M2 | Cuts depend on render rate, avatar IK and packet arrival | Fixed-step blade sampling on the sim clock |
| M3 | Live gesture fixed at (down, .25): direction bonus only for melon/bamboo | Keep for parity; a later pass may wire the real swing direction |
| M4 | One physical swing hitting on several frames counts as several swings | Keep (parity), note for the upgrade pass |
| M5 | Countdown overflow not carried | Keep |
| M6 | `Math.random()` arc tilt and particles | Presentation only; fine |
| M7 | Blade length depends on the shared rig (hand scale unverified) | Use the shared avatar rig port; verify ≈ 1.12 m |
| M8 | Sound pan uses player count | Fix: lane span |
| M9 | Local play | Mouse/right stick moves the blade tip; intent from tip speed |
