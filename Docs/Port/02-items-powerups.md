# 02 - Items, Powerups and Hazards (Movecade Kart Racer port spec)

Source: `C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2` (read-only). All refs are `path:line` relative to that repo root.
Target: Unreal Engine 5.8, C++.

Abbreviations used below:
- `ENG` = `app/src/legacy/Experience/kart/engine`
- `KART` = `app/src/games/kart`
- "u" = one three.js world unit. Kart speed in code is labelled "mph" (`ENG/constants.ts:1`) but it is applied as **u/s** (`ENG/MultiplayerKart.tsx:1380-1381`, `position += dir * speed * delta`). Kart top speed is 40 u/s. Suggested UE scale: 1 u = 1 m = 100 UU (keep as one project constant, `KART_UNITS_TO_UU`).

---

## 0. Where the item code actually lives

The production kart runtime is the "legacy" engine mounted by `KART/KartCanvas.tsx:13-41` -> `app/src/legacy/games/kart/KartRaceCanvas.tsx:116-145` -> `ENG/MultiplayerApp.tsx` (multiplayer, the shipped path). Single-player `ENG/App.jsx` + `ENG/Powerups.jsx` is **DEV-only** (`KartRaceCanvas.tsx:26-30`).

| Concern | File |
|---|---|
| Item enum, kart status fields, odds table, status setters | `ENG/store.ts` |
| Item use (instant effects + projectile spawn) | `ENG/useItemAction.ts` |
| Projectile simulation, hit detection, hit effects, explosion | `ENG/MultiplayerItems.tsx` |
| Bounce / homing / arc / AoE math | `ENG/ItemPhysics.ts` |
| Item boxes (multiplayer) | `ENG/MultiplayerPowerups.tsx` |
| Item box positions, boost pad positions | `KART/tracks/catalog.ts`, `KART/tracks/violetLoop.ts`, `KART/tracks/harbor.ts` |
| Boost pads | `ENG/BoostPads.jsx`, `KART/boostPad.ts` |
| Status-effect expiry, Bullet Bill autopilot, boost timer, speed composition | `ENG/MultiplayerKart.tsx` |
| Roulette UI, activation popup, screen overlays (ink/flash/rainbow) | `ENG/RaceUI.tsx` |
| HUD item slots | `ENG/KartHud.tsx`, `ENG/kartItems.ts` |
| Tactical hint text | `ENG/itemTacticalHint.ts` |
| Item lifetime clamp / shader warmup / item collision mesh filter | `KART/itemLifetime.ts`, `KART/itemWarmup.ts`, `KART/itemCollisionMeshes.ts` |
| Impact FX bus (shake / flash / burst profiles) | `KART/scene/kartFxBus.ts` |
| Continuous kart FX rules (boost flames etc.) | `KART/scene/kartTrails.ts`, rendered by `ENG/misc/KartFxLayer.tsx` |
| SFX cue table / state-driven SFX / event SFX | `app/src/audio/kartCues.ts`, `KART/sound.ts`, `ENG/kartAudioEvents.ts` |
| Phone controller ITEM button | `KART/KartController.tsx` |
| Phone event -> host item use | `app/src/games/legacyBridge.ts:317-335` |
| Server (PartyKit) | `partykit/src/engine.ts:739-771` relay only (no item logic, see section 9) |

Authority model: the **TV/host browser simulates everything** (all karts, all items, all boxes). Phones only send inputs. `playKartEvent` only sounds on the host (`ENG/kartAudioEvents.ts:9`).

---

## 1. Item list (11 items)

`ItemType` union: `ENG/store.ts:203-215`. Icons/names: `ENG/kartItems.ts:10-36`. Config table `ITEM_CONFIGS`: `ENG/MultiplayerItems.tsx:51-155`. Use logic: `ENG/useItemAction.ts:35-185`.

| Id | HUD name (`kartItems.ts:24-36`) | Icon | Kind | Speed u/s | TTL s | Effect key |
|---|---|---|---|---|---|---|
| `GREEN_SHELL` | Green shell | green circle | Projectile, straight, bouncing | 80 | 5 | spin |
| `RED_SHELL` | Red shell | red circle | Projectile, homing (nearest ahead), bouncing | 90 | 5 | spin |
| `BANANA` | Banana | banana | Dropped hazard behind kart | 0 | 5 | spin |
| `MUSHROOM` | Mushroom | mushroom | Instant self boost | - | - | boost |
| `TRIPLE_MUSHROOM` | Triple boost | 3 mushrooms | Instant self boost (one long boost) | - | - | boost |
| `STAR` | Star | star | Instant self invincibility + boost | - | (8) | boost |
| `LIGHTNING` | Lightning | bolt | Instant field effect on all others | - | - | slow |
| `BLOOPER` | Blooper | squid | Instant field effect on all others (ink) | - | - | blind |
| `BOB_OMB` | Bomb | bomb | Lobbed arc projectile, AoE explode | 30 (+15 up) | 5 | explode, r=8 |
| `BLUE_SHELL` | Blue shell | blue heart | Projectile, homing (1st place), AoE | 100 | 5 | explode, r=10 |
| `BULLET_BILL` | Bullet Bill | rocket | Self transform: autopilot + invincible + boost | - | (6) | boost |

`THROWN_ITEM_TTL_SECONDS = 5` (`ENG/MultiplayerItems.tsx:32`) applies to every thrown/dropped item, **including bananas**.

Duplicate speed table used at spawn time: `ITEM_SPEEDS` (`ENG/useItemAction.ts:14-26`), identical values; fallback `|| 60` (`useItemAction.ts:131`) is never hit.

> IP note: Bob-omb, Bullet Bill, Blooper, Blue/Red/Green Shell and Star are Nintendo names. The UE data assets should use neutral internal ids (e.g. `Item.Shell.Straight`) with display names in a string table so the shipping names can be changed in one place.

---

## 2. Obtaining items

### 2.1 Item boxes ("mystery boxes")

Multiplayer implementation: `ENG/MultiplayerPowerups.tsx`.

| Constant | Value | Ref |
|---|---|---|
| Box size (outer cube edge) | 2.0 u | `MultiplayerPowerups.tsx:35` |
| Inner cube edge | 1.4 u (0.7 x size) | `:38` |
| Glow sphere radius | 1.8 u (0.9 x size), 8x8 segments | `:39` |
| "?" label planes | 1.56 u square (0.78 x size), on all 6 faces, offset size/2 + 0.01 | `:40`, `:208-213` |
| Pickup radius | 2.5 u, **horizontal (XZ) only** | `:102`, `:175-179` |
| Respawn (per player) | 15 s | `:95`, `:151-155` |
| Spin | group yaw += 1.5 rad/s, inner cube extra yaw += 0.5 rad/s | `:159`, `:164` |
| Bob | y = base + sin(t * 2.5) * 0.1 | `:160` |
| Colour | `#ffcc00`, emissive `#ff9900` | `:28-31` |
| Inner material | MeshStandard, emissiveIntensity 0.6, metalness 0.8, roughness 0.2 | `:107-113` |
| Glow material | Basic, transparent, opacity 0.25 | `:114-118` |
| Outer material | Standard, transparent opacity 0.4, emissiveIntensity 0.2 | `:119-125` |
| "?" texture | 256x256 canvas, bold 180px "Arial Black", white fill, `#8b4f00` 20px stroke | `:45-77` |
| Visibility re-check cadence | every 0.5 s | `:187-193` |

Rules (`MultiplayerPowerups.tsx:146-201, 235-249`):
1. Each box keeps `collectedBy: Map<playerId, collectTime>`. A player inside the 2.5 u radius who is not in the map is added (`collectedBy.set`) **and then** `onCollect` runs.
2. `onCollect` gives an item only if `kart.heldItem` is null (`:239-241`). Note the ordering bug: a player who drives through a box while already holding an item still has that box locked for 15 s and receives nothing.
3. The box is **hidden only when every player has collected it** (`visibleCount < playerCount`, `:196-201`). A box you personally cannot use stays visible.
4. Respawn timing uses the R3F render clock (`state.clock.elapsedTime`, `:148`), which keeps running while the race is paused (items themselves do pause, see 3.1).
5. Item is chosen at pickup time by `getPositionBasedItem` (`:244`) and stored with `setKartItem` (`:245`).
6. No race-status gate; boxes can be touched before GO (karts are stationary then).

DEV-only single-player boxes (`ENG/Powerups.jsx`): 1.2 u box, respawn 15 s **global** (not per player), pickup 2.5 u, flat pool `MUSHROOM x3, GREEN_SHELL x2, RED_SHELL x1, BANANA x3, TRIPLE_MUSHROOM x1, STAR x1` (`Powerups.jsx:18-30`). Do not port; documented for completeness.

### 2.2 Box placement

Active set swaps by track in `configureKartTrack` (`KART/tracks/catalog.ts:77`).

**Violet Loop** (`KART/tracks/violetLoop.ts:269-274`) - one cluster of 4 (three.js x, y, z):

| # | x | y | z |
|---|---|---|---|
| 0 | 164.88 | 1.0 | -59.22 |
| 1 | 164.96 | 1.0 | -62.16 |
| 2 | 165.22 | 1.0 | -71.48 |
| 3 | 165.07 | 1.0 | -66.37 |

**Sunset Harbor** (`KART/tracks/harbor.ts:57-62`) - 4 rows x 3 boxes, at spline parameter t = 2/29, 10/29, 17/29, 26/29, lateral offsets -8, 0, +8 u from the centreline (road width 28, `harbor.ts:11`), y = 1. Spline is the closed Catmull-Rom `harborPoint(t, offset)` over `HARBOR_CONTROL_POINTS` (`harbor.ts:2-36`). Evaluated values:

| t | offset | x | z |
|---|---|---|---|
| 0.0690 | -8 / 0 / +8 | 0.43 / 8.00 / 15.57 | -113.42 / -116.00 / -118.58 |
| 0.3448 | -8 / 0 / +8 | 193.51 / 198.00 / 202.49 | -136.62 / -130.00 / -123.38 |
| 0.5862 | -8 / 0 / +8 | 252.99 / 245.00 / 237.01 | 79.45 / 79.00 / 78.55 |
| 0.8966 | -8 / 0 / +8 | -77.27 / -70.00 / -62.73 | 48.66 / 52.00 / 55.34 |

(UE: compute from the track spline at import instead of hardcoding; store as `t + lateralOffset` in the track data asset.)

### 2.3 Position-weighted odds (exact)

`getPositionBasedItem`: `ENG/store.ts:2003-2091`. Uniform pick from a pool array (`:2090`, `Math.random`).

Inputs:
- `position` = `getPlayerRacePosition` (`store.ts:1982-2001`): sort by laps desc, then `currentCheckpoint` desc, then `progressPercent` desc; returns 1 when there is at most 1 kart.
- `totalPlayers` = kart count (max 4, `KART/index.ts:13`).
- `positionRatio = totalPlayers > 1 ? (position-1)/(totalPlayers-1) : 0.5` (`:2007`).
- `progressScore = laps*100 + progressPercent`; `trailingFactor = min(1, max(0, leaderScore - myScore) / 120)` (`:2008-2015`).

Bracket choice (`:2072-2088`):
```
if positionRatio <= 0.2                                  -> FRONT
else if positionRatio <= 0.55 && trailingFactor < 0.55   -> MID
else                                                     -> BACK
override: position == 1 && totalPlayers <= 2             -> FRONT (solo and 2-player leader)
```

Pools (array copies, entry counts are the weights):

**FRONT** (`:2019-2024`, also override `:2082-2087`) - 9 entries
| Item | Count | % |
|---|---|---|
| BANANA | 4 | 44.4 |
| GREEN_SHELL | 3 | 33.3 |
| MUSHROOM | 1 | 11.1 |
| RED_SHELL | 1 | 11.1 |

**MID, totalPlayers <= 2** (`:2026-2033`) - 10 entries
| Item | Count | % |
|---|---|---|
| MUSHROOM | 3 | 30 |
| GREEN_SHELL | 2 | 20 |
| RED_SHELL | 2 | 20 |
| BANANA | 1 | 10 |
| TRIPLE_MUSHROOM | 1 | 10 |
| BOB_OMB | 1 | 10 |

**MID, totalPlayers >= 3** (`:2034-2042`) - 12 entries
| Item | Count | % |
|---|---|---|
| MUSHROOM | 3 | 25 |
| GREEN_SHELL | 2 | 16.7 |
| RED_SHELL | 2 | 16.7 |
| BANANA | 2 | 16.7 |
| TRIPLE_MUSHROOM | 1 | 8.3 |
| BOB_OMB | 1 | 8.3 |
| BLOOPER | 1 | 8.3 |

**BACK, totalPlayers <= 2** (`:2044-2050`) - 8 entries
| Item | Count | % |
|---|---|---|
| TRIPLE_MUSHROOM | 2 | 25 |
| MUSHROOM | 2 | 25 |
| RED_SHELL | 2 | 25 |
| STAR | 1 | 12.5 |
| BOB_OMB | 1 | 12.5 |

**BACK, totalPlayers == 3** (`:2052-2060`) - 10 entries
| Item | Count | % |
|---|---|---|
| TRIPLE_MUSHROOM | 2 | 20 |
| MUSHROOM | 2 | 20 |
| RED_SHELL | 2 | 20 |
| STAR | 1 | 10 |
| BLOOPER | 1 | 10 |
| LIGHTNING | 1 | 10 |
| BOB_OMB | 1 | 10 |

**BACK, totalPlayers >= 4** (`:2061-2070`) - 12 entries
| Item | Count | % |
|---|---|---|
| MUSHROOM | 3 | 25 |
| TRIPLE_MUSHROOM | 2 | 16.7 |
| RED_SHELL | 2 | 16.7 |
| STAR | 1 | 8.3 |
| BULLET_BILL | 1 | 8.3 |
| LIGHTNING | 1 | 8.3 |
| BLUE_SHELL | 1 | 8.3 |
| BOB_OMB | 1 | 8.3 |

Resulting bracket per seat:

| Lobby | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| 1 | FRONT | - | - | - |
| 2 | FRONT | BACK(<=2) | - | - |
| 3 | FRONT | MID(3+) or BACK(3) if trailing >= 0.55 (>= 66 progress pts behind) | BACK(3) | - |
| 4 | FRONT | ratio 0.333: MID(3+) or BACK(4) if trailing >= 0.55 | BACK(4) (ratio 0.667) | BACK(4) |

So Blue Shell, Bullet Bill appear only in 4-player lobbies, Lightning only in 3+, Blooper only in 3+.

### 2.4 Roulette (cosmetic delay before an item is usable)

- `setKartItem` (`ENG/store.ts:1725-1746`): if the kart had no item, sets `heldItem`, `isRollingItem = true`, `rollingStartTime = Date.now()`.
- `executeItemUse` refuses while `isRollingItem` (`ENG/useItemAction.ts:40`).
- The roll is ended **by the UI component** `ItemRoulette` (`ENG/RaceUI.tsx:391-497`): the slot animation finishes at `SLOT_DURATION = 1800 ms` or when velocity < 0.5 and position >= 90% of target; then `setTimeout(stopItemRolling, 1200)` (`RaceUI.tsx:435-444`). Effective lockout: **~3.0 s** after pickup. If the roulette UI is not mounted, the item never becomes usable (port this as a gameplay timer, not UI).
- Animation constants (`RaceUI.tsx:148-149, 255-258`): item height 50 px, 3 visible, initial velocity 25 items/s, decay `0.985^(dt*60)` per frame, min velocity 0.5. Strip = 34 uniform-random entries from all 11 types (`ALL_ITEM_TYPES`, `:240-252`) + the real item last, length 35 (`:381-389`, `:411`). Label under the frame shows the item id with `_` -> space once settled (`:493`).
- Activation popup when an item is spent (`RaceUI.tsx:514-559`): icon + name, `activationPopIn 0.4s`, `activationPopOut 0.4s` starting at 1.2 s.
- HUD slot (`ENG/KartHud.tsx:21-45`): states `empty` / `rolling` / `held`; glyph `ITEM_ICONS[heldItem]` (shows the real item glyph even while rolling - a spoiler quirk), name "Rolling" / item name / "Empty". Rail cue "Choosing item..." while rolling (`KartHud.tsx:117-118`).
- Tactical hint copy per item/position/final lap: `ENG/itemTacticalHint.ts:66-181` (used by legacy `KartRaceHUD.tsx:855`). Several hints describe mechanics that do not exist ("hold it behind you").

Only one item can be held at a time; there is no item queue and no "triple" multi-charge.

---

## 3. Triggering an item

### 3.1 Input paths (all end in `executeItemUse(playerId)`)

1. **Phone controller** (`KART/KartController.tsx:108-113, 164-181`): ITEM button `onPointerDown` (keyboard/assistive via `onClick` with `detail === 0`) -> `client.sendGameEvent('kart-input', { useItem: true })`, haptic `HAPTIC.gesture` = 24 ms (`app/src/hooks/haptics.ts:17`), button flash 140 ms. No rate limit on the phone; the phone **does not display** the held item.
2. **PartyKit relay** (`partykit/src/engine.ts:739-771`): `game-event` from a seated, non-eliminated player is broadcast to all other sessions with `fromPlayerId`. Control token bucket: 40 msgs/s, burst x2 (`protocol/src/constants.ts:119-125`, `partykit/src/rateLimiter.ts`).
3. **Host bridge** (`app/src/games/legacyBridge.ts:327-331`): `data.useItem === true` -> dynamic import -> `executeItemUse(fromPlayerId)`.
4. Legacy v1 path (`app/src/legacy/services/messageHandlers/controlHandlers.ts:325-335` -> `ENG/MultiplayerMobileControlBridge.tsx:204-225`): host-only, de-dupe by timestamp, `ITEM_RATE_LIMIT_MS = 200` (`:66`).
5. **Keyboard on host** (debug): `item` = `KeyE` / `ShiftLeft` (`ENG/MultiplayerApp.tsx:49`), rising-edge in `ENG/MultiplayerKart.tsx:954-963`.
6. **Bots** (`app/src/host/bots/kartPilot.ts:222-231`): once holding a usable item, fire after `1200 + random*2500` ms.
7. **Assist action** (`app/src/controller/assistActions.ts:31-32`).

### 3.2 `executeItemUse` (`ENG/useItemAction.ts:35-185`)

Guards: kart exists, not rolling, has item (`:39-41`). Item is cleared with `setKartItem(playerId, null)` after use.

Projectile spawn (`:126-182`):
- forward = `(-sin(yaw), 0, -cos(yaw))` (`:126-129`).
- spawn position = kart position + forward * `spawnOffset`; `spawnOffset = 2` (ahead), banana `-2` (behind) (`:132`, `:138`). Bomb spawn y = kart y + 1 (`:170`).
- velocity = forward * speed; banana 0; bomb `(fwd*30, 15, fwd*30)` (`:143`); blue shell aimed **straight at 1st place's current position** (`:148-162`), falls back to forward.
- `explosionRadius`: bomb 8 (`:144`), blue 10 (`:147`). `isHoming` true for red/blue (informational; behaviour is keyed on type).
- id = `${type}-${playerId}-${Date.now()}` (`:166`), `rotation = yaw`, `createdAt = now`.

---

## 4. Per-item behaviour (exact)

Projectile tick: `Item` component `ENG/MultiplayerItems.tsx:293-637`.

### 4.0 Shared projectile rules

- **Delta clamp / pause**: `itemSimulationDelta(delta, paused)` = 0 if paused or non-finite, else `clamp(delta, 0, 1/30)` (`KART/itemLifetime.ts:2-5`). Paused = race not `racing`, race paused, or PartyKit game paused (`MultiplayerItems.tsx:384`). Age (`ageMs`) only advances with this clamped delta, so stalls/pauses do not consume lifetime (`:386`). Below 30 fps items move in slow motion.
- **Lifetime**: removed when `age > lifetime` (`:390-397`); bomb explodes at timeout (`:392-394`).
- **Kart hit test** (`:578-605`): every kart, **horizontal distance < 1.5 u** (`COLLISION_DISTANCE`, `:313`), height ignored. Owner is immune only while `age < 0.5 s` (`:583`); after that you can hit yourself. Explosion items call `handleExplosion`; others play `kart.projectile-hit`, emit FX `projectile-hit` and apply `effectOnHit` to the victim (`:594-600`). Item is removed after the first hit (`:602`), including when the victim is invincible (invincible karts "eat" projectiles).
- **No item-vs-item collision** (shells do not destroy bananas etc.).
- **Wall collision**: only against meshes from `collectItemCollisionMeshes` (`KART/itemCollisionMeshes.ts:5-29`): skip any mesh whose ancestor has `userData.kartActor` / `kartDecoration`, or name contains `powerup`, starts with `item-`, or equals `boost-pads`. Sunset Harbor: only meshes named `harbor-barrier`. Violet: skip names matching `/wheel|body|booster|tire|start|finish|banner|line|flag|spawn|checkpoint|waypoint/` and meshes whose materials are all transparent with opacity < 0.5. Re-gathered on mount, after 1 s, and on track collider revision (`MultiplayerItems.tsx:669-683`).
- **Shell bounce** `calculateShellBounce` (`ENG/ItemPhysics.ts:96-180`):
  - walls = ray hits whose world normal |y| < 0.7 (`:48`).
  - ray origin = item pos + (0, 0.3, 0) (`:119`). `impactDistance = max(0.8, travel + 0.3)`, forward probe length `max(1.5, impactDistance)` (`:116-117`, constants `:29-32`).
  - If forward misses: left/right perpendicular probes 0.6 u accepted if hit < 0.36 u; forward-left/right diagonals 0.6 u accepted if < 0.42 u (`:126-160`).
  - Reflect horizontally `v' = v - 2(v.n)n`, normal flipped to face the projectile, new speed = speed * energyRetention, push out 0.3 u along normal, y velocity zeroed (`:59-89`, `:162-177`).
  - No collision meshes -> AABB fallback on `MINIMAP_BOUNDS` with 0.75 retention (`:242-281`).
  - Every bounce plays `kart.projectile-bounce` (`MultiplayerItems.tsx:327`).
- `MAX_BOUNCES = 5` (`MultiplayerItems.tsx:303`).

### 4.1 GREEN_SHELL (`MultiplayerItems.tsx:400-418`, config `:52-60`)
- Straight line at 80 u/s (2x kart top speed). Energy retention per bounce 0.75. Removed on the 5th bounce (`>= MAX_BOUNCES`). TTL 5 s.
- Render: y + 0.3, yaw += 8 rad/s.
- Colours `#00ff00` / inner `#087c36` / emissive `#00aa00`.
- Hit: `spin`.

### 4.2 RED_SHELL (`:420-452`, config `:61-69`)
- Target re-evaluated **every frame** as `getNearestPlayerAhead(ownerId)` (`ENG/store.ts:1943-1980`): among karts ahead of the **owner** (more laps, or same laps and higher `progressPercent`), the one closest in straight-line distance to the owner. If none (owner leading), it flies straight like a green shell.
- Homing: `calculateHomingDirection(pos, target, vel, turnRate=0.08, speed=90)` (`ItemPhysics.ts:293-333`): `v += (dirToTarget*speed - v) * 0.08`, renormalised to 90 u/s. **Per frame**, not per second (60 fps equivalent exponential rate = -ln(0.92)*60 = 5.0 /s).
- Retention 0.8, removed on the 8th bounce (`MAX_BOUNCES + 3`). TTL 5 s. Pure straight-line pursuit; no track spline following.
- Render yaw faces velocity `atan2(-vx, -vz)`.
- Colours `#ff0000` / `#8a1721` / `#aa0000`. Launch also plays `kart.warning` at gain 0.6 (`KART/sound.ts:171`).
- Hit: `spin`.

### 4.3 BLUE_SHELL (`:454-502`, config `:135-145`)
- Target `getFirstPlacePlayer()` (`store.ts:1923-1941`: laps desc then `progressPercent` desc, no checkpoint tiebreak). If the target is the owner, no homing.
- Homing turnRate 0.12 per frame (60 fps rate ~7.67 /s), speed 100 u/s.
- Altitude: `targetHeight = dist > 20 ? 4 : max(0.5, dist/5)`; `y += (targetHeight - y) * 0.05` per frame (`:474-475`, ~3.08 /s at 60 fps). Rendered at y + 0.5, roll wobble `sin(t*10)*0.1`, glow scale `1 + sin(t*8)*0.2` (`:492-500`).
- Wall probes run at item y + 0.3, so while cruising at y~4 it can pass over low barriers.
- Retention 0.9, removed on the 10th bounce (`MAX_BOUNCES + 5`). TTL 5 s, **no explosion on timeout** (only bombs explode on timeout).
- Hit test is the generic 2D 1.5 u test: it detonates on the **first kart it touches**, not necessarily the leader, and altitude does not protect anyone. Explosion radius 10 (2D).
- Colours `#0066ff` / `#174188` / `#0033aa`; has glow sphere (opacity 0.4). Launch plays `kart.warning`.

### 4.4 BANANA (`:560-568`, config `:70-78`)
- Spawned 2 u **behind** the kart at kart y, zero velocity. Stationary; yaw += 2 rad/s; rendered y + 0.3.
- TTL 5 s (then despawns silently). Owner immune for 0.5 s only.
- Snapshot age written every 100 ms of age (`:565-567`) so host handoff restores it.
- Colours `#ffff00` / `#9f6d08` / `#aaaa00`. Spawn plays `kart.hazard-drop` (`KART/sound.ts:169`).
- Hit: `spin`.

### 4.5 BOB_OMB (`:504-539`, config `:125-134`)
- Spawn 2 u ahead, y + 1, velocity (forward*30, 15, forward*30).
- Arc `calculateBombArc` (`ItemPhysics.ts:364-391`): gravity -20 u/s^2, `y += vy*dt + 0.5*g*dt^2`; grounded when `y <= 0.5` (absolute world y; physics plane is y=0, `KART/tracks/catalog.ts:29-37`). Flight from y=1: lands at ~1.53 s, ~46 u downrange, apex ~6.6 u.
- Wall bounce `calculateBombBarrierBounce` (`ItemPhysics.ts:187-237`): forward probe only, `impactDistance = max(0.6, travel+0.3)`, probe `max(1.2, impact)`, retention 0.5, vy preserved. "Explode on hard impact" when `bounceCount >= 2 && hSpeed > 15` (`:215`) is effectively unreachable (30 -> 15 -> 7.5 after two bounces).
- Explodes when grounded and age > 0.5 s (`MultiplayerItems.tsx:522-526`), on any kart within 1.5 u horizontally during flight, or at TTL 5 s.
- Explosion radius 8 (2D). Owner can be caught by own blast (`:229`).
- Render: tumble x += 5 rad/s, z += 3 rad/s; glow pulse `1 + sin(t*(3+2t))*0.3` (speeds up with age) (`:528-537`).
- Colours `#111111` / `#6d1c19` / emissive `#ff4400`, glow sphere. Launch plays `kart.projectile-launch` only (`kart.warning` is red/blue only, `sound.ts:171`); the visual-event bus marks bomb/red/blue launches as `danger` intensity 0.88 (`app/src/legacy/games/kart/kartRaceVisualEvents.ts:76-86`).

### 4.6 MUSHROOM (`useItemAction.ts:51-60`)
- `boostMultiplier 2`, `boostTimeRemaining 2 s`, `activeBoost 'MUSHROOM'`, `isBoosting true`.
- Speed effect, see 5.1: +40 u/s on the max-speed target for 2 s (80 u/s cap). Only felt while gas is held (target speed is 0 when coasting, `ENG/constants.ts:75-77`).

### 4.7 TRIPLE_MUSHROOM (`useItemAction.ts:62-71`)
- **One** boost: multiplier 2 for **5 s**, `activeBoost 'TRIPLE_MUSHROOM'`. Not three charges.

### 4.8 STAR (`useItemAction.ts:73-85`)
- `setInvincible(true, 8000)` (`store.ts:1864-1880`), boost x1.5 (+20 u/s) for 8 s, `activeBoost 'STAR'`, screen effect `STAR_RAINBOW` 8000 ms.
- Invincible: `applyItemEffect` returns early (`MultiplayerItems.tsx:165-168`), Lightning and Blooper skip invincible karts (`useItemAction.ts:90, 105`). No ram/knockback on contact with other karts (no code path).
- Visual: yellow sphere r=1.2 at y 0.5, opacity 0.3, hidden during Bullet Bill (`ENG/MultiplayerKart.tsx:1697-1713`); rainbow viewport overlay (`RaceUI.tsx:287-295`: 45deg rainbow gradient at alpha 0.15, 400% size, 0.5 s linear infinite shift, 6 px rainbow border).
- Expiry check per frame (`MultiplayerKart.tsx:976-979`).

### 4.9 LIGHTNING (`useItemAction.ts:87-100`)
- Every other kart that is not invincible (finished karts are **not** excluded): `shrinkPlayer(id, 5000)` (`store.ts:1828-1844`), `boostMultiplier 0.5`, `boostTimeRemaining 5`, `speed = max(0, speed*0.3)`.
- Shrink is **visual only**: kart group scale 0.5 (`MultiplayerKart.tsx:1669, 1675`); collision radius, pickup and hit radii unchanged. Expiry `MultiplayerKart.tsx:981-984`.
- Sound: `kart.lightning` when the owner spends it (`KART/sound.ts:158-159`) and again per victim on the `isShrunk` edge (`sound.ts:156`).

### 4.10 BLOOPER (`useItemAction.ts:102-110`)
- Every other non-invincible kart: `applyScreenEffect('INK_SPLATTER', 4000)`.
- Pure presentation: per-viewport CSS overlay of 5 black radial gradients (alpha 0.9..0.7 at (30%,20%), (70%,30%), (50%,70%), (20%,60%), (80%,80%)) with a 0.5 s `inkDrip` in (`RaceUI.tsx:275-282, 298-303`). No gameplay change. Expiry `MultiplayerKart.tsx:986-989`.
- Sound `kart.ink` on owner use (`sound.ts:160`) and per victim on effect edge (`sound.ts:157`).

### 4.11 BULLET_BILL (`useItemAction.ts:112-122`)
- `activateBulletBill(6000)` (`store.ts:1882-1901`): `isBulletBill` + `isInvincible` for 6 s; boost x2.5 (+60 u/s, cap 100) for 6 s, `activeBoost 'BULLET_BILL'`.
- Autopilot (`MultiplayerKart.tsx:996-1042`): forced throttle, no brake/drift; `steer = clamp((angleToWaypoint(nearest+3) - yaw)*2.5, -1, 1)`; if `getNearestPlayerAhead` is within 30 u, blend toward it with `blend = 1 - d/30` and gain 3. Running into karts is just the normal kart-kart bump: **no hit effect is applied to them**.
- Visual: black capsule r 0.6 len 1.5 at y 0.5, rotated PI/2 about Z (lies across the kart, likely meant along -Z), `#111111`, emissive `#444444` 0.3, metal 0.8, rough 0.3 (`MultiplayerKart.tsx:1677-1695`); nameplate hidden (`:1770`).
- Audio: engine voice stopped, `kart.rocket-loop` while active, `kart.boost` on start (`KART/sound.ts:122, 144, 151`).
- Expiry `MultiplayerKart.tsx:971-974` (`deactivateBulletBill` also clears invincibility).

---

## 5. Effects on karts

### 5.1 Speed composition (where boost multipliers land)

`ENG/MultiplayerKart.tsx:1091-1175`:
```
boostTimeRemaining -= dt; at <= 0 -> boostTimeRemaining 0, activeBoost null, boostMultiplier 1   (:1095-1106)
powerupBoost = boostMultiplier > 1 ? 40 * (boostMultiplier - 1) : 0                              (:1108)
maxSpeed = (40 + (driftTurbo > 0 ? 40 : 0) + powerupBoost + rubberBand + slipstream) * (onDirt ? 0.75 : 1)   (:1155-1161)
target = brake ? -15 : gas ? maxSpeed : 0                                                        (constants.ts:75-77)
speed = damp(speed, target, lambda, dt)  lambda: brake 6, gas 1.8, coast 1.1                    (:1170-1175, constants.ts:15-19)
isBoosting = maxSpeed > 40                                                                        (:1614)
```
Single boost slot: any later writer overwrites `boostMultiplier/boostTimeRemaining/activeBoost` (boost pads, drift exits, start boost, item hits). Example: a boost pad during Star replaces x1.5/8 s with x1.8/1.5 s, after which the Star speed is gone (invincibility stays).

Boost table:

| Source | Multiplier | Duration s | +u/s on cap | activeBoost | Ref |
|---|---|---|---|---|---|
| Mushroom | 2.0 | 2 | +40 | `MUSHROOM` | `useItemAction.ts:52-57` |
| Triple boost | 2.0 | 5 | +40 | `TRIPLE_MUSHROOM` | `:63-68` |
| Star | 1.5 | 8 | +20 | `STAR` | `:76-81` |
| Bullet Bill | 2.5 | 6 | +60 | `BULLET_BILL` | `:114-119` |
| Boost pad | 1.8 | 1.5 | +32 | `BOOST_PAD` | `BoostPads.jsx:19-20, 294-299` |
| `boost` hit effect (unused by any item) | 2.0 | 2 | +40 | `MUSHROOM` | `MultiplayerItems.tsx:188-194` |

### 5.2 Hit effects (`applyItemEffect`, `ENG/MultiplayerItems.tsx:160-212`)

| Effect | Used by | Writes | SFX | FX bus |
|---|---|---|---|---|
| `spin` | Green, Red, Banana | `speed 0`, `boostMultiplier 0.3`, `boostTimeRemaining 1.5` | `kart.projectile-hit` (from hit path) + `kart.spinout` | `projectile-hit` + `spinout` at victim |
| `explode` | Bomb, Blue (every kart in radius) | `speed 0`, `boostMultiplier 0.2`, `boostTimeRemaining 2.5`, screen `EXPLOSION_FLASH` 500 ms | `kart.explosion` once per blast | `explosion`, playerId null |
| `slow` | (Lightning applies its own equivalent inline) | `boostMultiplier 0.5`, `boostTimeRemaining 3` | - | - |
| `blind` | (Blooper applies inline) | `INK_SPLATTER` 4000 ms | - | - |
| `stop` | none | `speed 0` | - | - |

**Critical fidelity finding (what the web build actually does):**
1. The `speed: 0` / `speed * 0.3` writes go to the store, but kart physics integrates its own `speedRef` (`MultiplayerKart.tsx:236`, `:1175`) and never reads store speed back; the next store write overwrites it (`:1632`). Item hits therefore do **not** stop the kart.
2. Multipliers < 1 are ignored for speed (`:1108` only adds when > 1). Their only effect is to **cancel any active positive boost** for the duration (1.5 s spin, 2.5 s explode, 5 s lightning).
3. There is **no spin animation** of the kart (no yaw spin, no input lock). "Spin-out" = sound + spark burst + small camera shake.

So today an item hit is mostly cosmetic plus boost cancellation. Decide explicitly for the port (recommendation in Upgrade ideas: implement the intended stop + spin). If a strict 1:1 port is wanted, reproduce: boost cancel only.

Screen overlays per player viewport (`RaceUI.tsx:265-354`): `EXPLOSION_FLASH` = radial gradient `rgba(255,200,100,.95)` -> `rgba(255,100,0,.8)` 30% -> transparent 70%, keyframes scale 0.5 -> 1.2 (30%) -> 1, opacity 1 -> 0 over 0.5 s.

### 5.3 Counters and immunities
- Invincible (Star 8 s, Bullet Bill 6 s): immune to projectile effects, explosions, Lightning, Blooper. Still consumes the projectile it touches.
- Owner grace: 0.5 s after spawn (`MultiplayerItems.tsx:583`). After that, own bananas/shells/bombs hit the owner.
- Explosions hit everyone in radius, including owner and invincible check per victim.
- No shield, no trailing/held-behind item, no item-vs-item cancellation, no dodge mechanic. The jump/hop does not avoid items (hit test is 2D).
- Status effects are not cleared when a kart finishes (finished karts can be shrunk/inked; SFX is suppressed for finished karts `kartAudioEvents.ts:12`).
- `resetRace` clears all items and status effects (`store.ts:1601-1680`).

### 5.4 Status timers
Absolute `Date.now()` end times (`store.ts:176-184`), checked each frame on the host (`MultiplayerKart.tsx:969-989`). Snapshot stores remaining ms and restores them (`store.ts:2215-2262`, `:2320-2420`), active items restored with `ageMs` (`:2389-2417`). Note: `Date.now()`-based timers keep running during pause, unlike projectile age.

---

## 6. Boost pads (item-adjacent)

`ENG/BoostPads.jsx`, trigger math `KART/boostPad.ts:2-13`.

| Constant | Value | Ref |
|---|---|---|
| Boost | x1.8 for 1.5 s (`BOOST_PAD`) | `BoostPads.jsx:19-20` |
| Per-pad, per-player cooldown | 2.0 s (render clock) | `:21`, `:223-224` |
| Trigger padding | +1.5 u on each half-extent | `:22`, `:221` |
| Ground snap | downward raycast from y=50 (far 100) after 500 ms; prefers meshes named ground/track/dirt | `:105-142`, `:192-206` |
| Emissive pulse | `0.35 + 0.18*sin(t*2.4 + index)` | `:208-212` |
| Rim | yellow box rails 0.12 thick, 0.08 high; three cone arrows r 0.34 h 0.7 at x = -0.24w, 0, +0.24w | `:251-267` |
| Texture | 128x128 canvas, orange gradient `#ff9a00`->`#ff8800`->`#dd4400`, diagonal bands, yellow border, upward arrow `#ffe68a`->`#ffcc00` | `:42-100` |

`isInsideBoostPad`: local frame where -Z is forward, +X across; `across = dx*cos(r) - dz*sin(r)`, `along = dx*sin(r) + dz*cos(r)`; inside if `|across| <= w/2 + pad && |along| <= l/2 + pad`.

Host checks all karts; non-host only its local kart (`BoostPads.jsx:216-221`).

Placements:
- Violet Loop (`KART/tracks/catalog.ts:42-47`), all w=8, l=4: (205.34, -91.97) rot -0.37; (262.08, -36.32) rot -4.42; (242.92, 27.15) rot -1.52; (110.18, 121.33) rot -3.99. (position = [x, z], rot = three.js yaw rad)
- Sunset Harbor (`KART/tracks/harbor.ts:52-55`), w=12, l=6, rot = spline yaw at t = 6.2/29, 14.4/29, 19.5/29: (78.93, -224.47) yaw -2.136; (281.98, -44.38) yaw 2.089; (137.94, 148.69) yaw 1.152.

---

## 7. VFX per item

All item meshes are procedural primitives (`MultiplayerItems.tsx:234-263`): shell = sphere r 0.3 (16x12); banana = tube along quadratic bezier (0,-.2,0)-(.15,0,0)-(0,.2,0), radius 0.08; bomb = sphere r 0.4; squid = sphere r 0.3; bullet = capsule; mushroom = cylinder (never spawned in world). Outer material: MeshPhysical, emissiveIntensity 0.32, metalness 0.3, roughness 0.4, clearcoat 0.34, clearcoatRoughness 0.22; inner pass: MeshStandard roughness 0.56 metalness 0.08 at scale 0.76; glow (blue shell, bomb only): Basic, opacity 0.4, shell sphere at scale 1.5 (`:271-281`, `:608-635`). Materials are shared per type and precompiled during the intro (`:643-666`) with the dispose-after-warmup guard of `KART/itemWarmup.ts:26-49` (UE: PSO precache / Niagara warmup, see 10).

Impact FX bus `KART/scene/kartFxBus.ts`:

| Kind | Shake (m) | Flash | Flash colour | Burst x | Ref |
|---|---|---|---|---|---|
| `projectile-hit` | 0.34 | 0.22 | `#fff1dc` | 1.0 | `:32` |
| `explosion` | 0.55 | 0.32 | `#ffb070` | 1.6 | `:33` |
| `spinout` | 0.14 | 0 | - | 0.5 | `:34` |
| `wall` | 0.20 | 0 | - | 0.6 | `:35` |
| `bump` | 0.12 | 0 | - | 0.4 | `:36` |

- Shake envelope `peak * exp(-6.5 t)`, cut below 1 mm (`:39`, `:49-53`); offset = sums of incommensurate sines, x/y(0.7)/roll(0.06) (`:73-79`); per-player keys, `null` player = all views at 50% (`:91-125`). Flash `peak*(1-t)^2` over 0.28 s (`:40`, `:56-60`), own player only (explosion is emitted with `playerId: null`, so its bus flash is never shown; victims get the CSS `EXPLOSION_FLASH` instead).
- Burst renderer `ENG/misc/KartFxLayer.tsx:57-120` at ground + 0.6: sparks `34*burst`, speed 5-13, life 0.25-0.55, size 0.07-0.13, colour `#ffe2a0` (heavy) / `#fff4dc` -> `#ff5a1f`; smoke `12*burst`, life 0.7-1.3, size 0.5-0.9 -> 3, `#57514f` (heavy) / `#c9c4bd`; heavy kinds (explosion, projectile-hit) add fire `18*burst` (`#ffd27a` -> `#ff3d1f`, life 0.2-0.45, size 0.5-0.9 -> 1.8) and one ground shock ring (life 0.45, size 1.2 -> 9 explosion / 5 hit, `#ffcf8a` -> `#ff6a2a`). Pools: smoke 1024, fire 512 additive, sparks 768 additive (gravity -9, stretch 1.4), rings 16 (`:268-274`). Disabled on quality tier `low` (`MultiplayerApp.tsx:209, 404`).
- Continuous boost flames (any `isBoosting`, i.e. all item boosts): `kartContinuousFx` (`KART/scene/kartTrails.ts:23-30`), exhaust anchors `kart-exhaust-left/right` (`KartFxLayer.tsx:22-32`, rig fallback `kartTrails.ts:45-62`); 70 particles/s per pipe, fire `#fff0b8` -> `#ff4a1c`, speed 3-5.5 backwards, life 0.12-0.24, size 0.28-0.42 -> 0.35, plus 1 spark per emission (`KartFxLayer.tsx:159-194`).
- Visual event bus (`app/src/legacy/games/kart/kartRaceVisualEvents.ts:76-110`): new item = `danger` 0.88 for bomb/red/blue, `impact` 0.48 otherwise; screen effect start `danger` 0.76; shrink `danger` 0.6; boost start `combo` 0.62-0.8.
- Driver avatar raises the right arm on item use (edge when held item disappears) (`ENG/models/Kart.jsx:453-457`, `app/src/games/avatar/drivingExpression.ts:184-189`: upper-arm pitch 2.15, roll 0.52, decay 2.6/s).

Per item summary:

| Item | World VFX | Screen VFX |
|---|---|---|
| Item box | spinning/bobbing gold cube + glow + "?" decals | roulette widget |
| Green/Red shell | coloured sphere, spin or face-velocity; burst on hit | hit flash `#fff1dc` 0.22, shake 0.34 |
| Blue shell | blue sphere + pulsing glow, altitude + wobble; explosion burst + ring (size 9) | all views shake 0.275 (50% of 0.55); victims `EXPLOSION_FLASH` |
| Banana | yellow tube, slow spin; burst on hit | hit flash |
| Bomb | black sphere tumbling, accelerating glow pulse; explosion burst | as blue |
| Mushroom / Triple | exhaust flames while boosting | - |
| Star | yellow translucent sphere + exhaust flames | `STAR_RAINBOW` 8 s |
| Lightning | victims scale 0.5 (no bolt VFX) | - |
| Blooper | none in world | `INK_SPLATTER` 4 s on victims |
| Bullet Bill | black capsule over kart, flames, nameplate hidden | - |
| Boost pad | pulsing emissive pad + flames on kart | - |

---

## 8. SFX per item

Cue table `app/src/audio/kartCues.ts:15-61` (category `sfx`, files in `app/public/audio/`). Mixer: per-kart pan by spawn lane (`laneToPan`), gain / sqrt(playerCount) (`KART/sound.ts:93-108`, `ENG/kartAudioEvents.ts:13-18`). Event cues only on host, not paused, racing, kart not finished.

| Cue | File (bytes) | Gain | Throttle ms | Loop | Trigger | Ref |
|---|---|---|---|---|---|---|
| `kart.item-pickup` | kart-item-pickup.wav (57644) | 0.45 | 65 | - | `isRollingItem` false->true | `kartCues.ts:43`, `sound.ts:153` |
| `kart.item-roulette` | kart-item-roulette.wav (292364) | 0.16 | 0 | yes | while rolling | `:44`, `sound.ts:142` |
| `kart.item-ready` | kart-item-ready.wav (59804) | 0.45 | 65 | - | rolling ends with item | `:45`, `sound.ts:154` |
| `kart.projectile-launch` | kart-projectile-launch.wav (43244) | 0.45 | 65 | - | new non-banana active item | `:46`, `sound.ts:169` |
| `kart.hazard-drop` | kart-hazard-drop.wav (31724) | 0.45 | 65 | - | new banana | `:49`, `sound.ts:169` |
| `kart.warning` | kart-warning.wav (51884) | 0.34 (x0.6) | 850 | - | red/blue shell launched (also wrong-way) | `:57`, `sound.ts:171` |
| `kart.projectile-bounce` | kart-projectile-bounce.wav (25964) | 0.45 | 65 | - | each wall bounce (shell, bomb) | `:47`, `MultiplayerItems.tsx:327, 357` |
| `kart.projectile-hit` | kart-projectile-hit.wav (57644) | 0.45 | 65 | - | non-explosive item hits kart | `:48`, `MultiplayerItems.tsx:597` |
| `kart.spinout` | kart-spinout.wav (100844) | 0.45 | 65 | - | `spin` effect applied | `:50`, `MultiplayerItems.tsx:174` |
| `kart.explosion` | kart-explosion.wav (129644) | 0.65 | 65 | - | bomb/blue detonation | `:51`, `MultiplayerItems.tsx:217` |
| `kart.lightning` | kart-lightning.wav (100844) | 0.45 | 65 | - | owner uses Lightning; each victim shrink edge | `:52`, `sound.ts:156, 159` |
| `kart.ink` | kart-ink.wav (50444) | 0.45 | 65 | - | owner uses Blooper; each victim ink edge | `:53`, `sound.ts:157, 160` |
| `kart.invincible-start` | kart-invincible-start.wav (100844) | 0.45 | 65 | - | Star start (not Bullet) | `:54`, `sound.ts:155` |
| `kart.invincible-loop` | kart-invincible-loop.wav (247004) | 0.12 | 0 | yes | invincible and not Bullet | `:55`, `sound.ts:143` |
| `kart.rocket-loop` | kart-rocket-loop.wav (280124) | 0.20 | 0 | yes | Bullet Bill active (engine voice muted) | `:56`, `sound.ts:122, 144` |
| `kart.boost` | kart-boost.wav (100844) | 0.45 | 65 | - | boost start or activeBoost change (non-drift); Bullet start | `:36`, `sound.ts:146-151` |
| `kart.boost-loop` | kart-boost-loop.wav (280124) | 0.17 | 0 | yes | `isBoosting` and not Bullet | `:37`, `sound.ts:141` |

Drift-exit boosts use `kart.drift-boost-1/2/3` instead of `kart.boost` (`KART/soundMix.ts:96-101`) - covered by the driving spec.

Phones get **no haptic** for item hits (only barrier/kart collisions send `collision` events, `MultiplayerKart.tsx:1493-1530`).

---

## 9. Server-side / authoritative logic

- `partykit/src/engine.ts` contains **no** item, box, odds or hazard logic (searched; only kart map selection and cosmetics). It relays `game-event` messages (`:739-771`): spectators/eliminated players rejected, `HOST_ONLY_GAME_EVENTS` (`protocol/src/constants.ts:151`) reserved for the host, `player.actions += 1`, broadcast to every other session with `fromPlayerId`.
- Rate limit: control channel 40/s with burst x2 per session (`partykit/src/rateLimiter.ts:15-23`).
- All item authority is the host TV's `useGameStore` (zustand). Snapshot to PartyKit via `client.saveSnapshot` (`app/src/games/legacyBridge.ts:381-404`) carries held items, roll elapsed, status remaining ms and active items (`ENG/store.ts:2215-2262`) for host restore.

---

## 10. UE mapping (recommended data-driven design, UE 5.8 C++)

### 10.1 Coordinates
three.js (x, y, z), Y-up, right-handed, kart forward `(-sin yaw, 0, -cos yaw)` -> UE `X = -z * S`, `Y = x * S`, `Z = y * S` with `S = KART_UNITS_TO_UU` (100). Yaw: `UE_YawDeg = -RadiansToDegrees(threeYaw)`. Speeds scale by S (shell 80 u/s -> 8000 cm/s). Radii (1.5, 2.5, 8, 10 u) scale by S. Keep all gameplay numbers in data assets in **source units** and convert in one place, or convert once at import; do not mix.

### 10.2 Classes

```
UKartItemDefinition : UPrimaryDataAsset          // one asset per item, PrimaryAssetType "KartItem"
  FGameplayTag ItemTag;  FText DisplayName; TSoftObjectPtr<UTexture2D> Icon; FLinearColor HudColor;
  EKartItemUseKind UseKind;     // SelfBoost, SelfTransform, FieldEffect, Projectile, DroppedHazard, LobbedProjectile
  // spawn
  TSubclassOf<AKartItemProjectile> ProjectileClass; float SpawnOffset (2 / -2); float SpawnHeight (0 / 1);
  float Speed; float InitialVerticalSpeed (15 bomb); float Lifetime (5); float OwnerGraceTime (0.5);
  // motion
  int32 MaxBounces (5/8/10); float BounceEnergyRetention (0.75/0.8/0.9/0.5 bomb);
  EKartHomingMode Homing (None, NearestAheadOfOwner, Leader); float HomingRatePerSecond; bool bAimAtTargetOnSpawn;
  float CruiseAltitude (4), AltitudeNearDivisor (5), AltitudeFarDistance (20), AltitudeRatePerSecond;
  float Gravity (-20), GroundedHeight (0.5), MinFuseBeforeGroundDetonation (0.5);
  float HitRadius (1.5); float ExplosionRadius (0 / 8 / 10); bool bExplodeOnTimeout;
  // effects
  TObjectPtr<UKartStatusEffectDefinition> HitEffect;   // Spin / Explode
  TObjectPtr<UKartStatusEffectDefinition> SelfEffect;  // Mushroom, Star, Bullet
  TObjectPtr<UKartStatusEffectDefinition> FieldEffect; // Lightning, Blooper (applied to all others)
  // presentation
  TSoftObjectPtr<UNiagaraSystem> HeldFx, TrailFx, ImpactFx; TSoftObjectPtr<UStaticMesh> Mesh;
  TSoftObjectPtr<USoundBase> UseSound, LoopSound, BounceSound, HitSound; bool bWarnOnLaunch;

UKartStatusEffectDefinition : UPrimaryDataAsset
  float Duration; float BoostMultiplier; bool bOverridesBoostSlot (true = faithful single slot);
  float SetSpeedTo (-1 = untouched); float SpeedScale (1); bool bInvincible; bool bAutopilot;
  float VisualScale (1 / 0.5); EKartScreenEffect ScreenEffect (None, Ink, ExplosionFlash, StarRainbow); float ScreenEffectDuration;
  float SpinYawDegrees (0 faithful; see upgrades); bool bLockInput;

UKartItemOddsTable : UPrimaryDataAsset
  float FrontMaxRatio 0.2; float MidMaxRatio 0.55; float MidMaxTrailing 0.55; float TrailingDivisor 120; int32 LapScore 100;
  TArray<FKartItemPoolRow> Rows;  // {Bracket, MinPlayers, MaxPlayers, TArray<FKartItemWeight{Item, Weight}>}
  bool bLeaderOfTwoForcesFront = true;
```
Populate `Rows` from section 2.3 exactly (weights = entry counts). Use a seeded `FRandomStream` on the authority so replays/tests are deterministic.

Kart-side components (on the kart pawn, authority only mutates; replicate if online play is ever added):
- `UKartItemComponent`: `HeldItem`, `bRolling`, `RollEndTime` (gameplay timer 3.0 s = 1.8 + 1.2, not UI-driven), `TryUseItem()` (guards from `useItemAction.ts:39-41`), `GiveItem()`. Broadcasts `OnItemRollStarted / OnItemReady / OnItemUsed` for UMG and audio.
- `UKartStatusComponent`: active effects with end times on the **game clock** (pauses with the race; the web uses wall clock), `IsInvincible()`, boost slot (`Multiplier`, `Remaining`, `SourceTag`) feeding the kart movement component's max-speed formula in 5.1, visual scale, screen effect. Apply rules from 5.2/5.3. GAS is an option, but a single-slot boost and ~8 effects fit a small custom component better for a 4-player couch game.
- `UKartRaceRankSubsystem` (world subsystem): `GetRacePosition`, `GetNearestAhead(owner)`, `GetLeader()`, progress score; port the three distinct sort rules from `store.ts:1923-2001` exactly.

World actors:
- `AKartItemBox`: `USphereComponent` radius 2.5 u (overlap pawns, ignore Z by checking 2D distance in the handler for fidelity), static mesh + decals, `TMap<TWeakObjectPtr<AKartPawn>, float> CollectedAt`, respawn 15 s per player, hidden when all players collected; rotating movement component (1.5 rad/s) + bob in material WPO or timeline. Placed by an `AKartItemBoxRow` that samples the track spline at `t` with lateral offsets.
- `AKartItemProjectile` (base): custom planar movement in `Tick` with `UE::Math` sweeps against a dedicated trace channel `ECC_KartItemBlocker` (track barriers Block; karts, boxes, pads, decorations Ignore - mirrors `itemCollisionMeshes.ts`). A small sphere sweep (r ~0.3 u) replaces the 5-ray probe fan; reflect horizontal velocity, scale by retention, push out 0.3 u. Clamp step to 1/30 s and skip while paused (`itemLifetime.ts`). Convert per-frame lerps to per-second exponentials (`alpha = 1 - exp(-rate*dt)`, rate 5.0 red, 7.67 blue homing, 3.08 blue altitude) so behaviour is frame-rate independent. Subclasses: `AKartShellProjectile` (green/red/blue via data), `AKartLobbedProjectile` (bomb arc), `AKartDroppedHazard` (banana). Avoid `UProjectileMovementComponent` homing: its acceleration model differs from the source lerp and it bounces in 3D.
- Explosions: `UKartExplosionLibrary::Explode(Center, Radius)` doing a 2D radius test over karts (source ignores height), applying `HitEffect` to each, spawning `NS_Kart_Explosion`, camera shake for all views at 50%.
- `AKartBoostPad`: box overlap = pad extents + 1.5 u padding, per-player 2 s cooldown, applies `SE_BoostPad` (x1.8, 1.5 s); ground snap by line trace at BeginPlay.

### 10.3 Input
- Enhanced Input `IA_KartUseItem` (keyboard E / Left Shift, gamepad face button) -> `UKartItemComponent::TryUseItem`.
- Phone path: whatever bridge the networking spec chooses for PartyKit `kart-input` events, map `{ useItem: true }` to the same call, keyed by `fromPlayerId` -> local player/pawn. Keep a 200 ms per-player de-bounce (the legacy path's `ITEM_RATE_LIMIT_MS`).
- Bots: `UKartBotItemPolicy` (fire 1.2-3.7 s after ready).

### 10.4 Presentation
- Niagara (one system per row of section 7): `NS_ItemBox_Idle`, `NS_ItemBox_Collect`, `NS_Shell_Trail_{Green,Red,Blue}`, `NS_Bomb_Fuse`, `NS_Kart_Explosion` (sparks + fire + smoke + ground ring, burst scale 1.6), `NS_Kart_ProjectileHit` (burst 1.0), `NS_Kart_Spinout` (0.5), `NS_Kart_BoostFlame` (attach to exhaust sockets `exhaust_l/r`, 70/s per pipe), `NS_Kart_StarAura`, `NS_BulletBill_Trail`. Expose `BurstScale` as a user parameter so `KART_FX_PROFILES` maps 1:1. Warm up / PSO-precache item materials and Niagara systems during the race intro (UE analogue of `itemWarmup.ts`).
- Camera shake: one `UCameraShakePattern`-based asset per FX kind (peak from the profile table, exponential decay 6.5/s), played on the victim's `APlayerCameraManager`; explosions on all local players at 0.5 scale.
- Screen effects: per-player post-process blendables on each split-screen camera (dynamic material instances): `PP_InkSplatter` (4 s, 0.5 s drip-in), `PP_ExplosionFlash` (0.5 s), `PP_StarRainbow` (8 s). UMG would also work but post-process keeps them in the 3D view and per viewport.
- UMG: `WBP_ItemSlot` per player viewport (roulette strip of 35 icons, 1.8 s decel + 1.2 s hold), `WBP_ItemActivation` popup (0.4 s in, out at 1.2 s), top-centre item rail (`KartHud`).
- Audio: import each `kart-*.wav` from section 8 as `USoundWave`, wrap in MetaSound sources with the source gain and a throttle (use concurrency assets: e.g. `SCON_Kart_65ms`). Loops (`roulette`, `boost-loop`, `invincible-loop`, `rocket-loop`) as `UAudioComponent`s on the kart driven by status edges. Lane pan + 1/sqrt(n) gain can be done with 2D sounds and a per-kart pan parameter, matching the host TV mix.

### 10.5 Faithful-port checklist (decide each one)
1. Hits do not stop the kart and multipliers < 1 only cancel boosts (5.2). 2. Triple boost = one 5 s boost. 3. Bananas expire after 5 s. 4. Box lock is consumed even when you already hold an item. 5. Blue shell hits the first kart it touches; altitude gives no immunity. 6. Bomb "hard wall" detonation unreachable. 7. Single boost slot overwritten by pads. 8. Lightning shrink is visual only. 9. Bullet Bill does nothing to karts it rams. 10. Wall-clock status timers vs paused-clock item age. 11. Bullet Bill capsule orientation. 12. HUD glyph spoils the roulette.

---

## 11. Upgrade ideas (optional, NOT part of the faithful spec)

Feel and fixes:
- Make hits real: `SE_Spin` = speed x0.1, 720 deg yaw spin over 1.0 s with input lock 0.8 s, then 1.0 s invincibility; `SE_Explode` = launch the kart up 2 m with a tumble, 1.6 s lockout. Penalty multipliers actually cap speed.
- Triple boost as 3 charges (one tap each, 1.2 s each), shown as three icons orbiting the kart.
- Hold-to-trail: press and hold ITEM to drag a shell/banana behind as a shield (the existing hints already promise this); release to throw. Tilt the phone back while releasing to throw backwards.
- Red shell follows the track spline (then homes in the last 15 m); blue shell flies along the spline at altitude and dives on the leader only, with an on-screen incoming indicator and a rising `kart.warning` pitch.
- Lightning shrinks the collision capsule and lets full-size karts flatten shrunk ones; a bolt Niagara strike per victim.
- Box visibility per viewport in split screen (hide a box only in views of players who cannot use it), respawn on the game clock, with a pop-in burst.

Visuals:
- Proper meshes with Nanite, clearcoat shell materials, emissive trails (Niagara ribbons) colour-matched to the item, box with refractive glass and a slowly rotating holographic "?".
- Screen-space ink with fluid drips that run down with speed, and a wiper effect when the player shakes the phone (shake to clear faster: fits the motion controller).
- Explosion with a Niagara fluid fireball, ground scorch decal, and a short radial-blur post-process on nearby views.

Phone integration:
- Show the held item, roulette and incoming-shell warning on the phone screen; distinct haptic patterns per hit type (spin, explosion, ink).

New items that fit the set (all data assets on the same framework):
- Bubble shield: blocks one projectile or hazard for 10 s (a counter the set lacks).
- Oil slick: dropped hazard puddle (radius 3 u, 8 s) that removes grip instead of stopping you.
- Fake item box: dropped hazard that looks like a box with a subtle tell.
- Magnet: pulls you into the slipstream of the kart ahead for 3 s and steals its item if you touch it.
- Harbor-themed "Wave": rolls down the track behind you and pushes everyone it meets sideways (Sunset Harbor only, via a per-track odds override).
