# Arcade API: shared helpers for the party games (Phase B migration)

Everything below lives in `Source/MovecadeKart/{Public,Private}/Games/` (top level). Each helper has an automation
test in `Private/Tests/ArcadeFrameworkTests.cpp` (`MovecadeKart.Arcade.*`). Migrating a game must not change its
behaviour: same seats, same room traffic, same visuals, golden tests untouched.

## 1. `AArcadeGameModeBase` (`Games/ArcadeGameModeBase.h`): replaces each `A<Game>GameMode`'s boilerplate

What the base does (in this order, in `StartPlay`):
1. `SpawnVenue()` (yours).
2. Parses the launch once: `FArcadeLaunchOptions` = travel URL `?Players=&Bots=`, then `-<Prefix>Players=`,
   `-<Prefix>Bots=`, `-<Prefix>Auto` (not in Shipping), clamped with `Arcade::ClampSeats` (catalog MaxLocal/MaxPlayers).
3. Party Mode if the room runs a match of `ArcadeId`: room players sorted by seat, eliminated skipped, capped at the
   catalog's MaxPlayers, passed to `BuildRoomSeats`; otherwise `BuildLocalSeats(Launch)`.
4. `SpawnGame()` (yours; must call `SpawnArcadeGame<T>()`).
5. Party Mode: `SetPreparationReady(true)`, binds `OnReturnedToLobby` (travel to the front end) and `OnPauseChanged`
   (pause through `IArcadeGame::SetArcadePaused`), applies a pause already in force, forwards `OnSensorFrame` and
   `OnGameEvent` with weak lambdas, calls `BindRoomCallbacks(Room)`, and starts the 1 Hz live-score timer.
6. Input mode game-only, cursor hidden; local launches are noted in `UArcadeSession` (play again / next game).
7. Attaches `SArcadeOverlay`; arms `-KartShots=` / `-KartQuitAfter=` (`FArcadeTestHooks`).

`EndPlay` detaches the overlay, clears every timer it armed and unbinds everything from the room.

Configuration (set in your constructor):

| Member | Meaning |
|---|---|
| `ArcadeId` | catalog id, e.g. `TEXT("stacker")` (must exist in `ArcadeCatalog.cpp`) |
| `CommandLinePrefix` | `TEXT("Stacker")` for `-StackerPlayers=` (Bowling keeps `TEXT("Bowl")`) |
| `DefaultLocals`, `DefaultBots` | defaults when neither URL nor command line says (Sumo/Fishing use 2 bots) |
| `FrontEndLevel` | defaults to `L_SunsetHarbor` |

Virtuals:

| Virtual | Required | Notes |
|---|---|---|
| `SpawnVenue()` | no | find-or-spawn your venue actor |
| `BuildRoomSeats(TConstArrayView<FMcSeat> Players)` | yes | one seat per player (`PlayerId`, `Name`, `PaintColor`, `bBot`, `SeatIndex()`) |
| `BuildLocalSeats(const FArcadeLaunchOptions&)` | yes | `Locals` people then `Bots` house players; `bAutoPlay` makes locals bots |
| `SpawnGame()` | yes | `Game = SpawnArcadeGame<AMyGame>(); Game->Setup(...); Game->OnQuit = MakeQuitCallback(); Game->OnRematch = MakeRematchCallback();` |
| `BindRoomCallbacks(URoomHostSubsystem&)` | no | turns, toasts, prompts; `Game->OnFinished = [this] { CompleteRoomMatch(); }` |
| `HandleSensorFrame(const FMcSeat&, const FSensorFrame&)` | no | e.g. `Game->FeedPhone(Seat.PlayerId, Frame)` |
| `HandleGameEvent(From, Event, Data)` | no | compare event names case-sensitively; Data may be null |
| `CollectScores(TArray<FMcScoreEntry>&, bool bFinal) const` | yes | return false to send nothing yet (a countdown) |
| `HandleRoomPaused(bool, Reason, PlayerId)` | no | default: `IArcadeGame::SetArcadePaused` |

Helpers: `GetRoom()`, `IsRoomMatch()`, `GetLaunch()`, `PushLiveScores()` (sends only when the list changed and the
room accepted it; call it after a scoring event too), `CompleteRoomMatch()`, `RequestRematch()`, `TravelToFrontEnd()`,
`MakeRematchCallback()` (room: restart through the lobby; local: empty, the game restarts itself),
`MakeQuitCallback()` (room: as rematch; local: front end).

### Migration recipe (per game)
1. Header: derive from `AArcadeGameModeBase`; delete `SetupTestHooks`, `SetupRoom`, `BindRoom`, `PushScores`,
   `HandleRoomLobby`, `HandleRoomPause`, `Overlay`, `GameEventHandle`, `SensorHandle`, `ScoreTimer`, `LastScores`,
   `bRoomMatch` and the `StartPlay` / `EndPlay` / `RestartPlayer` overrides (keep `EndPlay` only for your own timers,
   calling `Super`). Keep `GetGame()` and the venue/game `UPROPERTY`s. Keep the seat array as a member
   (`TArray<FStackerSeat> Seats`) that `BuildRoomSeats` / `BuildLocalSeats` fill and `SpawnGame` consumes.
2. Constructor: `ArcadeId`, `CommandLinePrefix`, `DefaultBots` (no pawn/controller lines: the base sets them).
3. Move the room-seat loop body into `BuildRoomSeats`, the local loop into `BuildLocalSeats` (use `InLaunch.Locals`,
   `.Bots`, `.bAutoPlay`; keep your house names, skills, colours and looks exactly).
4. `SpawnGame`: spawn with `SpawnArcadeGame<T>()`, `Setup`, then `OnQuit = MakeQuitCallback()` and
   `OnRematch = MakeRematchCallback()` (this reproduces today's "local OnQuit opens the front end; room OnQuit =
   OnRematch = RestartGame").
5. Move the game-specific lambdas from `BindRoom` into `BindRoomCallbacks` (capture `WeakRoom`/`this` weakly as today).
   `OnFinished` becomes `CompleteRoomMatch()`. Sensor/game-event lambdas become `HandleSensorFrame`/`HandleGameEvent`.
6. `PushScores(false)` body becomes `CollectScores` (keep the order and any "not during the countdown" rule).
   Calls of `PushScores(false)` after a play become `PushLiveScores()`.
7. Extra game flags (`-DartsLocalAuto`, `-<Game>Seed=`, `-SumoSkill=`): parse them once into a small
   `F<Game>DebugFlags` struct (`#if !UE_BUILD_SHIPPING` where they change gameplay).
8. Build, run `Tools/ue/test.ps1` (all pass, goldens untouched), a local run with `-<Prefix>Players=0 -<Prefix>Bots=3`
   and screenshots before/after, and `Tools/net-sim/e2e.ps1 -RoomGame <id>`.

Behaviour notes: live scores used to be compared differently per game (Stacker by string key, others not at all);
the base compares the score list and sends at most once a second, which is what the room accepted anyway.

## 2. HUD helpers (`Games/ArcadeHud.h`)

- `FArcadeHudView`: `const FArcadeHudView View = FArcadeHudView::Capture(Viewer);` once per `OnPaint`, then
  `View.Project(WorldCm, Out01)` (0..1 of the whole viewport: what Stacker/Samurai do) or
  `View.ProjectToViewRect(WorldCm, Out01)` (0..1 of the constrained view rect: what Darts does). Replaces
  `FStackerView`, `FSamuraiView`, `FDartsView` and their `GetView()` / `Project()` copies.
- `FCachedText` (`TArcadeCachedText<int32>`) / `FCachedFloatText`: `mutable FCachedText Clock;` then
  `P.Text(Clock.Get(Seconds, [](int32 S) { return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60); }), ...)`.
  The formatter runs only when the value changes. Replaces Stacker's `Cached(...)` helper and ad-hoc caches.
- `FArcadeStandingsOrder`: `mutable FArcadeStandingsOrder Ranks;` then
  `const TArray<int32>& Order = Ranks.Update(Towers.Num(), [&](int32 i) { return Towers[i].Score; });` best first,
  ties in seat order (stable), re-sorted only when a key changes. Return a composite `int64` key for tie-breaks.

## 3. Venue helpers (`Games/ArcadeVenueKit.h`)

- `FArcadeInstanceGroups` (USTRUCT; keep as a `UPROPERTY()` on the venue): replaces each venue's
  `TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups` + `Group(...)`.
  `Groups.Init(this, TEXT("PubGroup"), PubTag, EComponentMobility::Static)` once (Derby: `Movable`), then
  `Groups.Find(Mesh, Material, bShadows, Parent = nullptr, Material2 = nullptr)->AddInstance(...)`. Keys are object
  keys, not formatted strings. Components are created with unique names, mobility set before attaching, no collision,
  tagged, registered.
- `FArcadeMaterialCache` (USTRUCT, `UPROPERTY()`): replaces `TMap<FString, TObjectPtr<UMaterialInterface>> Materials`
  + `Cached(Key, Make)`: `Materials.FindOrAdd(TEXT("wood"), [&] { return Art::Plain(this, ...); })` (FName keys).
- Call `Reset()` on both when the venue rebuilds.

## 4. Test hooks (`Games/ArcadeTestHooks.h`)

`FArcadeTestHooks::Parse(FCommandLine::Get()).Schedule(*this, TimerHandles)`: `-KartShots=t1,t2` and
`-KartQuitAfter=N`, weak timers, handles returned so `EndPlay` clears them. The base and `AKartGameMode` use it; games
that derive from the base get it for free.

## 5. Room host (`Net/RoomHostSubsystem.h`) changes games can rely on

- Public API unchanged, except `UpdateLiveScores` now returns `bool` (true: the room has this list).
- `GetLobbyRevision()` moves whenever anything the lobby shows changes.
- `MovecadeKart::Net::EvaluateStart` / `DescribeStartBlock` (string-free start rules) and `ParseServerMessageType`
  in `Net/RoomProtocol.h`. Event names the host itself consumes: `KartInputEvent`, `PracticeActionEvent`.
