# Room server change: let the Unreal host host rooms

The game hosts its Party Mode rooms on the production Movecade room server (`party.movecade.com`), so phones join
with the controller they already use: the QR opens `https://movecade.com/join/<CODE>`, and the Movecade app scans
the same QR. Production only lets an allow-listed browser origin host a room, so it needs one server change first.

## Why

On Windows, Unreal's WebSocket client (libwebsockets) always sends `Origin: http://<server host name>`, here
`http://party.movecade.com`. It can't be changed without modifying the engine; adding our own header makes the
request carry two Origins, which the server also refuses. Production refuses that origin (it isn't listed, and it
isn't https), with `UNAUTHORIZED_ORIGIN` / close 4003.

The patch admits exactly that one origin in production (`NATIVE_HOST_ORIGINS` in `partykit/src/config.ts`) and
exempts it, by value, from the https-only startup check.
- It opens nothing to websites. No browser page can present `http://party.movecade.com`, because nothing serves pages
  there, and a non-browser client could already send any Origin it liked.
- Every other plain-http origin is still refused. The new tests in `config.test.ts` and `server.config.test.ts`
  check both directions, and they fail without the change.

## Apply and deploy (from your movecade-v2 checkout)

```bash
git am "C:/Users/Jimmy/Documents/Unreal Projects/MyProject/Docs/Port/server/0001-partykit-let-the-Unreal-kart-host-host-rooms-in-prod.patch"
npx vitest run partykit          # 179 tests, including the four new ones
npm run deploy:production
```

The patch was made on top of movecade-v2 `58bf0476`. All 179 server tests pass and typecheck and lint are clean.

## Check it

1. Run the game, then pick Party Mode.
2. The lobby should say "Room open. Phones join from the QR code."
3. Scan the QR with a phone, or open the link shown under the room code.

Before the deploy, the lobby says the server doesn't accept this game as a host yet. The log line
`party.movecade.com refused this host's Origin` means the same thing.

## What was verified before the deploy

- Against production, the game connects over TLS and is refused only for its origin (`UNAUTHORIZED_ORIGIN`). So the
  address, TLS and protocol are right, and the origin is the only thing missing.
- The full phone flow with the real Movecade controller page (served from this commit) against the Unreal host, on a
  local room server:
  1. the QR link, the name and the avatar;
  2. touch steering;
  3. ready, then the lobby countdown with a second phone;
  4. the race;
  5. live scores shown on the phone.

# 0002: the motion pad (new motion games in Party Mode)

`0002-feat-pad-generic-motion-pad-controller-for-Unreal-ho.patch` lets the room server and the phone web app play
motion games that only the Unreal game hosts: `sumo` (tilt to roll, A to dash), `derby` (swing like a bat, one
batter at a time) and `fishing` (flick to cast, jerk to hook, roll the wrist to reel). It is the server and phone half
of "The motion pad contract" in `Docs/Agents/BRIEF.md`.

## What it changes

- **One list.** `protocol/src/motionPad.ts` `MOTION_PAD_GAMES`. An entry there makes the id a known room game
  (`normalizeGameId`, so configure-room, the host handshake, scores and signing take it), puts it in the free, premium
  and simultaneous lists and `GAME_MAX_SCORE`, lets a phone calibrate for it (`isMotionGame`, with its own grip text),
  and gives its phones the motion pad. To add a game later, add one entry: id, name, tier, `playMode`, score ceiling,
  default hint `mode`, `sendHz`, default button labels, accent and grip.
- **Turn order.** `sumo` and `fishing` are simultaneous: every phone streams at once and the server never names a
  player. `derby` is turn-based, like bowling and darts. The match starts with seat 1 active, the host names each
  batter with advance-turn (`URoomHostSubsystem::AdvanceTurn(PlayerId)`), and the other phones wait on "X's turn". If
  the batter's phone drops, the match pauses, as it does in bowling.
- **Premium.** All three are premium, like every game except bowling (`FREE_GAME_IDS` stays `['bowling']`). Premium
  gates nothing unless the deployment sets `ENABLE_MONETIZATION=true`. Local dev doesn't set it and current production
  doesn't either, so the games are playable in both. If monetization is turned on, they need a Party Pack grant like
  kart, samurai and the rest.
- **Web catalog untouched.** The ids are not in `protocol/game-manifest/catalog.json`, so the web picker, homepage,
  SEO, leaderboards and the generated iOS and Go catalogs don't change. A consequence is that the Go backend doesn't
  know these ids yet. A leaderboard submission of their signed scores is refused until they are added there. The
  room itself still signs and relays results.
- **Phone.** `findGame` falls back to the pad plugins (`app/src/games/pad`). In the match, the phone shows:
  - the host's latest `pad:prompt` for this player, big: the title, the subtitle and a status region that is
    announced politely. The pad also accepts `playerId: "*"` for every phone; a player's own newer prompt wins;
  - one or two big buttons (at least 96 px tall, or 22% of the screen). They send `pad:press` and `pad:release` with
    `{ button: "a" | "b" }` on pointer down and up, buzz on press, and also work from the keyboard. A held button is
    released when the page hides, when the pad unmounts, or when the host drops that button from the prompt;
  - a hint animation per `mode` (`tilt`, `swing`, `aim`, `flick`; still under reduced motion), plus a live lean dot
    for tilt and aim.

  The controller shell streams the calibrated sensor frames at the game's `sendHz` while the phone is active: 100 Hz
  for derby and fishing (like samurai), 60 Hz for sumo. Before the first prompt, the pad shows the game's own verb,
  hint and button labels. Prompts are collected from the moment the room is a pad room, so a prompt sent just before
  a phone's turn begins is still shown. A prompt belongs to the match it arrived in.
- **Security.** `pad:prompt` is host-only (`HOST_ONLY_GAME_EVENTS`), so one phone can't rewrite another's
  instructions. `pad:press` and `pad:release` are ordinary controller events.

Host side: `URoomHostSubsystem::SendPadPrompt(PlayerId, Title, Subtitle, A, B, AccentHex, Mode)`, plus
`Net::ReadPadButton` for the `OnGameEvent` data. Resend a prompt whenever the turn or phase it describes changes.
Game events are momentary: one queued for more than 1.5 s while the socket was down is dropped, and a phone that
rejoins shows the defaults until the next prompt.

## Apply and deploy (from your movecade-v2 checkout)

The patch was made on top of movecade-v2 `1bec8a64` (0001 applied) and applies there with `git am` unchanged.

```bash
git am "C:/Users/Jimmy/Documents/Unreal Projects/MyProject/Docs/Port/server/0002-feat-pad-generic-motion-pad-controller-for-Unreal-ho.patch"
npx vitest run partykit protocol    # 303 tests (200 partykit + 103 protocol), 23 of them new
npx vitest run app/src/games/pad app/src/controller app/src/games/manifest   # 192 tests, 16 of them new
npm run typecheck && npx eslint . --max-warnings=0
npm run deploy:production           # the room server
```

Then deploy the web app (movecade.com) the usual way. Phones load the pad from the web app, so the server deploy
alone is not enough. Until both are live, production refuses `configure-room` for these ids with
`MALFORMED_MESSAGE` "Unknown gameId".

## Test results

These were run in a scratch clone at `1bec8a64` plus this patch, on Windows with Node 22.13:
- partykit: 200 tests pass (12 new, in `engine.motionPad.test.ts`). They cover configure, calibrate and start for
  each id; derby's turns; sumo with no turns; live scores; the `pad:press` relay; `pad:prompt` delivered and forgery
  refused; unknown ids still refused; and the id taken from the host handshake.
- protocol: 103 tests pass (11 new in `motionPad.test.ts`; `scoring.test.ts` updated for the longer premium and
  simultaneous lists).
- app: 16 new tests: 15 in `MotionPadController.test.tsx` and 1 in `ControllerSurface.test.tsx`, which checks that a
  derby phone gets the pad with the prompt that arrived before its turn.
- The whole vitest run: 2735 of 2751 pass. The 16 failures are the same 16 that fail on `1bec8a64` without the
  patch on this machine: `seoGenerator` output paths, `deploy-party-production.test.mjs`, and the `phase15`
  measurement scripts. They are Windows and environment problems, not related to this patch.
- typecheck and eslint are clean. oxlint is clean when run with `NODE_OPTIONS=--experimental-strip-types`. Without it,
  Node 22.13 can't load `oxlint.config.ts`, because oxlint needs Node 22.18 or later.

## Run it locally (the room server from the patched clone)

```powershell
# a scratch clone with the patch applied (git-ignored)
git clone --local C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2 Tools\.cache\movecade-v2-pad
git -C Tools\.cache\movecade-v2-pad am "$PWD\Docs\Port\server\0002-feat-pad-generic-motion-pad-controller-for-Unreal-ho.patch"
# serve it (npm ci on first use). Any -Source other than the original checkout is served in place.
.\Tools\net-sim\run-party.ps1 -Source .\Tools\.cache\movecade-v2-pad            # ws://localhost:1999
# for real phones, also serve its web app (phones join at http://<this PC>:5173):
#   cd Tools\.cache\movecade-v2-pad; npx vite --host
```

Only one server can hold port 1999. Stop the unpatched one first, or pass `-Port 1998` and point the game at it
with `-RoomServer=127.0.0.1:1998`. The simulated phone covers every pad game: `node Tools/net-sim/phone-sim.mjs
--game sumo|derby|fishing|pad ...`. It streams a rocking attitude, presses and releases A every 1.5 s (derby only on
its turn) and logs each `pad:prompt` addressed to it. Game agents tune their game's entry in `PAD_PROFILES`.

Verified end to end against the patched server on port 1998. A stand-in host script took the place of the UE game,
which doesn't have these games yet. The script did the following:
- configured the room as derby, then fishing, then sumo;
- seated `phone-sim --game sumo`, started the match and sent `pad:prompt` every 2 s.

The phone logged all 5 prompts. The host received 6 `pad:press`, 6 `pad:release` and 600 sensor frames in 10 s. The
same run with `--game derby` started with `activePlayerId` = the phone and streamed at 100 Hz.
