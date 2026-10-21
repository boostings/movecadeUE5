# Sky Stacker: port spec

Source: movecade-v2 `app/src/games/stacker/` (paths below are relative to it). Parity first: these are the shipped
rules and numbers. Part of the multi-game port ([05-games-framework.md](05-games-framework.md)).

Notes before porting:
- `pacing.ts` does not exist; `pacing.test.ts` is a bot simulation (§8).
- The flick detector in `drop.ts` is **not wired in production** (`createDropDetector` is never called). Drops come
  only from the phone's DROP button (`runtime.ts:474`), resolved at host receipt with no rewind.

## 1. Engine (`engine.ts`)

Constants (5-54): round 90000 ms; countdown 3000 ms (`../countdown.ts:208`); 3 lives; `TRACK_HALF_WIDTH` 3.2,
narrowing 0.0125/row, min 2.4; `BASE_WIDTH` 3.6 (also max), `MIN_WIDTH` 0.42; `PERFECT_TOLERANCE` 0.18,
`STEADY_PERFECT_TOLERANCE` 0.4, `PERFECT_REGROW` 0.4; `DROP_COOLDOWN_MS` 420; `MAX_TOWER_HEIGHT` 60; complete
bonus 300 + 100/life. Scoring: base 18, +2/row, precision bonus max 12, perfect ×2, combo step 0.12, cap 10.
Sweep: 1.8 u/s + 0.045/row, max 3.6. World units; a row is 0.5 high (render only); time in ms.

Height functions (153-182): `sweepTrackHalfWidth(h) = max(2.4, 3.2 - max(0,h)*0.0125)`;
`sweepLimit(w,h) = max(0, halfWidth(h) - w/2)`; `sweepSpeed(h) = min(3.6, 1.8 + h*0.045)`; tower height =
blocks - 1 (row 0 is the foundation).

`createMatch` (184-215): foundation `{index 0, center 0, width 3.6, ageMs 9999}`; cursor 0; direction +1 for even
lane index, -1 for odd; `lastDropAtMs` -420; `nextId` 1 (shared by effects and shards).

`sweep()` (232-262): reflecting ping-pong. Clamp to ±limit; distance `speed*dt/1000` (negative dt runs backwards),
clamped to `limit*128`; limit ≤ 0 → center 0. Exactly invertible.

`stepMatch(dt)` (345-407):
1. Clamp dt to [0,100].
2. Countdown: subtract; at ≤ 0 switch to playing **in the same call** (the rest of dt still counts).
3. `elapsedMs += dt`; `timeRemaining = max(0, timeRemaining - dt)`.
4. Effects: ttl -= dt, drop at ≤ 0, cap 24 (oldest out).
5. Shards: `vy -= 18*dt_s`, `x += vx*dt_s`, `y += vy*dt_s`, ttl -= dt.
6. Per tower: block `ageMs += dt` while < 1000. Skip eliminated or completed. Skip if balance input is missing
   (freezes sweep and danger). `stepBalance`; on failure: lives -1, combo 0, eliminated at 0 lives, `topout` effect
   "Lost balance — out!" / "Lost balance — steady your palm". Cursor frozen if `elapsed - lastDrop < 420` or
   `recoveryMs > 0`; otherwise sweep at the current height and top width.
7. Ends when `timeRemaining ≤ 0` or every tower is done. `winnerIds` = every tower tied at the max score (max starts at 0).

`dropBlock` (428-573) returns null if not playing, unknown player, tower done, balance input missing,
`recoveryMs > 0`, or inside the 420 ms cooldown. Otherwise:
- `lastDropAtMs = elapsed`, `drops++`; `dropCenter = rewoundCenter(rewindMs)`.
- `sway = balanceDisplacement(leanX)`; `support = below.center + sway`; `offset = |drop - support|`.
- `steady = hypot(leanX, leanY) ≤ balanceLimit(h, w) * 0.5`; `tolerance = min(steady ? 0.4 : 0.18, width/2)`.
- Overlap = interval intersection (the falling block is as wide as the one below). `perfect = offset ≤ tolerance`. `row = h + 1`.
- **Miss** (`!perfect && overlap < 0.42 - 1e-9`): combo 0, lives -1, full-width shard at `dropCenter`; effect
  `miss` "Out!" / "N left"; outcome label "Eliminated", or "Combo broken" if combo was ≥ 2, else "Missed"; score 0;
  width and height unchanged.
- **Land / perfect:** `newWidth = perfect ? min(3.6, below.width + 0.4) : overlap`;
  `newCenter = perfect ? below.center : (left+right)/2 - sway` (stored centers are unswayed). If not perfect and
  `trimmed = width - overlap > 0.02`: shard at `right + trimmed/2` if `drop > support`, else `left - trimmed/2`.
  `precision = max(0, 1 - offset/max(0.001, below.width/2))`; `mult = 1 + min(10, combo_before)*0.12`;
  `delta = round((18 + row*2 + round(precision*12)) * mult * (perfect ? 2 : 1))` — JS rounding, `floor(x+0.5)`.
  `applyLandingImpulse(balance, perfect ? 0 : drop - support, below.width, row)`. Push the block (age 0).
  Perfect: combo++, perfects++, bestCombo; else combo 0. Reload: `entry = cursorDirection`;
  `cursorCenter = entry * sweepLimit(newWidth, row)`; `cursorDirection = -entry`.
- **Complete** (row ≥ 60): bonus `300 + lives*100`, `completed`, `cursorCenter = newCenter`; effect `complete`
  "Tower complete! +{bonus}", scoreDelta = delta + bonus; outcome "Tower complete"; match ends at once if every tower is done.
- Effect intensity: perfect `min(1, 0.55 + 0.15*min(combo,3))`; land `max(0.25, precision)`. Labels "Perfect ×N"
  (combo ≥ 2) / "Perfect!" / "+N".

Effect durations (291): complete 1600, perfect 900, miss 800, land/topout 900.
Shards (310-332): max 32; `side = x < 0 ? -1 : 1`; `vx = side*(1.15 + w*0.9)`, `vy = 1.9 + min(1.2,w)*0.5`,
`spin = side*(2.6 + 2.6/(0.35+w))`, ttl 1400.

Lanes (`lanes.ts`): a tower keeps its `laneIndex`. A departure (`runtime.ts:205`) splices the tower and its
effects, shards and winner entry. Lane span = max lane + 1 (cap 4). Scene lane X = `(i - (span-1)/2) * 8`.

## 2. Balance (`balance.ts`, degrees)

Stale 900 ms; failure 2200 ms; steady ratio 0.5.
- `windAt(t_ms, h)` (t in s): `strength = (3 + min(4, h*0.14)) * min(1, t/18)`;
  `x = strength*(sin(0.31t) + 0.28 sin(0.83t))`; `y = strength*0.65*sin(0.27t + 0.8)`. Match time; same for every tower at a height.
- `balanceLimit(h, w=3.6) = max(7.6, 12 - min(2.4, h*0.06) - max(0, 1 - w/3.6)*2)`.
- `balanceDisplacement(lean) = sign*min(0.45, max(0, |lean| - 0.4)*0.045)`.
- `inputMissing = isFinite(lastInputMs) && elapsed - lastInputMs > 900` (before any frame: not missing — web quirk).
- `applyLandingImpulse`: `kick = clamp(offset/max(0.42, w), -1, 1) * (10 + min(12, h*0.5))`; `vx += kick`;
  `|offset| < 0.14` → vx, vy ×0.65.
- `stepBalance` (42-66): store wind; false if input missing; dt clamped [0,100]; substeps of `min(rem,10)/1000` s;
  roll, pitch clamped ±35 (0 if no input yet); per axis `v += ((roll + wind - lean)*28 - v*8)*h; lean += v*h`.
  Then `recoveryMs -= dt`; if > 0: danger 0, false. `ratio = hypot(lean)/limit`; ratio > 1 → `danger += dt`, else
  `danger = max(0, danger - 2dt)`. danger ≥ 2200 → danger 0, recovery 2000, lean ×0.35, v 0, return true.
- Input (`runtime.ts:501-508`) from the frame quaternion: `nx = 2(xz + wy)`, `ny = 2(yz - wx)`,
  `nz = 1 - 2(x²+y²)`; `roll = deg(atan2(nx, nz))`, `pitch = deg(atan2(ny, nz))`; `lastInputMs` = elapsed at receipt.

## 3. Runtime (`runtime.ts`)

- DROP: event `stacker:drop` (no payload), resolved at receipt, no rewind (465-477); ignored during the game intro.
  No network-delay policy: the 420 ms cooldown de-dups duplicate deliveries.
- Clock: `advance()` from frames and a 50 ms interval, wall-clock dt sliced into ≤ 100 ms steps, catch-up cap
  1000 ms; frozen during the intro or a room pause. Pose stale after 900 ms.
- HUD every 100 ms; `go` = playing and `elapsed < 700`. Scores every 1000 ms (changed only, not in the countdown).
- Feedback (540-549): tone warning (miss/topout) else success; haptic warning / celebration (perfect, complete) /
  impact; prominence major for complete or a perfect at combo ≥ 3; detail "+N points · L lives · B blocks".
- End: compare hold 8000 ms (`compareHold.ts`); `started`/`complete` one-shot; `complete` → `completeGame(scores)`.
- Snapshot v1 (236-334): `v,g,m,ph,el,cd,tr,wi,t[]`; tower `i,n,l,s,c,bc,pf,dr,lv,e,cc,cdir,ld`, `b` flat triples
  (center ×1000, width ×1000, perfect), `co` only when true. Balance, shards, effects not saved; restored blocks
  age 10000; refused once any drop happened. Rebuild only on a new match number; departures are mutations.

## 4. Flick detector (`drop.ts`, unused)

`DROP_TUNING` (104-133): onset 7 m/s², commit 12, release 0.4 (floor 4.5), duration 30-320 ms, cooldown 240,
reference 25, smoothing 0.5; gyro onset 260°/s, commit 480, reference 900, release floor onset×0.6, confidence
ceiling 0.6. Port only for a flick option.

## 5. Phone controller (`StackerController.tsx`)

DROP on `pointerdown` → `stacker:drop` + `buzz(14)` + 120 ms flash (`onClick` ignored for 700 ms after). Balance
readout: `roll = clamp(gamma, ±30)`, "Level" below 5°, "Leaning left/right", dot at `50 + roll/30*42 %`. Text
"Balance, then drop", "DROP", "{score} pts". The phone shows gamma; the host balances on the quaternion roll/pitch.

## 6. Presentation

- `presentation.ts`: row 0.5; 4 work rows; `targetScroll(h) = max(0, h - 4)`; `builderLiftY = max(0, h - scroll)*0.5`.
  Builder pose over `t = clamp(age/420)` (×0.2 under reduced motion): reach `sin(min(1,t/0.4)π)`, brace
  `sin(min(1,t/0.7)π)`, recoil `sin(clamp((t-0.3)/0.7)π)`, reload `t`.
- Scene (`StackerScene.tsx:62-99`): 16 visible rows per tower; block a rounded box `1 × 0.43 × 1.5` (radius 0.11);
  pools shards 32, sparks 72, dust 96, birds 8; compare rows 0.15; squash 280 ms; plinth `6.78 × 0.62 × 2.08` at
  y -0.4 in the lane colour. Scroll `+= (want - scroll)(1 - e^(-8dt))`, never above want.
- Blocks (922-934): `squash = (1-t)^1.5 cos(3πt)`; `x = laneX + center + disp(leanX)*(index+1)/len`;
  `y = (index - scroll)*0.5 + 0.25 - max(0,squash)*0.08`; scale `(w(1+0.32sq), 1-0.5sq, 1+0.2sq)`; perfect blocks
  bright (lerp to white 0.45), else even base / odd ×0.74.
- Cursor (1024-1086): hidden in the cooldown; `y = (top.index + 1.35 - scroll)*0.5 + 0.25 + 0.06 sin(6t + lane) +
  beta*0.08`; roll `clamp(leanX/limit, ±1.5)*0.12`, pitch ×0.1, yaw alpha ×0.14; pulse urgency
  `1 - min(1, (w - 0.42)/1.6)`.
- Shards scale ×life, tumble `(0, (1-life)spin·0.45, (1-life)spin)`.
- Effects (1178-1227): perfect sparks `round(8 + 16i)`, +10 white ring at i ≥ 0.99, flash ring, shake
  `min(0.6, punch*0.45)` with punch `0.45 + 0.75i`; miss 22 dust, punch 1.2; land punch `0.2 + 0.3i`.
- Altitude beats: birds every 5 rows (1.8 s), "peek" from row 10 (1.2 s).
- End showcase: compressed full towers; after 2.5 s orbit the winner (score, then height) at 0.32 rad/s, radius
  `5.2 + top*0.9`; losers dim toward `#4f5566` (×0.62 over 1.2 s).
- Camera: home `[0, 5.6, 10 + span*2.5]` look `[0, 1.25, 0]` fov 46, smoothing `1 - e^(-7dt)`; arena (playing, > 1
  lane): x 0, `z ≥ 2.5 + halfWidth/(tan(fov/2)*aspect*0.92)`, `halfWidth = (span-1)*4 + 3.7`. Compare shot 437-472;
  tables in `../avatar/motion/cameraDirector.ts:672-694`. Stage camera `[0, 4.4, 19]` fov 46; intro `[6, 6.5, 24]` →
  `[0, 2, 0]` fov 48.
- Mood (`StackerCanvas.tsx:34-42`): golden-hour rooftop; sky `#2a5580` → horizon `#e3ae94`, fog 34-118; sun
  `[11,15,10]` 2.6, rim `[10,5,-14]` 1.35, hemisphere 0.7; bloom 0.8 (threshold 0.82); vignette 0.4. Lane colours
  `#6d8bff #3fc9c2 #f2b23f #a083ff`.
- Rooftop (`RooftopEnvironment.tsx`): deck width `max(18, lanes*8 + 8)`, depth 14, y -0.696, z 1; edge box
  `w × 0.66 × 14` at y -1.03; deck texture with dashed `7.3 × 4.8` work zones in lane colours numbered "01".., a
  yellow hazard stripe. Three city bands (`makeCity` 49-82): 28/23/18 buildings at z -49/-29/-14, deterministic
  heights, setbacks, ledges, antennas, window grids (lit `#edd3a4` ×1.9). Parallax y = -maxScroll × 0.07/0.12/0.18.
  Four clouds x [-36,-12,18,43], y 9/11, z -62, drifting `sin(0.025t)*2.2`.
- Props (`RooftopProps.tsx`): yellow crane (`#d39a32`) at x `-w/2 - 2`, z -13, mast 12, 12-unit jib at y ~11, hook;
  railing at z -3.5 and sides; two cones, cable spool, pallet with three boxes, HVAC with a fan at 2.4 rad/s, two
  flags (`#d86654`, `#4c8990`).
- Block texture (`blockSurface.ts`): grey gradient panel, border, ribs every 52 px of 512, four bolts; colour from tint.
- Avatar (`StackerAvatar.tsx`): floor y -0.7, z 2.5, yaw 0.35, scale 0.88 at 4 lanes else 0.96; rides a lift
  `1.9 × 0.18 × 1.4` with a piston at `builderLiftY`. Carried block width `min(0.5, max(0.42, w)*0.16)`, 0.16
  thick, between the hands, hidden in cooldown/recovery. Hands: `x → cursor*0.42`, `y = -0.19 reach + 0.1 recoil -
  0.08|balY|`, `z = -0.18 reach + 0.08 recoil`. Drive `min(0.95, 0.18 + sweep*0.08 + strain*0.32 + brace*0.5 +
  0.25 recovering)`. Lateral `cursor/3.2*0.3 - balX*0.7`, smoothed `1 - e^(-14dt)`. Reactions: perfect/complete →
  'perfect' + joy (700 ms); miss; topout → 'topple'; elimination; finishFirst / finishLast.

## 7. Sound (`sound.ts`, cues `app/src/audio/cues.ts:211-276`)

Pan `((lane - (n-1)/2)/((n-1)/2))*0.55` with n = tower count. land: `stacker.drop`, rate `1 + min(24,row)*0.02`,
gain `0.7 + 0.3i`. perfect/complete: drop at gain 1 plus `stacker.perfect`, `step = min(12, combo-1)`,
rate `2^(step/12)`, gain `0.7 + 0.3 step/12`. miss/topout: `stacker.miss`. Shards ≥ 0.3 wide: `stacker.debris`,
rate `1.15 - min(1, w/2)*0.35`, gain `0.5 + min(1,w)*0.5`. Altitude once per 5-row band of the tallest tower,
rate `2^(2·min(6, band-1)/12)`, gain `0.75 + 0.25 step/6`. `match.over` once at the end. Samples
`app/public/audio/{stacker-drop,stacker-perfect,stacker-debris,stacker-miss,stacker-altitude,match-over}.wav`.
Cue gain/throttle: drop 0.6/30 ms, perfect 0.5/30, debris 0.26/55, miss 0.34/90, altitude 0.4/400, match.over 0.5/800.

## 8. Golden tests

- Lean 0 is steady, so the perfect window is 0.4.
- First drop at offset 0 → perfect, +64; a second perfect → `round(34*1.12*2)` = 76.
- Offset 1.0 on the foundation → land, width 2.6, +25; shard width 1 at x 2.3, vx 2.05, vy 2.4, spin ≈ 4.5259.
- Offset `3.6 - 0.21` → miss: lives 2, height 0, score 0 (`engine.test.ts:315`).
- leanX 4 → sway 0.162; a cursor at 0.162 is perfect, stored center 0 (`engine.test.ts:352`).
- Steady perfect with vx 2 → 1.3 (`engine.test.ts:337`).
- After a drop the cursor sits at `|sweepLimit(w, 1)|` and holds for 420 ms (`engine.test.ts:321`).
- Completing row 60 from a raised tower with combo 0 → 300 + 600 = 900, "Tower complete! +600".
- Two towers mirror: `c0 = -c1` (`engine.test.ts:267`).
- Countdown: still counting after 29 steps of 100 ms, playing on the 30th.
- Countering the wind exactly at h 40, w 0.6 for 90 s → no losses, lean < 0.01 (`balance.test.ts:4`); roll 40 for
  3.4 s → exactly one loss; inertia matches at dt 10, 20, 100.
- Compare hold: start at 1000 ms, complete at 9000 ms, once each.
- `cursorY - feetY` is always 1.625.
- `pacing.test.ts` (LCG bot, 20 ms steps): over 20 seeds every round lasts ≥ 60 s and reaches height ≥ 25; ≥ 18/20
  survive 90 s; active balancing earns > 1.1× the perfects of a level phone.

## 9. Port decisions to make

| # | Web behaviour | Default |
|---|---|---|
| S1 | Variable wall-clock dt: the sweep is exact, balance drifts with the split (10 ms substeps, wind sampled per call) | Fixed 10 ms step (deterministic) |
| S2 | Drops resolve at host receipt (latency-dependent) | Keep (no rewind, as shipped) |
| S3 | Before any frame, balance integrates wind with roll 0, so danger can build for a silent phone | Fix: treat "no frame yet" as missing |
| S4 | Cursor smoothing `exp(-14/60)` per frame (frame-rate dependent) | Fix: per-second rate |
| S5 | Phone shows gamma; host uses the quaternion roll/pitch | Keep the host's mapping; verify axes against the protocol |
| S6 | Sound pan uses tower count, not lane span | Fix: lane span |
| S7 | `winnerIds` ties by score; scene/HUD break ties by height | Keep both |

## 10. Port status (2026-10-21)

- **Rules:** `Games/Stacker/StackerRules` ports engine.ts and balance.ts, with suites `MovecadeKart.Stacker.{Engine,Balance}`
  covering the §8 goldens (scores, trims and shards, sway, landing impulse, completion, mirroring, countdown,
  countering the wind for 90 s, the single topple).
- **S1 to S7 as built:** the rules step on a fixed 10 ms clock. Drops resolve at receipt with no rewind. S3 lives in
  the game layer, so the parity tests stand: a phone lane starts with a stale `LastInputMs`, so it is frozen and
  can't drop until its first frame. S4: the cursor is drawn where the rules hold it (no smoothing; it is hidden
  through the reload anyway). S5: the host maps the quaternion as runtime.ts does, falling back to gamma and beta.
  S6: the cue player is 2D, so there is no pan. S7: the HUD and showcase sort by score, then height.
- **Rooftop:** `AStackerRooftop` holds the golden-hour look and the deck: work zones, plinths, lane numbers and the
  hazard stripe. Around it are the building under the roof and three city bands that sink with the scroll at their
  parallax. Props: the crane with its hook, the railing, cones, the spool, the pallet, the HVAC fan and two flags.
- **Game:** `AStackerGame` draws the blocks as rounded ribbed panels: the landing squash, the lean carried up the
  tower, perfect blocks brighter. The cursor pulses as the block narrows; shards tumble. A perfect gets sparks, a
  flash and a ring; a miss or topple gets dust; a completed tower gets confetti. It plays the cues (the perfect
  climbs a semitone per combo, debris by width, altitude per five rows). Each builder rides a lift beside their
  lane (the web's spot in front hid a lone tower) with the next block in hand, and reacts to each drop. The end
  showcase compresses every tower to .15 a row, dims the losers and swings around the winner. `SStackerHud`
  shows the clock, the wind, and per lane the score, lives, row, combo, balance gauge and danger. It also has the
  callouts, the "hold your phone flat" banner, the controls, the intro and the standings.
- **Controls:** one local builder; the left stick, A/D or the mouse lean the palm, W/S pitch it; A, Space, Enter or a
  click drops. House bots counter the wind with a wandering hand and drop at a mark scattered by skill.
- **Party Mode:** the lobby offers Sky Stacker. The mode routes sensor frames to the palms and `stacker:drop` to
  the drop. It announces simultaneous play and the countdown, sends feedback per effect ("+N points · L lives ·
  B blocks"), and sends live scores only when they change. It completes the match after the 8 s showcase. Test:
  `e2e.ps1 -RoomGame stacker -QuitAfter 150 -SimSeconds 145`; `phone-sim.mjs --game stacker` streams a 60 Hz palm and
  drops every 1.3 s.
