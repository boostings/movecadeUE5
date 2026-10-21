# Perf audit: Bowling, Darts, GameAudio (2026-10-21)

Verified against UE 5.8 source: `UTextRenderComponent::SetText` always dirties render state; `SetMaterial`/`SetVisibility` with unchanged values early-out; `USceneComponent` defaults to Movable. None of these touch rules/RNG/fixed step.

1. HIGH: Chalkboard text rebuilt every frame. `DartsGame.cpp:548-556`, `DartsPub.cpp:614-623`: Leaderboard() + 4 Printf + 4 SetText each tick (4 text proxies recreated/frame). Rebuild only when a score hash changes; SetText only if text differs.
2. MED: `KartMaterials.cpp:110-122` `Glow()` uses translucent `M_TrackTranslucent` (opacity 1) for big opaque surfaces (bowling masking panels, cove/accent strips, trough lenses, guide dots; darts windows, back bar, lamp strip, bulbs) -> translucency pass, overdraw. Add an opaque unlit emissive master for opacity-1 glow; keep translucent only for bounty wedge/reticle. (Shared file: coordinate.)
3. MED: `GameAudio.cpp:68` `FCuePlayer::Play` does Printf + LoadObject per play; first play is a sync disk load mid-game, nothing holds the wave so GC can unload. Preload all cue waves once into strong refs (`TStrongObjectPtr`), static FNames at call sites.
4. MED: `SBowlingHud.cpp:120-128` score sheet: FrameMarks() TArray<FString> + Join + FromInt per frame per seat (~150 allocs/frame). Cache per seat keyed by Rolls.Num().
5. LOW-MED: name fitting re-measures fonts each paint `SBowlingHud.cpp:97-108`, `SDartsHud.cpp:81-91`. Cache fitted name per seat keyed by scale.
6. LOW-MED: ProjectWorldToScreen per callout `SDartsHud.cpp:183,199`, `DartsGame.cpp:172-187`. Compute view-projection once per paint (ULocalPlayer::GetProjectionData + FSceneView::ProjectWorldToScreen).
7. LOW-MED: `DartsGame.cpp:691-715` copies TArray<FDartLanding> (FStrings) every frame; `ShownVisit` write-only. Use pointer views, delete ShownVisit.
8. MED (GPU): lights. Bowling `BowlingAlley.cpp:385-428` 9 movable lights, 3 shadowed (point light :406 cube VSM, 15.6 m rect :424). Darts `DartsPub.cpp:548-586` pendant point light casts shadows (:579) invalidated by moving avatar/darts; 16000 lm fill radius 20 m in 8x22 room (:583). Drop point-light shadows / make pendant a spot; shrink radii.
9. LOW-MED: venue ISMs inherit Movable (`BowlingAlley.cpp:97-105`, `DartsPub.cpp:148-156`, racks :444, bottles/board `DartsPub.cpp:178`). Make root + static groups Static before RegisterComponent (Static child under Movable parent aborts attach). Keep BoardPivot subtree Movable.
10. LOW-MED: duplicate MIDs split ISM groups (Plain/Surface create a new MID each call; Group keys by MID name). Bowling Machine/Dark (:235-236 vs :282-283); Darts WoodDark (:346 vs :397), 3 brass (:348/399/466), bottles. Cache materials by key per Build().
11. LOW-MED: 10 pin components x2 sections (`BowlingGame.cpp:111-122, 717-745`) -> one ISM, BatchUpdateInstancesTransforms. Darts 13 comps on 6-section mesh (`DartsGame.cpp:83-102`, `DartsPub.cpp:263-313`) -> merge metal sections; no shadows on in-flight darts.
12. LOW: tiny lane arrows/dots cast shadows `BowlingAlley.cpp:251,255`.
13. LOW: rake/setter moved while hidden `BowlingGame.cpp:759,761`; skip PlaceRack for hidden pins.
14. LOW: reticle double transform `DartsGame.cpp:831-832` -> one SetWorldTransform.
15. LOW: FNames from strings in hot paths (MID params `BowlingGame.cpp:801`, `DartsGame.cpp:792,833`; `BoneWorld(TEXT("rightHand"))` `BowlingGame.cpp:639` x2, `DartsGame.cpp:899`, double bone lookup; cue names). Static FNames / cached bone index.
16. LOW: GetBowler/GetThrower/SeatOf linear FString searches 6-8x per tick + per paint (`BowlingGame.cpp:501,570,766,792,912`). Cache current seat index on turn change.
17. LOW: results paint sorts/builds each frame (`SBowlingHud.cpp:231,238,162`, `SDartsHud.cpp:273,223`). Cache at Finished / turn change.
18. LOW: `BowlingThrow.cpp:64-67` History.RemoveAt(0) per sensor frame -> ring buffer (identical outputs).
19. LOW: `SDartsHud.cpp:142` Polygon initializer TArrays per paint.

Correctness:
- Camera actors spawned in Setup never destroyed (`BowlingGame.cpp:177`, `DartsGame.cpp:129`) -> destroy in EndPlay.
- Game-mode lambdas capture raw `this` (`BowlingGameMode.cpp:109,202,220`, `DartsGameMode.cpp:83,182,197`) -> weak guards.
- Dead state: ShownVisit, ReticleShown, bFinishedFired (`DartsGame.h:188,203,205`).
- EndPlay comments claim timers live on the game instance: wrong (world owns timer manager); fix the comment.
