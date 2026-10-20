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
