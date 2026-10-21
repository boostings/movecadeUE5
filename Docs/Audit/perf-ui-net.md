# Perf audit: UI and Net (2026-10-21)

1. HIGH (risk med): every painter primitive takes its own Slate layer (`KartUIPainter.h:82` NextLayer per Box/Text/Gradient/Polygon/Lines, `KartUIPainter.cpp:52,64,111,125,130,159`) -> no batching, ~150-300 layers per HUD frame. Group primitives per card/row; bump layers only where overlap order matters. Measure `stat Slate` batches before/after.
2. MED-HIGH: minimap rebuilt every frame (`SKartRaceHud.cpp:548-573`): ~180-pt Road array without Reserve, Lines() copies twice, Order array StableSorted. Cache normalized centreline per track; TConstArrayView overload; inline allocators.
3. MED: `Font()` builds FSlateFontInfo + FName("Regular") lookup per call (`KartUIStyle.cpp:106-120`), Text() copies font twice and measures (`KartUIPainter.cpp:152,157`). Static FName, small font cache.
4. MED: LOCTEXT / FText::Format / Printf every paint (SKartRaceHud.cpp:224,235,326,347,414,476,494,669,676; KartFrontEndScreens.cpp:390-398,670-673,1298-1303,1322-1323; KartRaceScreens.cpp:262-264,302,306,312). Static const FText; cache formatted values on change.
5. MED (menus): WrapLines every frame (`KartUIPainter.cpp:163-200`; callers KartFrontEndScreens.cpp:436,693,1500,1617; KartSettingsScreen.cpp:476). Cache wrapped lines.
6. LOW-MED: Polygon/Lines/Gradient/arcs allocate per call (`KartUIPainter.cpp:97-111,66-67`; KartWidgets.cpp:86,91; KartItemIcons Arc; SKartRaceHud.cpp:249,584,645; SKartCurtain.cpp:76). TConstArrayView, reused static vertex arrays, cached white-box handle.
7. LOW-MED: Paint mutates session state: `Setup.Sanitize` per frame (KartFrontEndScreens.cpp:1290-1291; Racers PaintCard -> Choice() -> Sanitize at :1022-1029,1099). Sanitize on change/OnShown only.
8. LOW-MED: RoomHostSubsystem Tick builds strings (`CanStart()` -> GetStartBlockedReason Printf; countdown key Sort+Join every tick; :823-861, :890-943, :1181). Bool ComputeCanStart; key on state change.
9. LOW: GatherViews / GetHints return TArray by value per paint (SKartRaceHud.cpp:165/213; KartScreen.cpp:302; lobby backdrop KartLobbyWidget.cpp:186-193).
10. LOW: ActiveBoost FString compares per view per frame (SKartRaceHud.cpp:42-43,63).
11. LOW: KartItemIcons Hex() per frame (:12-15,:167,:182) -> static colours.
12. LOW: sync session save JSON + file write every 10 s on game thread (RoomHostSubsystem.cpp:1182-1185,1333-1345) -> async.
13. LOW: lobby Refresh every 100 ms (~20 SetText) and TActorIterator<ATrackBuilder> per frame in UpdateBackdrop (KartLobbyWidget.cpp:416,439-444,508-634). Refresh on state change; cache bounds.
14. LOW: inbound JSON type dispatch via ~10 case-insensitive compares (RoomHostSubsystem.cpp:336-421).
15. LOW: sensor path lean; skip accumulator copy for single-fragment frames (:619-668).
16. NOTE: screen stack/HUD rely on full repaint; global invalidation would freeze them unless volatile.

Lobby/flow bugs (fixed in the arcade-flow work):
1. CloseRoom/new room doesn't reset HighestStateVersion, StartedMatchNumber, AdvancedMatchNumber, SubmittedMatchNumber, AckedMatchNumber, Room, Inputs, LastLiveScores, countdown, bStartSent (RoomHostSubsystem.cpp:162-184, 4100 path :244-250).
2. bStartSent can stick if start-game lost across reconnect (:483-486, :596-609): clear in HandleWelcome when Lobby, or time out.
3. SelectGame sets local GameId before checks (:800) and never syncs from Room.GameId; lobby UI and countdown key use the local value while start uses Room.GameId (KartGameMode.cpp:178).
4. Room in Complete status at lobby is a dead end (KartGameMode.cpp:159-167; GetStartBlockedReason :833-836): nothing sends restart-game.
5. Menu music not restarted in HandleReturnedToLobby (KartLobbyWidget.cpp:700, :715-731).
