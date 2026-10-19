# 04 — Game flow, networking / phone controllers, UI and audio

Scope: everything a **UE 5.8 room host** must implement to replace the web host (`/host/kart`) while
phones stay controllers. The source of truth is the movecade-v2 repo
(`C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2`, read-only); every claim cites `path:line`.
Where `protocol/README.md` and the code disagree, the code is quoted (the README is incomplete, see §12).

> **Status as of the 2026-09-23 QA audit:** GAMES-01, GAMES-02 and the "no race time cap" item have
> **all been fixed in the web code since then** (commits `0a177ebb`, `49ed3074`, `88a4cf49`, `92c2163b`).
> §7.4 says what the port must copy so it doesn't bring the bugs back.

---

## 0. Which code actually ships (live vs dead)

The kart that runs is the **legacy engine** under `app/src/legacy/Experience/kart/engine/`. It is wrapped by
a thin v2 adapter. The call chain:

```
app/src/host/HostSurface.tsx:414            <Canvas …>  (status != lobby)
 └ app/src/games/kart/KartCanvas.tsx:72      KartCanvas → selects map, then ConfiguredKartCanvas
    ├ app/src/games/legacyBridge.ts:261      useLegacyBridge(...)  ← ALL v2 networking for kart
    ├ @legacy/games/kart/KartRaceCanvas.tsx:116 → MultiplayerApp (lazy)       (3D scene)
    │   └ Experience/kart/engine/MultiplayerApp.tsx:375-411
    │       MultiplayerMobileControlBridge, KartAudio, KartRaceCamera, MultiplayerTrackScene,
    │       MultiplayerItems, KartLighting, KartFxLayer, AutoPilot/PromoCameraBridge
    └ @legacy/games/kart/KartRaceUI.tsx:192   (DOM HUD, only while !resultMode — KartCanvas.tsx:36)
        ├ KartRaceIntro.tsx  (TrackOpening → host GameIntro)
        └ RaceUI.tsx:1321 → SplitScreenOverlay, Minimap, RaceSpeed, KartHud, ItemRoulette,
                            PowerupActivationPopup, ScreenEffectOverlay, RaceResults
```

| File | Status | Notes |
|---|---|---|
| `app/src/games/kart/index.ts` | **live** | manifest: `tier:'premium'`, `simultaneous:true`, `sendHz:30`, `steering:'wheel'`, `scoreLabel:'Time (ms)'` (`index.ts:5-20`) |
| `app/src/games/kart/KartCanvas.tsx` | **live** | map selection + legacy bridge mount |
| `app/src/games/kart/KartController.tsx` | **live** | the phone UI (web) |
| `app/src/games/kart/TrackPicker.tsx` | **live** | lobby map picker |
| `app/src/games/kart/sound.ts`, `soundMix.ts`, `drivetrain.ts` | **live** | driven by legacy `KartAudio.tsx` |
| `app/src/audio/kartCues.ts`, `engineVoice.ts`, `audioService.ts`, `cues.ts`, `introCues.ts` | **live** | |
| `app/src/games/legacyBridge.ts` | **live** | the *only* v2↔legacy adapter: sensor frames, `kart-input`, score push, completion, countdown publish |
| `legacy/Experience/kart/engine/store.ts` | **live** | race state machine, laps, finish, deadline, pause |
| `legacy/.../hooks/useKartRaceNetworkSync.ts` | **partly live** | intro/countdown/deadline/lap/pause sync are live. The 30 Hz `kart-sync`/`kart-delta-sync` broadcast is **dead in v2**: `sendData` is replaced by `publishKartCountdown`, which ignores everything except `race-event countdown-start/race-start` (`legacyBridge.ts:71-83,357-360`) |
| `legacy/.../MultiplayerMobileControlBridge.tsx` | **partly live** | only the **steering** effect (`mobileData`, `:228-291`) is live. The GAS/DRIFT/REVERSE/ITEM effects (`:121-225`) read `isAccelerateActive` etc. from the v1 PartyKit store, which v2 never writes → **dead**. The pedals reach the kart via `legacyBridge.ts:318-335` → `store.syncKartInput` / `executeItemUse` |
| `legacy/.../RaceUI.tsx`, `KartHud.tsx`, `KartRaceIntro.tsx`, `kartItems.ts`, `raceHudLayout.ts`, `SplitScreenRenderer.tsx` | **live** | HUD |
| `legacy/.../KartRaceHUD.tsx` (1008 lines) | **dead** except `getDriftChargeStatus` (imported by `KartHud.tsx:8`) | |
| `legacy/.../RaceUI.tsx` `RaceResults` → legacy `GameOverPopup` | **transient** | visible only between local race `finished` and the room flipping to `complete` (then KartRaceUI unmounts). The real results screen is `app/src/host/GameOverScreen.tsx` |
| `legacy/.../App.jsx` (single player) | **DEV only** (`KartRaceCanvas.tsx:26-30`) | |
| `legacy/hooks/usePartyKitStore.ts` socket code | **dead** | `connect/disconnect/reconnect` replaced with no-ops (`legacyBridge.ts:347-352`) |
| `CheckpointDebugOverlay`, `KartRaceDebugPanel` in RaceUI | dead (flags `false`, `RaceUI.tsx:949,1009`) | |

---

## 1. Endpoints, environments, running locally

| What | Production | Staging | Local dev |
|---|---|---|---|
| PartyKit project | `movecade` (`partykit.production.json`) | `movecade-v2-staging` (`partykit.json`) | `partykit.json` via `npm run dev:party` |
| Party host | `party.movecade.com` | `party-v2.movecade.com` | `localhost:1999` |
| Socket URL | `wss://party.movecade.com/parties/main/r-<code lowercase>` | `wss://party-v2.movecade.com/parties/main/r-…` | `ws://localhost:1999/parties/main/r-…` |
| `MOVECADE_ENV` | `production` | `staging` | `staging` (same file) |
| Go API | `https://api.movecade.com` | (per deploy) | `http://localhost:8080` |
| Phone join base URL | `https://movecade.com` | the staging web deploy | the dev web origin |

Sources: `app/src/lib/config.ts:2-7,34-37`, `partykit.json`, `partykit.production.json`, `package.json:8`.

- **Run the room server locally:** `npm run dev:party`, which runs
  `partykit dev --config partykit.json --port 1999 --var SCORE_SIGNING_SECRET=local-dev-not-a-secret`
  (`package.json:8`). The worker 404s on `/`, so check it's ready with the port, not with HTTP (`playwright.config.ts:64-70`).
  A GET on any room path returns a readiness JSON `{ok, ready, mode, storage, reasons}` (`partykit/src/server.ts:207-223`).
- Run the web app (for phones or desktop controllers) with `npm run dev` on port 5173, listening on all interfaces (`vite.config.ts` `server:{host:true,port:5173}`).
  Point it at the party server with `VITE_PARTY_HOST=localhost:1999` (that is the dev default) and `VITE_API_URL=http://localhost:8080`.
- **Origin allow-list:** a socket with a non-null `Origin` header that isn't on the list is refused with
  `UNAUTHORIZED_ORIGIN` and closed with 4003 (`partykit/src/engine.ts:395-399`). The list is the production origins
  `https://movecade.com`, `https://www.movecade.com`, `https://play.movecade.com`, plus the dev origins `http://localhost|127.0.0.1:{5173,5174,5175,5176,5181,3000,4187}`
  in every non-production mode, plus anything in `EXTRA_ALLOWED_ORIGINS` (comma-separated) (`server.ts:46-75,145-148`).
  In production every origin must be `https://` and not local (`partykit/src/config.ts` `validateDeploymentConfig`).
- **Phones on a LAN:** a phone loading `http://192.168.x.x:5173` sends that as its Origin. It isn't allow-listed
  (add `--var EXTRA_ALLOWED_ORIGINS=http://192.168.x.x:5173`), **and iOS will not grant motion sensors on an insecure
  origin** (`docs/launch-phone-test.md` "Rig"). For real-phone tests, use staging or an HTTPS tunnel.
- **Cheapest local controller:** a desktop browser tab at `http://localhost:5173/join/<CODE>` in **assist mode**
  (touch steering slider, `docs/controller-assist-mode.md`; for kart, assist auto-holds GAS every 750 ms,
  `app/src/controller/ControllerSurface.tsx:417-426`). Its origin `http://localhost:5173` is allowed in dev.
- **Score signing:** in dev the secret is `local-dev-not-a-secret`, and unsigned rooms are allowed outside production.
  A production worker without a valid secret refuses every connection with `INTERNAL` + close 4100 (`server.ts:226-240`).

---

## 2. Transport, envelope, liveness (host-relevant subset)

- One WebSocket per client. Text frames carry JSON control messages; binary frames carry sensor data only (`protocol/README.md:11-17`).
- Envelope: a flat object with `v:2` and `t:<type>` (`README.md:47-70`). The JSON frame limit is **8192 bytes** (`constants.ts:129`).
  A wrong `v` gets `PROTOCOL_VERSION_MISMATCH` + close 4000 (`engine.ts:528-538`). Ignore unknown types and fields.
- **`hello` must be the first frame, within 10 s** (`HANDSHAKE_TIMEOUT_MS`, `constants.ts:66`; `engine.ts:420-430`).
- **Heartbeat:** every 10 s the server sends `{"v":2,"t":"ping","serverTime":…}` to every authenticated session, **including the host**.
  The host must reply `{"v":2,"t":"pong"}`. After 3 missed pongs the server closes with 4001 and runs `handleClose` (`engine.ts:2250-2273`).
  Any inbound frame resets the miss counter (`engine.ts:493-494`). The client may also send `{"t":"ping","nonce":n}` and gets back `{"t":"pong","nonce","serverTime"}` (`engine.ts:594-596`).
- **Rate limits** (per session, token bucket, 1 s window, 2× burst): control 40/s (burst 80), sensor 150/s (burst 300)
  (`constants.ts:119-125`). An exhausted control bucket closes the socket with `RATE_LIMITED`/4029 (`engine.ts:500-504`).
  The host's only steady control traffic is `score-update` (≤1/s), `save-snapshot` (≤4/s, server-throttled to 250 ms), `state-ack` per state frame, and `pong`.
- **Close codes** (`protocol/src/errors.ts`): 4000 ProtocolMismatch, 4001 HandshakeTimeout/liveness, 4003 Unauthorized, 4008 RoomFull,
  4029 RateLimited, 4100 RoomClosed, 4101 Replaced.
  Terminal (don't reconnect): 1008, 4000, 4003, 4008, 4100, 4101 (`client-core/src/transportPolicy.ts:28-35`). Any `error` with `fatal:true` also stops the loop (`client-core/src/sessionStore.ts:396-404`).
- **Reconnect ladder:** 250, 500, 1000, 2000, 4000, 8000 ms (capped), jittered over the lower half of each slot (`transport.ts:122`, `transportPolicy.ts:76-82`).
  Connect timeout 8 s (`transport.ts:123`).
  Queue control messages while down. **Drop "momentary" types older than 1.5 s** (`game-event`, `advance-turn`, `score-update`,
  `state-ack`, `host-lobby-visible`, `ping`, `pong`; `transportPolicy.ts:51-64`, `transport.ts:129,555-559`). Never queue binary.
- **stateVersion watermark:** drop any `state` whose `stateVersion` < the highest applied. Also advance the watermark on `welcome` and `host-changed`
  (`sessionStore.ts:351-385,406-418`). The web client acks each applied state with `{"t":"state-ack","stateVersion":n}` (`sessionStore.ts:384`).
  The ack is optional (used for delivery telemetry, `engine.ts:600-602`), but cheap.

---

## 3. Host identity, handshake, tokens

### 3.1 Room code and room id
- The **host mints the code itself**: 6 chars from `23456789ABCDEFGHJKMNPQRSTUVWXYZ`, uniform rejection sampling (`protocol/src/roomCode.ts` `generateRoomCode`; `constants.ts:31-33`).
  `roomId = "r-" + code.toLowerCase()` (`roomCode.ts` `roomIdForCode`). There is no create-room HTTP call: connecting to `/parties/main/r-xxxxxx` creates the room's durable object.
  The web host pins the code in its URL so a reload reconnects to the same room (`app/src/routes/HostRoute.tsx:46-58`). The UE host should persist `{code, hostToken}` across restarts the same way.
- A phone that joins a party room no host ever claimed gets `ROOM_NOT_FOUND` (fatal, 4100) (`engine.ts:983-987`). **Connect the host before showing the QR.**

### 3.2 `hello` (host)
The web host sends this (`client-core/src/sessionStore.ts:330-346`, options from `HostRoute.tsx:110-119`):

```json
{ "v":2, "t":"hello", "role":"host", "client":"web-host/2.0.0",
  "hostToken":"<from previous welcome, if any>", "gameId":"kart",
  "identity":"<lowercase v4 uuid>", "entitlementToken":"<JWS or omitted>" }
```

Field rules (`protocol/src/messages.ts:50-79, 571-611`):
- `client` ≤ 64 chars. Use something like `"ue5-host/0.1.0"`.
- `hostToken` and `joinToken` must pass `isToken` (≤128 chars, `MAX_TOKEN_LENGTH`).
- `gameId` is a slug ≤ 32 chars. `"mariokart"` is still normalised to `kart` server-side (`engine.ts:520-523`), but always send `kart`.
- `identity` must be a v4 UUID and is lower-cased by the parser. It is **required for premium entitlement checks** (token `sub` must equal it, `engine.ts:1800-1811`, `protocol/src/entitlements.ts:292`).
- `mode:"public"` is only for the Live Arcade. Don't send it.
- A premium `gameId` named on the handshake is adopted only after the entitlement verifies (DELTA-01, `engine.ts:868-893`).
  When monetization is off, it is adopted immediately (`hostLifecycle.ts:53-58`).

**Host eligibility = a non-null, allow-listed `Origin` header** (`engine.ts:408` `hostEligible: context.origin !== null`;
`hostLifecycle.ts:26-29`). A native host with no Origin gets `UNAUTHORIZED_ORIGIN` ("Hosting requires a browser origin") and stays a spectator. See §11 (flag F1).

### 3.3 `welcome` (to host)
`{t:'welcome', sessionId, role:'host', playerId:null, resumeToken:null, hostToken, joinToken, state, resumed}` (`messages.ts:286-305`).
- Persist `hostToken`. A later `hello` that presents it reclaims the room instantly and closes the stale host socket with 4101
  (`hostLifecycle.ts:32-46`). The web client keeps it in memory only (it survives socket reconnects, not tab reloads, `sessionStore.ts:358`). The UE host should persist it.
- `joinToken` goes **only in the QR fragment** (§5.1). It is optional for phones (`README.md:97-100`, `room/context.ts:41-48`).
- A second host-eligible session while an incumbent is connected becomes a "standby" `role:'host'` with no authority, and is promoted if the incumbent drops (`hostLifecycle.ts:34-40,82-113`).
  The web shows `HostStandbyScreen` when `room.hostSessionId !== sessionId` (`HostSurface.tsx:210,314-316`). Do the same.

### 3.4 Host disappearance
If the host socket closes: with `closeOnHostLeave` the room closes. Otherwise another eligible host is promoted, or the room
enters a host grace of **90 s** (`HOST_GRACE_MS`, `constants.ts:64`). During the grace an `in-progress` match is set to
`paused/pauseReason:'host-transfer'` (`hostLifecycle.ts:115-128`) and phones are still let in (FIN-001, `engine.ts:977-982`).
When no host returns, the room closes with "No host returned".

---

## 4. Server-authoritative vs host-simulated

| Concern | Owner | Where |
|---|---|---|
| Room code/id, seats (`p1..p4`, `seat` 1-4, never renumbered), names (sanitised to ≤20 chars), emoji, avatar | server | `engine.ts:949-1146`, README §3.3 |
| `ready`, `calibrated`, `connected`, `eliminated`, `graceRemainingMs`, controller `screen` | server | `room/state.ts:37-82` |
| `game.status` (`lobby`/`in-progress`/`paused`/`complete`; `countdown` exists in the type but kart never uses it) | server, changed by host commands | `protocol/src/state.ts:10,174-191` |
| Pause on phone disconnect, 135 s grace, auto-resume (GAMES-04), 3 s resume countdown | server | `engine.ts:1431-1487`, `pauseLifecycle.ts:9-97` |
| `selectedMapId` (`violet-loop` default, `sunset-harbor`) | server, set by host `configure-room.mapId` | `engine.ts:1612-1658`, `protocol/src/maps.ts` |
| Visible countdown deadline `room.countdown {kind,endsAt}` | server stamps it; host arms it | `pauseLifecycle.ts:96-114` |
| `player.score` | **host-authored** via `score-update` / `complete-game`, range-checked 0..600000 for kart | `engine.ts:1868-1893` |
| Score signing (HMAC v3), match summary, outbox redelivery | server | `engine.ts:1918-2051`, `room/scoreOutbox.ts` |
| Bots excluded from signing | server (from `hello.client` prefix `movecade-bot/`) | `constants.ts:175-182`, `engine.ts:1986` |
| **Everything about the race**: physics, intro, 5-4-3-2-1, laps, checkpoints, finish order, finish times, DNF ranking, deadline, items, roulette | **host** | `legacy/.../store.ts`, `useKartRaceNetworkSync.ts` |
| Snapshot for mid-match host reload | host (opaque blob, ≤5 KB) | `engine.ts:1895-1916` |

The server never sees kart positions. A finish time is whatever the host says, within `[10000, 600000]` ms.

---

## 5. Room lifecycle, step by step (what the host sends and receives)

### 5.1 Lobby
1. Connect, send `hello`, receive `welcome` (+`state`).
2. **QR payload:** `https://movecade.com/join/ABCDEF#v=2&t=<joinToken>`. The token goes in the fragment, never the query string. A `?src=<campaign>` query is
   optional (`roomCode.ts` `buildJoinUrl`; web: `LobbyScreen.tsx:165` with `appOrigin()`).
   The base URL must be a web deployment whose `VITE_PARTY_HOST` is **the same party host the UE host connected to**, or phones land in a different room server.
3. Display the code large (web `RoomCode size="lg"`, QR 200 px, `LobbyScreen.tsx:291-295`).
4. **Phones arrive.** The host just renders `state.players` (≤4, `MAX_PLAYERS`). A phone goes `join → calibrate → ready` (README §4.3).
   Web host extras, both optional telemetry: `{"t":"host-lobby-visible","stateVersion":n,"playerId":"p1"}` once per player after the
   lobby actually rendered them (`HostSurface.tsx:265-284`, `messages.ts:93-98`). The server answers `join-visible`.
5. **Track picker** (kart only): `{"t":"configure-room","mapId":"sunset-harbor"}`. Lobby only (`LOBBY_ONLY` otherwise), and the game must be kart (`engine.ts:1612-1621`).
   A map change resets everyone's `ready` but **keeps calibration** (`engine.ts:1650-1661`). A **game** change clears calibration (`:1636-1641`).
   UI: two cards (`TrackPicker.tsx:22-40`, catalog `tracks/catalog.ts:8-11`):
   - Sunset Harbor, "Lighthouse bends · coral tunnel · marina", `#f3ad74`
   - Violet Loop, "The original figure-eight circuit", `#b8a1ff`

   Each card has an SVG outline of the track path, scaled to fit 128×84 (`TrackPicker.tsx:7-14`).
6. **Kick** (lobby only): `{"t":"kick-player","playerId":"p2"}`. The web confirms first (`LobbyScreen.tsx:328-345,473`).
7. **Lobby practice:** phones in the lobby may send `game-event` `practice:action` `{gameId:'kart', kind:'steer', actionId}` (≤80 chars). The host checks the sender is
   seated, connected and not ready, replies `game-event` `practice:ack` `{gameId, playerId, actionId}` (a host-only event,
   `constants.ts:151-159`), and shows the tile for 5.5 s (`app/src/host/useLobbyPractice.ts:5-60`).
8. **Start conditions** (web): socket open, assets prepared, ≥1 non-bot player, every player `ready && connected && !eliminated`
   (`HostSurface.tsx:327-348`). The server additionally requires ≥1 player, every player connected and ready (party mode), and the entitlement (`engine.ts:1670-1715`).
9. **Lobby countdown:** when start is allowed and there is more than one seat (a lone human must press "Start solo",
   `LobbyScreen.tsx:133-153`), the host runs a local **5 s** count (`LOBBY_COUNTDOWN_MS`, `constants.ts:73`). It plays `lobby.tick` each second
   and `lobby.go` at 0 (`useReadyCountdown.ts:23-53`), and publishes the count with `{"t":"set-countdown","kind":"lobby","durationMs":5000}`.
   On cancel (roster change, ready lost, a kick or cancel dialog open) it sends `durationMs:0` (`:47-52`). The count restarts on any roster or map change (key `LobbyScreen.tsx:132`).
   `durationMs` is 0..60000 (`COUNTDOWN_MAX_MS`). The server stamps `endsAt` on its clock; phones render `endsAt - serverTime` (`app/src/lib/useRoomCountdown.ts`).
10. At 0: `{"t":"start-game","entitlementToken":"…"}` (`HostSurface.tsx:237-241`). The server zeroes scores and counters and sets
    `status:'in-progress'`, `activePlayerId:null` (kart is simultaneous), `startedAt:now`, and clears the countdown (`engine.ts:1721-1758`).
    Possible errors: `INVALID_STATE_TRANSITION`, `NO_ACTIVE_PLAYERS`, `PLAYERS_NOT_READY`, `ENTITLEMENT_REQUIRED`.

### 5.2 Match start → race
11. Once per match, right after `in-progress`, the web host sends `{"t":"advance-turn","activePlayerId":null,"phase":"active"}`
    (`HostSurface.tsx:288-297`). This is redundant with `beginMatch`, but keep it. **Never** name a player in kart: the server would then
    relay only that player's sensor frames (`engine.ts:2236`). The legacy bridge forces `null` for simultaneous games (`legacyBridge.ts:373-380`).
12. Phones flip to `screen:'active'` immediately and start streaming sensor frames (`room/state.ts:42-51`), even during the intro.
13. Host race flow (all local, `store.ts:240` `RaceStatus = waiting|intro|countdown|racing|finished`):
    - Load gate: the web keeps the loading screen up at least 450 ms (`KartCanvas.tsx:49-70`), and a launch curtain lifts 560 ms after the first frame (`LaunchCurtain.tsx`).
    - **1000 ms** after karts exist, `startIntro()` → `intro` (`MultiplayerApp.tsx:349-370`, `useKartRaceNetworkSync.ts:497-529`).
    - Intro = `GameIntro` overlay, **6000 ms** (`GAME_INTRO_DURATION_MS`, `host/hud/gameIntros.ts:50`). The clock only runs once the camera is ready and assets are loaded, and pauses while the room is paused.
      It plays `intro.kart`, shows a skip button, and points the camera at the static `start_line` shot (`KartRaceIntro.tsx:23-60`, `GameIntro.tsx:21-97`).
      Copy: "Own the next corner." / "Chase the pack. Drift your way to the front." Steps: Steer / Drift / Items (`gameIntros.ts:35-37`).
      Reduced motion: no camera move.
    - At the intro's end, `countdownStarter()` → `startCountdown()` → `countdown`, `countdownValue=5` (`store.ts:1359-1376`). The host
      **publishes `{"t":"set-countdown","kind":"match","durationMs":5000}`** so phones show the same count (`legacyBridge.ts:71-83`).
      A 1 s interval decrements 5→1. Ticks are skipped while paused (`useKartRaceNetworkSync.ts:531-606`).
    - At 0: `startRace()` → `racing`, `startTime=Date.now()` (`store.ts:1378-1393`). The host sends `set-countdown match 0` (`legacyBridge.ts:73-76`).
      The HUD holds "Go!" for the first second of racing (`KartHud.tsx:97-101`).
14. While racing:
    - **Live scores**: once a second, if the values changed, `{"t":"score-update","scores":[{playerId,score}]}` (`legacyBridge.ts:474-506`).
      Kart live score = `laps*100 + (finished ? max(0, 1000 - place*100) : clamp(floor(lapPercent),0,99))` (`legacyBridge.ts:165-188`).
      These numbers are deliberately < 10000, so they can never be mistaken for a finish time. Scores must be integers 0..600000 and at most 4 entries.
    - **Snapshot**: host `{"t":"save-snapshot","snapshot":{…}}` every 5 s during intro/countdown/racing, and immediately when any item state
      changes (`KartRaceUI.tsx:222-242`). Server limits: ≤5 KB serialised, JSON depth ≤4 per the parser (`messages.ts:817-822`;
      `MAX_SNAPSHOT_DEPTH` 6 in the engine), min interval 250 ms (silently dropped if faster). Errors: `SNAPSHOT_REJECTED`. Shape: `KartRaceSnapshot` (`store.ts:242-296`).
      It is optional for a UE host. Use it only if you implement host-reload restore.
    - Lap: `recordLap`. When a kart's lap count reaches `totalLaps`=3, `recordFinish` (`useKartRaceNetworkSync.ts:608-626`, `store.ts:1426-1487`).
    - **Race end** (`RACE_CONFIG`, `store.ts:444-459`): `maxRaceTime 300000` (5 min hard cap), `finishGraceMs 30000` after the leader finishes.
      The deadline is `min(300000, leaderFinish+30000)` (`store.ts:389-397`), checked every 250 ms by the host and frozen while paused (`useKartRaceNetworkSync.ts:61,289-298`, `store.ts:1489-1506`).
      The race also ends when every kart still in the race has finished, including when a departure leaves only finishers (`store.ts:1274-1289`).
15. **Completion** (`RaceUI.tsx:844-893`, `useGameCompletion.ts:56-65`, `legacyBridge.ts:388-400`):
    - Scores = each finisher's `finishTimes[id]` (ms, race clock), then each DNF's `kartProgressScore` (`laps*100+pct`, <10000) (`store.ts:406-415`).
    - The host sends `score-update` with those scores, then `{"t":"complete-game","scores":[…]}`. The update goes first so a rejected completion still leaves the scores.
    - It also sends `game-event` `match:highlights` `{gameId, matchNumber, highlights:{p1:{title,detail},…}}` once the room is `complete` (`HostSurface.tsx:99-112`).
      Kart lines: `"Finished 1st"` / `"Best lap · 23.45s"` or `"Time · m:ss"`, or `"Did not finish"` / `"2 laps completed · Best lap …"` (`games/feedback/matchHighlights.ts:84-114`).
      The iOS controller shows them. The web phone currently ignores them.
    - Server guards: the match must be `in-progress|paused`, not already completed (`DUPLICATE_COMPLETION`), and **≥10 s since `startedAt`** (`MATCH_TOO_SHORT`) (`engine.ts:1918-1932`).
16. Server → `status:'complete'`. Every phone gets `screen:'results'`. **Only the host** gets `signed-scores` (§7).

### 5.3 Results → again
17. Results screen (`GameOverScreen.tsx`). Buttons:
    - **Play again**: `{"t":"restart-game","actionId":"…"}`.
    - **Leave room**: closes the session. Optionally send `close-room` to end it for everyone.
    - **Next game strip**: `return-to-lobby`, then `configure-room{gameId}` once the room is back in the lobby (`HostSurface.tsx:114-131`).
    - **Share invite**.
18. Both `restart-game` and `return-to-lobby` put the room back in **`lobby`** (`matchNumber+1`; scores, ready and eliminated reset; disconnected seats
    released) (`engine.ts:2053-2094`). The server acks with `{"t":"action-ack","action":"restart-current-game"|"return-to-lobby","actionId","accepted","stateVersion","reason?"}`
    after persisting (`:2096-2130`). From `lobby` it answers `accepted:false, reason:'already-in-lobby'`.
    So "Play again" means everyone re-readies and the lobby count runs again. The same two actions are in the in-race "Host actions" menu (`GameShell.tsx`), with a confirm modal and a retry on ack timeout (`usePendingHostCommand.ts`).

### 5.4 Join / leave / reconnect during a race
- A phone can't join mid-match in a party room (`LOBBY_ONLY`, `engine.ts:1036-1039`).
- **Phone disconnect** (socket close or 3 missed pongs): the seat keeps everything for **135 s** (`RECONNECT_GRACE_MS`) and the room goes
  `paused/player-disconnected/pausedForPlayerId` (`engine.ts:1431-1452`). **While paused, the server drops all sensor frames** (`engine.ts:2235`).
  The host must pause the race clock (web: `pauseRace/resumeRace` driven by room `paused`, `useKartRaceNetworkSync.ts:143-160`, `store.ts:1548-1600`).
- Host pause overlay: "Waiting for {name}", "Holding their spot for N more seconds" (from `players[].graceRemainingMs`, never a constant),
  and a button "Continue without {name}" → `{"t":"drop-player","playerId"}`. Drop only works for a disconnected player (`INVALID_STATE_TRANSITION` otherwise),
  and marks them eliminated (`GameShell.tsx:237-252`, `engine.ts:2181-2202`).
  `PAUSE_PROMPT_AFTER_MS = 45000` exists (`constants.ts:62`), but the web shows the button immediately.
- **Reconnect within grace:** auto-ready if calibrated, then `resume-countdown {remainingMs:3000, playerId, cancelled:false}` broadcast, and `in-progress` after 3 s
  (`engine.ts:1469-1487`, `pauseLifecycle.ts:64-80`). A cancel arrives as `resume-countdown {remainingMs:0,cancelled:true}`.
- **Grace expiry mid-match:** the player is `eliminated:true` and stays in the roster. The kart is removed from the race (roster key filters eliminated, `raceRoster.ts:19-25`).
- If every active player is gone, the room goes straight to `complete` with **no signing** (`pauseLifecycle.ts:37-46`; there is no `complete-game` command).
- A host socket reconnect shows "Reconnecting… everyone keeps their spot" (`GameShell.tsx:220-235`).

### 5.5 Message cheat-sheet (host)

**Send:** `hello`, `pong` (reply to every ping), `ping{nonce}` (RTT, optional), `state-ack{stateVersion}`, `host-lobby-visible{stateVersion,playerId}` (opt),
`configure-room{gameId?,mapId?,closeOnHostLeave?,entitlementToken?}`, `set-countdown{kind:'lobby'|'match',durationMs}`, `start-game{entitlementToken?}`,
`advance-turn{activePlayerId:null,phase:'active'}`, `score-update{scores}`, `save-snapshot{snapshot}`, `complete-game{scores?}`,
`restart-game{actionId?}`, `return-to-lobby{actionId?}`, `kick-player{playerId}`, `drop-player{playerId}`, `close-room`,
`game-event{event,data?}` (`practice:ack`, `match:highlights`), and the out-of-band `{"v":2,"t":"scores-ack","matchNumber":N}` (§7.3).
Host-only types: `messages.ts:497-511`.

**Receive:** `welcome`, `state{state,reason,delivery?}`, `screen` (ignore as host), `error{code,message,fatal}`, `host-changed{hostSessionId,reason,stateVersion}`,
`action-ack`, `state-observed`, `join-visible`, `resume-countdown`, `signed-scores{game,scores}`, `room-closed{reason}`, `ping`, `pong`,
`game-event{event,data,fromPlayerId}` (`kart-input`, `practice:action`). Binary: 31/39-byte relayed sensor frames.

`RoomState` fields: `roomId, joinCode, mode?, queue?, gameId, selectedMapId?, stateVersion, hostSessionId, hostMissing, hostGraceRemainingMs,
closeOnHostLeave, players[], spectators[], game{status,activePlayerId,turnNumber,roundNumber,phase,matchNumber,startedAt,pauseReason,pausedForPlayerId,pausedAt,snapshot,snapshotAt},
countdown?{kind,endsAt}, serverTime` (`protocol/src/state.ts:217-253`).
`PlayerState`: `id, name, emoji?, avatar, seat, score, ready, bot?, calibrated, connected, eliminated, disconnectedAt?, graceRemainingMs, screen` (`state.ts:137-167`).
`identity` is **never** broadcast.

---

## 6. Phone controller: what it shows and sends

### 6.1 Binary sensor frame (the steering wheel)
Phones send a **30-byte**, or **38-byte with quaternion**, little-endian frame at `sendHz = 30` for kart (`index.ts:18`; throttle
`minIntervalMs = 1000/hz`, `lib/motion.ts:394,474-477`). The server appends **1 byte = `seat-1`** and relays **31 or 39 bytes to the host only**
(`engine.ts:2218-2242`, `sensor.ts:501-508`).

| off | type | field | decode |
|---|---|---|---|
| 0 | u8 | frameType | must be `0x02` |
| 1 | u8 | flags | bit0 CALIBRATED, bit1 SCREEN_COMPENSATED, bit2 HAS_GYRO, bit3 HAS_ACCEL, bit4 HAS_GRAVITY, bit5 HAS_QUATERNION. Any bit outside `0x3F` = invalid |
| 2 | u8 | screenOrientation | 0 portrait, 1 landscape-left, 2 upside-down, 3 landscape-right (must be ≤3) |
| 3 | u8 | sequence | wraps at 256 |
| 4 | i16 | alpha | /100 deg |
| 6 | i16 | beta | /100 deg |
| 8 | i16 | **gamma** | /100 deg. **In kart this is the wheel angle**, not Euler roll (below) |
| 10,12,14 | i16 | gyro x,y,z | /20 deg/s |
| 16,18,20 | i16 | accel x,y,z (gravity removed) | /1000 m/s² |
| 22,24,26 | i16 | gravity x,y,z | /1000 m/s² |
| 28 | u16 | dtMs | ms since the previous frame |
| 30..37 | i16×4 | quat x,y,z,w | /16384, present only if flag bit5 |
| 30 or 38 | u8 | **playerIndex** | `seat - 1` (relay only) |

Validation is `protocol/src/sensor.ts:422-443`. Size must be 30/31/38/39, the quaternion flag requires 38/39, and orientation must be ≤3.
**Resolve the player by seat (`players.find(p => p.seat === idx+1)`), never by array index**: the roster compacts after a release (`legacyBridge.ts:253-259`).

**Kart "wheel mode" gamma** (`lib/motion.ts:363-374,479-490`, `lib/wheel.ts:52-60`): the phone computes the rotation of gravity in the
*screen* plane, `atan2(gRight, -gUp)` in degrees, with screen axes chosen by `screenOrientation`. That makes 0 = level in landscape and positive = turned clockwise (steer right).
If the in-plane gravity is ≤1.5 m/s² (phone flat), it is undefined and the last value is held. The phone **subtracts its calibrated neutral gamma**
and wraps the result to (-180,180] before encoding (CALIBRATED flag set). `SCREEN_COMPENSATED` is left clear (`motion.ts:531-533`). Kart never uses the axes.

**Host steering transform** (live code, `MultiplayerMobileControlBridge.tsx:228-291`, constants `legacy/utils/kartRaceConstants.ts:8-23`):
```
g = gamma; wrap to [-180,180]
s = clamp(g / 90, -1, 1)                          // MAX_STEER_ANGLE 90
dz = 2/90                                         // STEER_DEADZONE 2°
s = |s| < dz ? 0 : sign(s)*(|s|-dz)/(1-dz)
s = clamp(s * steeringSensitivity, -1, 1)          // kartRaceStore default 1.0
s = -s                                            // INVERT_STEERING = true
(STEER_SMOOTHING = 1.0 → no lerp)
kart.steeringInput = s                            // via store.syncKartInput
```
Note the sign: clockwise phone → positive gamma → **negative** `steeringInput`. Keep that convention, or flip both ends consistently.
Web quirk: frames go through a single `mobileData` slot plus a React effect, so with 4 phones some frames can be coalesced. The UE port should process every frame for each player.
There is **no stale-input timeout** on the web. Consider decaying steering to 0 after ~300 ms with no frames.

### 6.2 JSON controls: `game-event` `kart-input`
Phones send `{"v":2,"t":"game-event","event":"kart-input","data":{…}}`. The host receives `{"t":"game-event","event":"kart-input","data":{…},"fromPlayerId":"p2"}`.
The server relays it to **every other session**, counts it toward `player.actions`, and rejects it from spectators and eliminated players (`engine.ts:739-777`).

| data field | sent when | host effect (`legacyBridge.ts:318-335`) |
|---|---|---|
| `isAccelerating: bool` | GAS pointer down/up (edge-triggered) | `syncKartInput` |
| `isReversing: bool` | BRAKE down/up ("Brake and reverse") | `syncKartInput` |
| `isDrifting: bool` | DRIFT down/up | `syncKartInput` |
| `useItem: true` | ITEM pointer down (one-shot) | `executeItemUse(fromPlayerId)`, which ignores the press while rolling or with no item (`useItemAction.ts:35-41`) |
| `steeringInput: number` | **bots/assist only** (clamped −1..1) | `syncKartInput` |

`KartController.tsx:43-113`: each pedal change sends one event containing only that field. On deactivate, `visibilitychange`, `pagehide`, or
an orientation prompt, it sends `{isAccelerating:false,isReversing:false,isDrifting:false}` (`:43-79`). Pedals are only accepted while `screen==='active'` and the phone is landscape.
Bots send `{isAccelerating, isDrifting, steeringInput}` on change (≤ one per tick), plus `{useItem:true}` 1.2–3.7 s after picking up an item (`host/bots/kartPilot.ts:186-232`).
They send **no** sensor frames. The legacy bridge rate limits (50 ms per button, 200 ms per item, `MultiplayerMobileControlBridge.tsx:61-66`) are on the dead path.
**Implement a 200 ms item debounce per player in UE.**

### 6.3 What the phone shows (web `KartController.tsx`, iOS `ios/Movecade/Games/KartActive.swift`)
- Landscape-locked. When portrait it shows "Turn it sideways / Hold the phone like a steering wheel, screen towards you. Not flat." (`:121-131`).
- Left column: **DRIFT** (hold) and **ITEM** (tap, 140 ms flash). Centre: a wheel graphic rotated `round(clamp((gamma-calGamma)/90)*90)`°, plus the status line
  `"Braking" | "Gas held" | "Coasting"` + ` · {me.score} pts`. Right column: **GAS** and **BRAKE** (hold) (`:133-254`).
- Haptics (Vibration API, `hooks/haptics.ts:13-24`): pedal down 12 ms (`press`), ITEM 24 ms (`gesture`), and the `turnStart` pattern `[40,70,20,70,40]` with a
  "Your turn" banner when the screen flips to `active` (`ControllerSurface.tsx:263-274`).
  A score-change banner `"You scored Δ — total"` with the `confirm` haptic fires on every `player.score` change (`ControllerSurface.tsx:280-291`).
  **Kart quirk:** live lap points make this fire on every lap.
- Countdown overlay for `room.countdown` (lobby 5 s, match 5 s), "Go!" at 0 (`controller/CountdownOverlay.tsx:24-51`).
- **Host → phone kart feedback: none.** No item, roulette, position or lap display on the phone. Item state is pushed in snapshots
  "so controllers do not show stale item affordances" (`KartRaceUI.tsx:237-242`), but no controller reads it. The **only** channels are the room countdown,
  `player.score`, `screen`, and `match:highlights` at the end. Adding item/position feedback needs a new host `game-event` (e.g. `kart:hud`) **and**
  controller changes on web and iOS (§11 F5).

### 6.4 Calibration and ready (phone side, host just observes)
The phone sends `calibrate{alpha,beta,gamma,screenOrientation,motion?}` and then `set-ready{ready:true}`. The server rejects ready before calibration.
For kart the calibrate screen uses the "wheel" pose (`ControllerSurface.tsx:620,653`). Readiness is removed when the game, grip or screen orientation changes (`:294-305`).

---

## 7. Scoring and the leaderboard

### 7.1 What the host reports
- Final kart score = **finish time in ms** (race clock: `Date.now() - startTime - totalPauseDuration`, `store.ts:362-378`) for finishers.
  For DNFs it is `laps*100+pct` (<10000). The scoring rule comes from the catalogue (`protocol/game-manifest/catalog.json:5`): `scoring:{mode:'lowest', min:0, max:600000, format:'time-ms'}`.
- Signing floor: `MIN_SIGNED_TIME_MS = 10000`. `signScore` refuses a time-ms score below it with `SCORE_BELOW_GAME_MIN` (`protocol/src/scoring.ts:46-55,289-311`).
  Lowest-wins games also **skip eliminated seats** (`engine.ts:2016-2025`). Bots are never signed.

### 7.2 Signature (server side; the host forwards verbatim)
`v3:{playerIdentity}:{matchPlayerId}:{playerName}:{score}:{game}:{timestamp}:{roomId}:{matchDigest}`, HMAC-SHA256 hex.
`matchDigest` = sha256 over the canonical match-summary JSON (key order `version, gameId, gameVersion, roomSessionId, startedAt, endedAt, durationMs,
players[{playerId,score,motionSamples,actions,reconnects,eliminated}], totalMotionSamples, totalActions`). The timestamp is `endedAt` (`README.md:512-541`, `engine.ts:2030-2040`).
The fallbacks are v2 (no player identity) and v1.

`SignedScore` = `{playerName, playerIdentity?, matchPlayerId?, signatureVersion, matchDigest?, matchSummary?, score, game, roomId, timestamp, signature}` (`messages.ts:398-410`).

### 7.3 Delivery and submission
- `signed-scores` goes to the host via a **durable outbox** keyed `roomId:matchNumber`. It is redelivered to whichever host holds the room, up to **4 attempts**
  (5 s base backoff, 60 s ceiling), and dropped 30 s before the 5 min signature expiry (`room/scoreOutbox.ts:16-49`).
  **Ack it** with the out-of-protocol frame `{"v":2,"t":"scores-ack","matchNumber":N}`. `matchKey` and a bare ack are also accepted (`engine.ts:512-587`).
  The web host never sends this ack (grep shows no client), so it gets redeliveries and dedups them by signature.
- **Submit** each score with `POST {API}/api/scores`, `Content-Type: application/json`. The body is assembled field by field, because the Go decoder uses `DisallowUnknownFields`.
  Fields: `playerName, score, game, roomId, timestamp, signature, signatureVersion` + optional `playerIdentity, matchPlayerId, matchDigest, matchSummary`.
  **Never reformat** any value (`client-core/src/scoreSubmission.ts:226-262,276-345`).
  Responses:
  - 200 `{success:true, duplicate?, id?}`
  - 400 invalid/expired
  - 403 bad signature (terminal)
  - 429 with `Retry-After`
  - 5xx retryable

  Retry: 6 attempts, base 500 ms, max 30 s, jittered, never past 5 min − 15 s from the timestamp (`scoreSubmission.ts:52-66`). The UI offers 2 manual retries (`useScoreSubmission.ts:85`).
  Send scores one at a time (the per-IP limiter allows ~5 req/s). Dedup by signature (the backend uses `ON CONFLICT (signature) DO NOTHING`, `backend/main.go:436`).
  A native client sends no `Origin`, which the backend accepts: CORS is only enforced when `Origin` is present (`backend/main.go:293-305`).
- **Filter before submitting:** submit only when `isFinishedScore` holds, i.e. `score > 0 && score >= 10000` for kart (`app/src/host/useScoreSubmission.ts:77-82`,
  `games/manifest/scoring.ts:26,44-49`). The backend refuses `score < minPlausibleScore["kart"]=10000` (`backend/score_validation.go:33-35,85-92`)
  and `|timestamp-now|` outside −5 min/+30 s (`main.go:398-401`).
- Results status copy: "Saving to the leaderboard…", "Saved to the leaderboard.", "Some scores could not be saved…", "These scores could not be saved…" (`GameOverScreen.tsx:24-30`).
- Leaderboard read for the results card: `GET /api/leaderboard?game=kart&timeframe=all|week|today`. Kart is `MIN`/`ASC`, top 50, rows `{rank,name,score,games,avgScore,key?}` (`backend/main.go:454-511`).

### 7.4 The audit bugs and what the port must preserve
| Audit item | Bug (2026-09-23) | Fix now in code, replicate in UE |
|---|---|---|
| **GAMES-01** (P0) | Results sorted `b.score-a.score`, which crowned the slowest racer, and showed raw ms | `rankByScore` (`games/manifest/scoring.ts:57-67`): ascending for `lowest`. Finished scores rank first; non-finishers go last, ordered by progress descending, then seat. Format `formatLapTime` (`m:ss.mmm`) or `"DNF"` (`:70-74`). Winner only if the top score is a finish; ties give "Photo finish!" (`GameOverScreen.tsx:106-121`) |
| **GAMES-02** (P1) | A dropped/DNF racer was signed with 0 ms and went to the top of the MIN board | Three layers: the host submits only `isFinishedScore`; the server skips eliminated seats in lowest-wins games and refuses <10000 ms; the backend refuses <10000 for kart. **The UE host must still never report 0 as a finish**, and should send DNF progress (<10000) or simply omit the score |
| **No race time cap** | `maxRaceTime` was unused | `endRaceAtDeadline` with a 5 min cap and leader+30 s (`store.ts:444-450,389-397,1489-1506`). The HUD shows "Race ends in / Finish in Ns" in the last 30 s (`KartHud.tsx:65-76`). **Keep a cap below `max 600000`** |

Other score traps to avoid:
- The race clock must exclude pause time (`totalPauseDuration`).
- The server requires ≥10 s of wall-clock match time, measured from `start-game` and including intro and countdown (`engine.ts:1928-1932`).
- The match summary event-rate cap is 240/s (`score_validation.go:22`). At 4×30 Hz that's fine; don't raise `sendHz`.
- `score-update` silently drops values outside 0..600000 or non-integers (`engine.ts:1874-1888`). **Round the ms.**

---

## 8. Host canvas HUD / UI (what to rebuild in UMG)

### 8.1 Layout: split-screen, one viewport per kart
Viewport rectangles in % (`RaceUI.tsx:34-50`):
- 1 kart: full screen
- 2 karts: top/bottom halves
- 3 karts: two on top, one bottom-centred half-width (x 25%)
- 4 karts: 2×2

The host is **not a player**; it renders every kart (`MultiplayerApp.tsx:93-98`). Seat colours come from `SEAT_COLORS` (`@app/games/render`).

### 8.2 Shared top HUD: `KartHud` (`KartHud.tsx:78-128`, the shared `GameHud` from `app/src/host/hud`)
Visible while `countdown|racing`.
- **Stage:** `CountdownStage` with numerals 5..1, then "Go!", held for 1 s after GO.
- **Timer chip:** "Race time" `m:ss`, refreshed every 250 ms. It reads "Paused" while paused.
- **Aside chips:**
  - "Finish in" / "Race ends in" `{s}s` (accent, urgent), shown only within 30 s of the deadline.
  - "Lead lap" `n / 3` (accent on the final lap).
  - "Racers" `{n}`.
- **Item slot row** (`KartItemSlots`, `:21-45`): one slot per racer, always the same size. States are `empty` ("·", "Empty"), `rolling` ("Rolling"), and `held` (icon + name).
  Names: Green shell, Red shell, Banana, Mushroom, Triple boost, Star, Lightning, Blooper, Bomb, Blue shell, Bullet Bill (`kartItems.ts:24-36`).
  Several of these are Nintendo marks: rename them in the port (see 02-items doc).
- **Player rail** (one `PlayerTile` per kart):
  - Place, as an ordinal (`rankKarts`, `:48-59`: finished first by finish order, then laps desc, checkpoint desc, lap % desc).
  - Detail: `Lap n · {mph} mph`, then a cue line chosen in priority order: Finished > Wrong way! > Choosing item… > {item} > drift label (`getDriftChargeStatus`) > Boosting > Off-road > Race clean.
  - Badge: "Turn back" or "Finished".
  - Settings button top-left (`QualitySettings`).

### 8.3 Per-viewport widgets (`RaceUI.tsx`)
- **Minimap** (`:597-704`), bottom-left of each view. It shows only while `racing`.
  - Heading: the view's racer name in seat colour.
  - Map: the track SVG path (edge + road), a start/finish chequer at world (5,5), and every kart as a seat-coloured dot or its leading emoji.
  - The focus kart gets a heading arrow, from velocity when speed > 0.2, otherwise from yaw.
  - Mapping: `x=(pos.x-minX)/scaleX*100`, `y=(pos.z-minZ)/scaleZ*100`, clamped 5–95%. Bounds per track: `tracks/catalog.ts` (`MINIMAP_BOUNDS`; Harbor `{-100,350,-260,180}`). Refreshed every 16 ms.
- **Speed instrument** (`RaceSpeed`, `:709-831`), bottom-right. Dial max = `ceil(TOP_MPH*1.5/5)*5`, with a boost zone from top speed to max. The needle eases at a 10/s rate; the number updates at 10 Hz.
  It has a "Boost" tag and stats Place (ordinal) and Lap `n / 3`. Reduced motion: the dial steps every 250 ms.
  **Units bug:** the HUD treats `kart.speed` as km/h and multiplies by 0.621371, but the sim's `kartSettings.speed.max = 40` is labelled mph (`engine/constants.ts:3-5`).
  So the dial reads ~25 "mph" at top speed. The audio code treats speed as mph (`soundMix.ts:193-195`). **Pick one unit in the port.**
- **Item roulette** (`:391-506`), top-centre, starting below the top bar when the view touches the screen's top edge.
  It's a slot-machine strip of 35 random items ending in the real item. Item height 50 px, 3 visible. 25 items/s initial, decaying ×0.985 per 60 Hz frame, stopping at 1800 ms.
  The item name stays up 1200 ms, then `stopItemRolling(playerId)`. **This HUD timer gates `isRollingItem`**, so the roulette is gameplay-relevant: the item can't be used while rolling.
- **Activation popup** (`:514-560`): when a held item disappears without rolling, show icon + name, popping in over 0.4 s and out after 1.2 s.
- **Screen effects** (`:321-358`):
  - `INK_SPLATTER`: dark radial blots, 0.5 s drip.
  - Explosion: an orange flash fading over 0.5 s.
  - Star: a rainbow tint with a rainbow border.

### 8.4 Pre-race and non-race screens
- **Lobby** (`LobbyScreen.tsx`):
  - 4 seat pods with name, avatar, ready/connected state and grace.
  - QR + code, "Share invite".
  - TrackPicker (kart only).
  - Countdown label: "Everyone ready" / "Starting in N…" / "Here we go!".
  - Start-blocked reasons, e.g. "Scan the code with a phone to join.", "Waiting for N players to get ready." (`HostSurface.tsx:337-348`).
  - Practice tiles, a kick list, add-bot, and a cancel confirm.
- **Loading:** "Loading Kart Racer" with "{n}%" / "Warming up the track" / "Preparing the room's track" (`KartCanvas.tsx:64-81`). The launch curtain shows game hue, mark, players and a progress bar.
- **Intro:** `GameIntro` with title, invitation, objective, venue (track name), a gesture demo, 3 step beats with progress, "Skip intro", and footer text "Setting the scene…" / "Intro paused" / "Get into position" / "See you on the grid." (`GameIntro.tsx`).
- **In-race host chrome:** game name, mute toggle, "Host actions" (Restart current game / Return to lobby, confirm modal, ack text, retry on timeout), the pause modal, and the reconnecting modal (`GameShell.tsx`).
  The join code is hidden mid-match.
- **Results** (`GameOverScreen.tsx`):
  - Left: "Kart Racer · final", winner medallion(s), "{name} wins!" / "Photo finish!" / "Game over", sub-line, and the time in signage size with the unit "finish time".
  - Right: standings card with Rank, Name, Time (`m:ss.mmm` / DNF) and highlights, with the leaderboard status as its footer.
  - Actions: Play again / Leave room / Share invite, the next-game strip, and the Party Pack upsell.
  - The race scene stays mounted behind it with an end camera shot (`MultiplayerApp resultMode`, `KartRaceCamera resultMode`).
  - The roster is frozen at match end so a leave can't blank the results (`HostSurface.tsx:87-98`).
- **Failure / standby:** `RoomFailureScreen` (retry / new room) and `HostStandbyScreen` (another TV holds the room).

---

## 9. Audio

### 9.1 Mixer (web `app/src/audio/audioService.ts`)
- **Buses:** master (0 when muted) feeds an `sfx` bus (default 0.8) and a `music` bus (default 0.5). Mute and volumes persist in `localStorage`
  (`movecade.audio.muted|sfx|music`) (`:98-102,286-292,506-533`). Muting stops all loops.
- **One-shots:** a 24-voice ring, reusing the oldest voice (`VOICE_COUNT`, `:118`). Gain = `cue.gain × clamp(opt.gain,0,2)`, rate clamped 0.25..4, pan −1..1.
  Each cue has a **throttle `throttleMs`** minimum between plays (`:693-736`).
- **Loops:** keyed `setLoop(key, cue, {gain,rate,pan,lowpass})`. Idempotent: per frame it only updates gain (τ 0.035 s), rate (τ 0.06 s) and lowpass (τ 0.08 s), with the lowpass clamped 40..20000 Hz.
  Max 32 loops (`:588-633`). `stopLoops(prefix)` stops loops and engines.
- **Engines:** `setEngine(key, {rpm, load, character}, {gain, pan})`. At most **8** engines, at output gain `ENGINE_GAIN 0.3 × gain` (`:121-122,635-650`).
- **Lane panning:** `laneToPan(i,n) = ((i-(n-1)/2)/((n-1)/2))*0.55`, 0 when n ≤ 1 (`:200-205`). Everything is 2D stereo, not 3D.
- All sound stops when the tab is hidden. Kart audio runs **only on the host** and only while not paused (`sound.ts:62-69`).

### 9.2 Engine sound: layered recordings (`audio/engineVoice.ts`, `audio/kartCues.ts:9-12`)
- Banks: **on-throttle** `kart-engine-on-{1000,2500,4000,5500,7000}.wav` and **off-throttle** `kart-engine-off-{2500,6500}.wav`, all looping (ElevenLabs).
- Every layer source starts once and loops. The start offset per kart is `character × duration`, with `character = (spawnIndex*0.29+0.13)%1` (`sound.ts:124`), so engines don't phase.
- Per update (`engineVoice.ts:111-132`):
  - `rpm` is clamped to 500..9500 and `load` to −1..1.
  - Within each bank, **equal-power crossfade between the two layers bracketing the RPM**: `cos(t·π/2)` / `sin(t·π/2)`. Outside the range, the end layer plays at 1 (`:50-61`). Gain τ 0.03 s.
  - Each layer's `playbackRate = clamp(rpm/layerRpm, 0.5, 2)`, τ 0.02 s.
  - Bus crossfade: `idle = clamp((1800-rpm)/800,0,1)`, `drive = max(idle, clamp((load+0.3)/0.9,0,1))`. The on-bus gets `sin(drive·π/2)` and the off-bus `cos(drive·π/2)·0.7` (τ 0.04 s).
  - Output `= (0.8 + 0.2·clamp(rpm/7000,0,1)) × mixGain` (τ 0.03 s), then a stereo pan.
- **Drivetrain** (`games/kart/drivetrain.ts`): a pretend 6-speed gearbox that turns speed (mph) into RPM. It's deterministic and seeded with `spawnIndex+1`.
  - `IDLE 950`, `UPSHIFT 7600`, `LIMITER 8600`, downshift 3500 (kickdown 5200 on throttle), launch 3300.
  - Gear tops (mph): `[19.2,31.7,42.5,54.4,70,90]`. Shift gap 0.13 s, lockout 0.25 s, limiter cut 0.055 s.
  - Neutral (the grid before GO): free-revs to 9000 (limiter bounce) when gas is held.
  - Lift-off above 4500 rpm queues backfire pops: one immediately plus 1–3 more at 0.07–0.39 s. 35% of upshifts also pop.
  - Load: −0.35 during a shift or limiter cut, 1 on throttle, −1 when coasting above idle+350, else 0.1.
  - Emits `upshift|downshift|backfire` events. Throttle comes from `isAccelerating`, or is inferred from acceleration (EMA > 1.5 / < −2.5).

### 9.3 Every kart cue and when it fires
Base gains and throttles are from `kartCues.ts:15-60`. Per-kart options are `{pan: laneToPan(index by spawnIndex), gain: 1/√N}` (`sound.ts:92-104`).

| Cue (file `kart-*.wav`) | gain / throttle ms | Fires (source) |
|---|---|---|
| `lobby.tick` (lobby-tick.wav) | 0.5 / 100 | lobby count each second (`useReadyCountdown.ts`). Race countdown each value 5..1, with rate 1.0 for values ≥3, 1.1 at 2, 1.2 at 1 (`sound.ts:79-82`) |
| `lobby.go` (lobby-go.wav) | 0.5 / 100 | lobby count reaches 0; race `countdown→racing` (`sound.ts:83`) |
| `intro.kart` (synth: saw sweep 65→196 Hz + G-B-D arpeggio, 1.65 s) | 0.4 / 1200 | intro starts (`GameIntro.tsx:41-44`, `introCues.ts:41-49`) |
| `kart.engine-*` (7 loops) | 1 / 0 (×0.3 engine gain) | per kart while countdown/racing, not finished, not Bullet Bill |
| `kart.shift` | 0.32 / 120 | drivetrain upshift |
| `kart.backfire-a` / `-b` (alternating) | 0.3 / 60 | drivetrain backfire; gain `mix*(0.6+rpm/12000)`, rate `0.92+(n%5)*0.04` |
| `kart.tire-skid` loop | 0.16 | `squealMix` gain > 0 while racing. Drifting: `clamp((v-6)/24)*(0.5+0.3*clamp(dp/6)+0.2*|steer|)`. Grip slide: `0.35*clamp((|steer|-0.55)/0.45)*clamp((v-28)/20)`. Off below 0.02; rate `0.9+min(v/160,0.5)+0.08g` (`soundMix.ts:289-301`) |
| `kart.wind` loop | 0.2 | `t=clamp((v-4)/76)`: gain `t^1.6`, rate `0.85+0.3t`, lowpass `700+13300·t^1.2` Hz (`soundMix.ts:193-213`) |
| `kart.offroad` loop | 0.14 | `isOnDirt && speed>3` |
| `kart.boost-loop` loop | 0.17 | `isBoosting && !BulletBill` |
| `kart.item-roulette` loop | 0.16 | `isRollingItem` |
| `kart.invincible-loop` loop | 0.12 | `isInvincible && !BulletBill` |
| `kart.rocket-loop` loop | 0.2 | `isBulletBill` |
| `kart.drift-boost-1/2/3` | 0.4/0.5/0.62 / 65 (+tier gain 0.8/0.9/1.0) | boost start where `activeBoost` starts with `DRIFT_`. Grade MINI/SUPER/ULTRA, falling back to the charge before release (thresholds 1/3/6 s) (`soundMix.ts:260-282`) |
| `kart.boost` | 0.45 / 65 | other boost starts (mushroom, pad), and Bullet Bill start |
| `kart.drift-charge` | 0.45 / 65 | `floor(driftPower)` increments while drifting |
| `kart.item-pickup` | 0.45 / 65 | roulette starts |
| `kart.item-ready` | 0.45 / 65 | roulette ends with an item |
| `kart.invincible-start` | 0.45 / 65 | star starts |
| `kart.lightning` | 0.45 / 65 | kart becomes shrunk; or own LIGHTNING used |
| `kart.ink` | 0.45 / 65 | `screenEffect→INK_SPLATTER`; or own BLOOPER used |
| `kart.warning` | 0.34 / 850 | wrong way starts; also at gain 0.6 when a RED/BLUE shell spawns |
| `kart.lap` / `kart.final-lap` | 0.45 / 65 | lap count increases. `final-lap` when the new lap count == totalLaps−1 |
| `kart.finish` | 0.58 / 65 | each new id in `finishOrder` |
| `kart.hazard-drop` | 0.45 / 65 | new BANANA in `activeItems` |
| `kart.projectile-launch` | 0.45 / 65 | any other new active item |
| `kart.projectile-bounce` | 0.45 / 65 | shell wall bounce (`MultiplayerItems.tsx:327,357`) |
| `kart.projectile-hit` | 0.45 / 65 | item hits a kart (`MultiplayerItems.tsx:597`) |
| `kart.spinout` | 0.45 / 65 | kart spun out (`MultiplayerItems.tsx:174`) |
| `kart.explosion` | 0.65 / 65 | bomb / blue shell (`MultiplayerItems.tsx:217`) |
| `kart.hop` / `kart.land` | 0.45 / 65 | drift hop and landing (`MultiplayerKart.tsx:436,445`) |
| `kart.fall` / `kart.respawn` | 0.45 / 65 | off-track fall and respawn (`MultiplayerKart.tsx:871,873,1057`) |
| `kart.crash-light/medium/heavy` | 0.4/0.5/0.62 / 90 | wall impact. `strength = clamp(mph/60 + penetration*1.5 + (critical?0.15:0))`; tiers <0.35 / <0.7 / ≥0.7; gain `0.6+0.4·(position within tier)`; rate `0.96+((s*7919)%1)*0.08` (`soundMix.ts:249-258`, `kartAudioEvents.ts:22-25`, `MultiplayerKart.tsx:1485,1520`) |
| `kart.collision` | 0.48 / 65 | defined but no live call site found |

Physics one-shots (`playKartEvent`) only play on the host while racing, not paused and not finished, at gain `opt/√N` with the lane pan (`kartAudioEvents.ts:5-18`).
On reset (new `startTime`, or back to `waiting`) all `kart:` loops stop. Finished karts stop their own loops (`sound.ts:73-78,109-112`).
Fallback: if a file fails to load, a 0.3 s 180 Hz sine is baked instead (`kartCues.ts:4-6`). The roulette, item-ready, warning, lap, final-lap and finish cues have no fallback.

### 9.4 Music
**There is none.** A `music` bus and volume exist, but no cue uses `category:'music'` (`cues.ts:47`). `match.over` exists in the manifest for other games. Kart has no results sting beyond `kart.finish`.

---

## 10. Entitlements / premium gating
- The kart manifest says `tier:'premium'`, offer `game:kart` $1.99 (`catalog.json:5`). The **current model** is the Party Pack (`pack:party`, which includes kart).
  Bowling is the only free game (`app/src/entitlements/CONTRACT.md` §1). Separately, the launch policy `LAUNCH_GAME_IDS` includes kart (`protocol/src/launch.ts`).
  That only affects the TV route and is not an authorisation.
- **Enforcement is on the room server, and only when `ENABLE_MONETIZATION=true`** (`server.ts:150`). `partykit.production.json` doesn't set it, so it's off
  unless it was set in the PartyKit dashboard (couldn't verify from the repo). When on, `configure-room{gameId:'kart'}`, the `hello` game adoption and `start-game`
  each need `entitlementToken` = an Ed25519 JWS with `iss` = `ENTITLEMENT_ISSUER` (`https://api.movecade.com`), `aud` = `movecade-client`, `sub` = the host's `hello.identity`,
  an unexpired `exp`, and a grant for `kart` or `*` (`engine.ts:1791-1817`). Otherwise the server answers `ENTITLEMENT_REQUIRED`.
- **How the host gets a token:** `GET {API}/api/entitlements` with header `X-Movecade-Identity: <uuid>` returns `{monetizationEnabled, token|null, offers[]}`.
  `monetizationEnabled:false` is a kill switch (CONTRACT §1).
  Purchase is `POST /api/checkout {offerId, identity, returnUrl}` → `{status:'redirect', url}`. Open it in an external browser; the return URL must be on the CORS list.
  Restore is `POST /api/restore/request|redeem`. Tokens are short-lived. Re-fetch on launch, and send the current token with every premium command (the web reads it fresh each time, `HostSurface.tsx:239,371`).
- Phones never need a game entitlement: "the host pays for everyone" (`backend/main.go:422-426`). Paid cosmetics (outfit, vehicle) are verified per phone token on `join-player`/`set-avatar` and aren't the host's concern.
- UE UX: if `access(kart)` isn't playable, show "Kart Racer is in the Party Pack" with "See the Party Pack" and "Host Bowling free" (`HostRoute.tsx:91-106`). Handle `ENTITLEMENT_REQUIRED` from the server with the same screen.

---

## 11. Things that need a server or protocol change (or a deliberate decision) for a native host

| # | Issue | Options |
|---|---|---|
| **F1** | **Hosting requires a non-null, allow-listed `Origin`** (`engine.ts:395-408`, `hostLifecycle.ts:26-29`). A native socket normally sends none, so the UE host would be a spectator. | (a) **No code change:** send an `Origin` upgrade header. In dev use `http://localhost:5173`. In staging/prod add a dedicated https pseudo-origin (e.g. `https://tv.movecade.com`) to **`EXTRA_ALLOWED_ORIGINS`**, which is config only; prod validation needs it https and non-local. Spoofing `https://movecade.com` also works, but it ties the native host to the web origin and is indistinguishable from it in telemetry. (b) **Server change (recommended long term):** a proper native-host credential, e.g. allow `hello.role:'host'` with a signed host credential or an entitlement token instead of an Origin. |
| F2 | `scores-ack` isn't in the protocol union. It's parsed ad hoc ahead of the parser (`engine.ts:512-517`). | Use it as documented in §7.3. Ideally formalise it in `protocol/src/messages.ts` + README. |
| F3 | README §3–§4 is out of date for a host: it omits `set-countdown`, `return-to-lobby`, `configure-room.mapId`, `state-ack`, `host-lobby-visible`, `action-ack`, `state-observed`, `join-visible`, `room.countdown`, `selectedMapId`, `player.bot/avatar`, `mode/queue`, and `scores-ack`. | Doc only; this spec is the reference. |
| F4 | The kart race is fully host-trusted. The server signs any integer 10000..600000 the host reports. | No change needed for parity. Any anti-cheat would need server-side race telemetry. |
| F5 | There is no host→phone kart feedback (item, position, lap). | Possible today with a host `game-event` (any name ≤48 chars, data depth ≤4). If it must be unforgeable by phones, add it to `HOST_ONLY_GAME_EVENTS` (`protocol/src/constants.ts:151-159`), which is a protocol and server change. Web `KartController.tsx` and iOS `KartActive.swift` need UI work. |
| F6 | The host can't send binary frames to phones (the server drops binary from sessions without a seat, `engine.ts:2233-2234`). | Fine; use JSON `game-event`. |
| F7 | The QR base URL must be a web deploy whose party host matches the UE host's party host. iOS needs HTTPS for motion. | Config/process: keep a URL table per environment in UE settings. |
| F8 | Bots are host-spawned controller sockets with `client:'movecade-bot/2.0.0'` (`constants.ts:175-177`, `host/bots/botClient.ts:357-380`). They use the normal controller protocol, so no change is needed. A UE host can open extra `role:'controller'` sockets (no Origin needed for controllers), `join-player`, `calibrate`, `set-ready`, then drive with `kart-input{steeringInput,…}`. | — |

---

## 12. UE5 mapping (5.8, C++)

### 12.1 Modules
In `Build.cs`: `"WebSockets", "HTTP", "Json", "JsonUtilities", "UMG", "Slate", "SlateCore", "MetasoundEngine", "MetasoundFrontend", "AudioMixer", "DeveloperSettings"`.
QR: vendor Nayuki **qrcodegen** (C++, MIT) and blit its modules into a `UTexture2D` (`FTexture2DMipMap` / `UTexture2D::CreateTransient`, `TF_Nearest`).
Call `FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets")` before the first `CreateWebSocket`.

### 12.2 `UMovecadeRoomSubsystem : UGameInstanceSubsystem` (owns the room for the life of the app)
```cpp
UCLASS() class UMovecadeRoomSubsystem : public UGameInstanceSubsystem, public FTickableGameObject {
  // config (UDeveloperSettings UMovecadeNetSettings): PartyHost, bSecure, Origin, JoinBaseUrl, ApiBaseUrl, ClientId
  void OpenRoom(FName GameId = "kart", const FString& ExistingCode = "");   // mints code if empty
  void CloseRoom(bool bEndForEveryone);
  // host commands → JSON (each sets v:2,t:…)
  void ConfigureRoom(TOptional<FString> GameId, TOptional<FString> MapId);
  void SetCountdown(EMcCountdownKind Kind, int32 DurationMs);
  void StartGame(); void AdvanceTurnSimultaneous(); void UpdateScores(const TArray<FMcScore>&);
  void CompleteGame(const TArray<FMcScore>&); void RestartGame(); void ReturnToLobby();
  void KickPlayer(const FString&); void DropPlayer(const FString&); void SendGameEvent(const FString& Ev, TSharedPtr<FJsonObject> Data);
  void SaveSnapshot(TSharedPtr<FJsonObject>); void AckScores(int32 MatchNumber);
  // state
  const FMcRoomState& GetRoom() const; FString GetJoinUrl() const;     // https://…/join/CODE#v=2&t=TOKEN
  // events (dynamic multicast for UMG + native for C++)
  FOnMcRoomState OnRoomState;  FOnMcError OnError;  FOnMcHostChanged OnHostChanged;
  FOnMcActionAck OnActionAck;  FOnMcResumeCountdown OnResumeCountdown; FOnMcRoomClosed OnRoomClosed;
  FOnMcSignedScores OnSignedScores;  FOnMcGameEvent OnGameEvent;   // (Event, DataJson, FromPlayerId)
  TMulticastDelegate<void(const FString& PlayerId, const FMcSensorFrame&)> OnSensorFrame;
private:
  TSharedPtr<IWebSocket> Socket; int64 HighestStateVersion = -1; int32 Attempt = 0;
  TArray<FQueuedFrame> Outbox; // {Json, bMomentary, EnqueuedAt}
  FString HostToken, JoinToken, Identity, SessionId, Code;   // HostToken/Identity/Code → UMcHostSave (USaveGame)
  TArray<uint8> BinaryAccumulator;
};
```
- **Connect:** `FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""), Headers)` with `Url = (bSecure?"wss":"ws")+"://"+PartyHost+"/parties/main/r-"+Code.ToLower()`
  and `Headers.Add(TEXT("Origin"), Settings->Origin)` (F1).
  Bind `OnConnected` → send hello, `OnConnectionError`/`OnClosed(Status,Reason,bClean)` → backoff unless the close code is terminal,
  `OnMessage(const FString&)` → JSON dispatch, `OnBinaryMessage(const void*, SIZE_T, bool bIsLastFragment)` → accumulate until last → parse.
  Verify on your platform's WebSockets backend (libwebsockets vs WinHttp) that the custom Origin header is really sent. The server logs `security.unauthorized_origin`, and you'll get error `UNAUTHORIZED_ORIGIN` if it's missing.
- **Identity:** `FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)`. Force the version nibble to 4 and the variant to 8–b if the platform GUID isn't v4 (the server regex requires v4). Persist it.
- **Dispatch:** a switch on `t`:
  - `welcome`: store session, tokens, room; watermark.
  - `state`: watermark check, then `state-ack`, then broadcast `OnRoomState` with the parsed `FMcRoomState` USTRUCTs (`FMcPlayer{Id,Name,Emoji,Seat,Score,bReady,bBot,bCalibrated,bConnected,bEliminated,GraceRemainingMs,Screen}`, `FMcGame{Status,…,MatchNumber,StartedAt,PauseReason,PausedForPlayerId}`, `Countdown{Kind,EndsAt}`, `ServerTime`, `SelectedMapId`).
  - `ping`: reply `pong`.
  - `error`: if fatal, stop reconnecting; surface `ENTITLEMENT_REQUIRED`, `UNAUTHORIZED_ORIGIN`, `MATCH_TOO_SHORT`, etc.
  - `signed-scores`: broadcast, then `AckScores` after the POSTs are queued.
- **Threading:** UE WebSockets delegates are fired on the game thread. Keep all room state game-thread-only.
  `FTickableGameObject::Tick` drives backoff timers and a local countdown interpolation (`remaining = endsAt - serverTime - (Now-AnchorLocal)`).
- **Binary parsing:** check the size is 31 or 39, `B[0]==0x02`, `(B[1]&~0x3F)==0`, `B[2]<=3`, and that flag bit5 implies the size is 39.
  Read little-endian with `int16 I16(int o){ return (int16)(B[o] | (B[o+1]<<8)); }`. `Gamma = I16(8)/100.f`, and so on.
  `Seat = B[Size-1]+1` → find `Room.Players` by `Seat` → `OnSensorFrame(PlayerId, Frame)`. Drop frames silently on any failure.
- **Score submission:** `UMcScoreSubmitter` (FHttpModule). It does one POST at a time, builds a key-by-key JSON body exactly as in §7.3, retries with jittered exponential backoff bounded by the
  timestamp + 285 s, dedups on signature, and filters with `IsFinishedScore(game, score)`.
- **Entitlements:** `UMcEntitlementService` does `GET /api/entitlements` with `X-Movecade-Identity`. It caches the token and `exp`, and the room subsystem attaches it to `hello`, `configure-room{gameId}` and `start-game`.

### 12.3 Input: `UKartInputRouter` (owned by the GameMode)
- Subscribes to `OnSensorFrame`, then applies the §6.1 transform per player to get `FKartInputState::Steering`. It keeps last-received timestamps for an optional stale decay.
- Subscribes to `OnGameEvent("kart-input")`. It sets `bAccelerating`, `bReversing` and `bDrifting` from the booleans present, `Steering` from `steeringInput` (bots), and on `useItem` calls `Kart->TryUseItem()` with a 200 ms debounce.
- Exposes `GetInput(PlayerId)` to the kart pawns' physics tick (sample-and-hold; phones only update at 30 Hz).
- Optional: a dev keyboard/gamepad path that writes the same struct.

### 12.4 Race flow: `AKartRaceGameMode` + `AKartRaceGameState`
The match is single-process with no UE replication: phones are not UE clients. Use a GameMode/GameState split for clarity only.
- `AKartRaceGameState`:
  - `ERacePhase {Waiting, Intro, Countdown, Racing, Finished}`, `CountdownValue`
  - `TMap<FString,int32> LapCounts`, `TArray<FString> FinishOrder`, `TMap<FString,int32> FinishTimesMs`, `TMap<FString,TArray<int32>> LapTimes`, `CheckpointProgress`
  - `RaceStartWall`, `TotalPausedMs`, `PausedAtMs`, `bPaused`, `TotalLaps=3`
  - Helpers: `ElapsedMs()`, `DeadlineMs()=min(300000, leader+30000)`, `ProgressScore(id)=laps*100+clamp(pct,0,99)`, `Standings()`.
- `AKartRaceGameMode`:
  - Listens to `OnRoomState`.
  - On `status==in-progress` with a new `matchNumber`: spawn karts for non-eliminated players, sorted by seat (`spawnIndex = order`). Send `AdvanceTurnSimultaneous()`. After 1.0 s go to `Intro` (6.0 s, skippable, waits for streaming).
    Then `Countdown`: send `SetCountdown(Match,5000)`, tick 5..1 at 1 Hz (frozen while paused). Then `Racing`: send `SetCountdown(Match,0)`.
  - On `paused`: freeze the clock, physics input and countdown. On `in-progress` again: resume and add to the paused total.
  - A player eliminated or removed: despawn the kart. If everyone left is finished, go to `Finished`.
  - While `Racing`: every 250 ms check the deadline. Every 1 s send `UpdateScores` (live points) if they changed.
  - On `Finished`: build the final scores (finishers' ms, DNFs' progress), send `UpdateScores` then `CompleteGame`, then send `match:highlights` once the room is `complete`.
  - Results: show `WBP_Results`, keep the level, switch to the end camera. "Play again" → `RestartGame()`. On `status==lobby` → back to the lobby (level or widget).
  - Always integer-round ms.
- Travel: a lobby map, or a persistent level with the lobby UI, and a race map per `selectedMapId` (`violet-loop`, `sunset-harbor`). Load the track **before** sending `StartGame`
  (the web has `preparation.ready` gating start, `HostSurface.tsx:238,335`). Use level streaming so the socket subsystem isn't touched by travel; a GameInstance subsystem survives `OpenLevel` anyway.
- **Split-screen:** create `ULocalPlayer`s (`UGameInstance::CreateLocalPlayer(ControllerId=i, OutError, true)`) for karts 2..N so each kart has a viewport and camera.
  Override `UGameViewportClient::LayoutPlayers` / `SplitscreenInfo` to match §8.1 (3-player = two top + bottom-centred 50%). Feed input from `UKartInputRouter`, not from `UInputComponent`.

### 12.5 UMG
- `WBP_Lobby`:
  - QR `UImage` (dynamic texture from `GetJoinUrl()`, regenerated when `joinToken` changes), the code in a large monospace font.
  - 4 seat cards bound to `FMcPlayer` (connected/ready/grace countdown from `GraceRemainingMs`).
  - `WBP_TrackPicker` (two radio cards from a `UDataTable` of `{Id, Name, Description, Color, OutlinePoints}`) → `ConfigureRoom(MapId)`.
  - Countdown text driven by the local 5 s timer; the start-blocked reason line; kick confirm; Start-solo button; practice toasts.
- `WBP_RaceHud` (one, full screen) has the top bar (timer, finish-window, lead lap, racers), item-slot row, player rail, countdown stage and pause modal.
  `WBP_ViewportHud` (one per split viewport, positioned with the viewport rect) has minimap, speedometer (material with a `Meter` scalar param), roulette, activation popup and screen-effect overlay.
  Drive these from the GameState in `NativeTick`. Avoid per-frame property bindings for 4×N widgets.
- `WBP_Intro`, `WBP_Loading`, `WBP_Results` (rank with `RankByScore`, format `m:ss.mmm`/DNF, leaderboard status line, Play again / Leave), `WBP_HostStandby`, `WBP_RoomFailure`, `WBP_Reconnecting`.
- Mute toggle and SFX volume: a `USoundMix` with `USoundClass` overrides. Persist in `UGameUserSettings` or a SaveGame.

### 12.6 Audio: MetaSounds
- **Engine:** one `MS_KartEngine` MetaSound Source per kart (2D, spatialization off) with:
  - 7 looping Wave Players for the WAVs from §9.2.
  - Float inputs `L_On1000..L_On7000`, `L_Off2500`, `L_Off6500` (layer gains), `Rate_*` (or one `RPM` input with the per-layer `RPM/layerRpm` division done in-graph), `OnBus`, `OffBus`, `Out`, `Pan`.
  - Use **Interpolate**/smoothing nodes with the τ values in §9.2. Output through Stereo Panner → Audio Out.
  - Compute the weights in C++ (port `layerWeights`, the `drive`/`idle` formula and `Drivetrain`) and push them with `UAudioComponent::SetFloatParameter` each tick.
    Alternatively compute them inside the graph with Map Range + Sin/Cos nodes from `RPM`/`Load` inputs.
  - Start Wave Players with a `StartTime = character*duration` offset.
- **Loops** (skid, wind with a Biquad/One-Pole Lowpass `Cutoff` input, offroad, boost, roulette, invincible, rocket): one `MS_KartLoop` per layer per kart, or one multi-layer MetaSound per kart with gain/rate/cutoff inputs.
  Apply the `squealMix`/`windMix` formulas in C++.
- **One-shots:** `USoundWave` assets (or tiny MetaSounds) played with `UGameplayStatics::PlaySound2D(…, Volume=cue.gain*opt.gain, Pitch=rate)`.
  Use a `USoundConcurrency` (Max 24, Resolution StopOldest) on the SFX class, and a C++ per-cue throttle table (`throttleMs`). Pan via a source-effect stereo panner, or per-play `UAudioComponent` with `SetFloatParameter("Pan")`.
- Mix rules to keep: lane pan `0.55` spread by spawn order; per-kart gain `1/√N`; engine output ×0.3; nothing plays on pause or for finished karts; reset on new race.
  Defaults: SFX 0.8, music 0.5. There's no music content yet.

### 12.7 Test plan for the port
1. `npm run dev:party` + `npm run dev`. The UE host connects to `ws://localhost:1999` with `Origin: http://localhost:5173`.
2. Open `http://localhost:5173/join/<CODE>` in 1–3 desktop tabs and use assist mode. Ready up → lobby count → race → results.
   Check the partykit console shows signed scores (dev secret) and that `scores-ack` stops redelivery.
3. Bot clients from UE (F8) to fill seats. A disconnect test: close a tab mid-race; check the pause modal, then reopen within 135 s for auto-resume, or use "Continue without".
4. Real phones on staging (HTTPS) with an `EXTRA_ALLOWED_ORIGINS` entry for the UE host's Origin.
