# Brief for parallel agents (read fully before starting)

You are one of several agents improving **Movecade Kart**, a UE 5.8 C++ party game headed for a paid Steam release.
It has a kart racer plus arcade party games (Bowling, Bullseye Bash darts, Samurai Slice, Sky Stacker). Phones join
through a QR code and are motion controllers in Party Mode. Local play uses keyboard, mouse and gamepad, plus house bots.
The bar is **commercial AAA-casual quality**: polished, juicy, readable, fast. Verify everything in the real game.

## Your workspace
- You run in your **own git worktree** (a full copy of the repo, including LFS content). Work and commit **only there**,
  on your worktree's branch. Never touch `C:\Users\Jimmy\Documents\Unreal Projects\MyProject` itself (the main
  checkout: other agents and the lead work there). Never push. Never rewrite history.
- Never modify `C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2` (the read-only web original). If you need
  to change the room server or phone app, follow "Server and phone changes" below.
- Use only free assets. Prefer geometry and materials built in code (see `Art/KartMeshBuilder.h`, `Art/KartMaterials.h`,
  and the venue actors such as `Games/Stacker/StackerRooftop.cpp`). Don't download anything.
- Commit messages: conventional style (`feat(x): ...`, `perf(x): ...`, `fix(x): ...`), ending with the line
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Make small, coherent commits as you go.

## Build, test, run (PowerShell; `$p` = your worktree root)
- Build: `& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MyProjectEditor Win64 Development "-Project=$p\MyProject.uproject" -WaitMutex -NoHotReloadFromIDE`
  (UBT serialises builds across worktrees, so you may wait for the mutex. The first build in a fresh worktree is a
  full module build.) Filter the output: `| Select-String -Pattern "error|Result:"`.
- Tests: `.\Tools\ue\test.ps1` (automation tests). **All tests must pass** before you finish: 70 today, plus yours.
- Play and screenshot: `.\Tools\ue\play.ps1 -Map L_X -Shots '5,20' -QuitAfter 25 -NoAutoDrive -Extra '-FooPlayers=0 -FooBots=3'`.
  Screenshots land in `Saved\Screenshots\WindowsEditor\`; **look at them** with the Read tool. The log is `Saved\Logs\KartGame.log`.
  Keep runs short and always pass `-QuitAfter`: several agents share this PC's GPU and RAM.
- Maps for party games: `.\Tools\ue\run.ps1 make_game_map.py @{ KART_GAME = 'id' }` (add your game to `GAMES` in `Tools/ue/make_game_map.py`).
- UI tour: `.\Tools\ui\shots.ps1 -Steps 'menu,arcade'` (quote comma lists).
- Party Mode end to end (needs the local room server): `.\Tools\net-sim\run-party.ps1` (in the background), then
  `.\Tools\net-sim\e2e.ps1 -RoomGame <id> -QuitAfter N -SimSeconds N -Shots '..' -Bots 0`. The simulated phone is
  `Tools/net-sim/phone-sim.mjs --game <id>`. The lead's server may already be running on :1999; if so, reuse it.

## Pitfalls we hit before
- Bash heredocs with apostrophes break; write scripts to a file. PowerShell `Get-Content`/`Set-Content` mangle UTF-8
  and add a BOM. Use Python (`newline=""`), or the Edit/Write tools.
- The module has **no RTTI** (`dynamic_cast` fails to compile).
- Timers: the world timer manager outlives level travel in practice. Always use
  `FTimerDelegate::CreateWeakLambda(this, ...)` and clear timers in `EndPlay`.
- `NewObject` with a fixed name on the same outer reconstructs a live object: use `MakeUniqueObjectName`.
- Static child under a Movable parent aborts the attach. Set the root's mobility first.
- Coordinates: web (three.js) to UE is `UE.X = -z*100, UE.Y = x*100, UE.Z = y*100` (`Coords::ToUE`, `DirToUE`).
- Material slot order = order of first `Section()` call in `FMeshBuilder`.
- Without a sun, auto exposure drifts. Indoor venues set their own PostProcess exposure (see `DartsPub`).
- Deterministic gameplay: rules run on a fixed step in a pure module with golden tests. Never change a ported game's
  rule outputs or RNG sequences (the golden tests must keep passing untouched).

## The arcade framework (already built: use it, don't fork it)
- `Games/ArcadeCatalog.h/.cpp`: the one list of party games (id, name, tagline, how-to, map, accent, max players, max
  local, results delay). A new game adds one entry. The picker, lobby, room and "next game" read it.
- `Games/ArcadeGame.h` (`IArcadeGame`): every game actor implements it. It provides id, finished age, pause, restart,
  standings, and commit-results.
- `Games/SArcadeOverlay.h`: the game mode calls `Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch)` after
  spawning the game, and `Overlay->Detach()` in `EndPlay`. It provides the pause menu, the results bar (play again,
  next game, change game, menu) and the party points. Games **don't** handle Esc/Enter at results themselves.
- `Games/ArcadeSession.h` (`UArcadeSession`): seats across games, the party table, the queued room game.
- Look at `Games/Stacker/*` as the most recent complete example of the full pattern: the rules module with golden
  tests, the venue actor, the game actor with a fixed step, the `S*Hud` SLeafWidget painted with `FKartPainter`
  (`UI/KartUIPainter.h`, `UI/KartUIStyle.h`), the game mode with `?Players=&Bots=` and `-<Game>Players=`, the
  `-KartShots`/`-KartQuitAfter` hooks, and the room glue (sensor frames, game events, feedback, live scores, completion).
  Audio: `Games/GameAudio.h` (`FCuePlayer`, cues in `GameAudio.cpp`). FX: `FX/KartFxSubsystem.h`.
  Avatars: `Avatar/ArcadeAvatar.h` (`UArcadeAvatarComponent`, pose input) with `FKartLook`.
- **File ownership, to keep merges clean:** edit freely inside your own area (listed in your task). Shared files
  (`ArcadeCatalog.cpp`, `KartFrontEndScreens.cpp`, `GameAudio.cpp`, `make_game_map.py`, `phone-sim.mjs`,
  `e2e.ps1`, `RoomHostSubsystem.*`, `KartMaterials.*`, `KartUIPainter.*`) get **minimal, additive** edits only
  (append an entry or a case), and you mention each one in your report. Don't reformat shared files.

## Server and phone changes (only the agents told to)
The phones run the movecade-v2 web app, and rooms run on its PartyKit server. Both are read-only here. Changes go
in as **patch files** in `Docs/Port/server/` (see `0001-*.patch` and `README.md`): make a scratch clone under your
worktree's `Tools/.cache/` (git-ignored), commit there, then `git format-patch` into `Docs/Port/server/`, with tests.
The local room server for e2e can run from your patched clone: `run-party.ps1 -Source <your clone>` (check the script).

## The motion pad contract (for new motion games)
New games can't rely on a game-specific phone screen. They use a **generic "motion pad"** phone controller, which a
dedicated agent is adding to the web app and server as patch `0002`. Build against this contract:
- Phones stream the existing binary sensor frames (`Net/RoomProtocol.h` `FSensorFrame`): calibrated attitude
  quaternion, gyro (deg/s), accel (m/s^2) and gravity, at 60-100 Hz. The host receives them through
  `URoomHostSubsystem::OnSensorFrame` (seat index), exactly as Samurai and Stacker do today.
- One big button: game events `pad:press` and `pad:release` with data `{ "button": "a" }`, plus an optional second
  button `"b"`. Received through `URoomHostSubsystem::OnGameEvent(From, Event, Data)`.
- Host to phone: `SendPlayFeedback(...)` (toasts and haptics) as today, plus `SendGameEvent("pad:prompt", json)`
  with `{ "playerId", "title", "subtitle", "a": "<label or empty>", "b": "<label or empty>", "accent": "#rrggbb", "mode": "tilt|swing|aim|flick" }`.
  The phone shows the prompt big and the button labels, and `mode` picks the phone's hint animation.
- Room game ids for new games are the catalog ids. The patch adds them to the server's allowed and simultaneous
  lists, and maps them to the motion pad on the phone.
- `phone-sim.mjs --game <id>` must get a mode for each new game (stream plausible motion, press buttons) so e2e can play it.

## When you finish
- The build is green, all tests pass, your screenshots look great, and the e2e is run where it applies.
- Reply with a concise report: what you built or changed, the commits (hashes) on your branch, the shared files you
  touched, anything you could not verify, and the paths of the 2-4 best screenshots.
