# Perf and modern C++ pass (branch perf/cpp-modernize)

Approved 2026-10-20. Execute exactly these files; stop and ask before touching anything unlisted.

**Gate per phase** (Mac): `gate.sh <worktree> <label>` (MyProjectEditor build + every MovecadeKart test),
`KartSimFingerprint` lines compared to the current baseline (must be identical from phase 2 on), and a headless
bot race on both maps (`UnrealEditor-Cmd MyProject.uproject /Game/Maps/<map> -game -nullrhi -nosound -unattended
-KartAutoDrive -KartSkipIntro -KartQuitAfter=25`) with no crash or ensure from LogKart*. Game target compiled at the end.

**Hard rule:** no sim result may change. No FP reordering, no float/double swaps, no approximate maths. Golden
tests (net, audio, QR, render budget) must not change.

Done before this plan: e33a905 Mac clang build fix, afc95be fingerprint test.

- [x] **1. Fingerprint hardening + FP semantics.** Done: gate 54/0, smoke clean on both maps. New baseline:
  harbor 42=791d13e2534f8623 7=7e56bed949ef74f5, violet 42=5086f1ecca449878 7=13b195fcefad916c.
  - Tests/KartDeterminismTests.cpp: item clock `Step / 60.0` (as `GetItemTime`); hash slots, projectiles, box
    `CollectedAt`, RNG seed every step; contexts from the shared standings helper.
  - Sim/KartItems.h: `RandomSeed() const`.
  - Race/KartRaceManager.cpp/.h + new Public/Race/KartStandings.h: pure placing/context maths used by game and test.
  - MovecadeKart.Build.cs: `FPSemantics = FPSemanticsMode.Precise` (shipped Win64 game was /fp:fast).
  - Accept: gate green, new fingerprint baseline recorded here.
- [x] **2. Correctness.** Done: gate 54/0, fingerprint identical, smoke clean. GameMode unbinds in .cpp before
  travel (RemoveAll) instead of an EndPlay override, so KartGameMode.h is untouched. FX/KartFxComponent.cpp (null BoostFire pipe); UI/KartScreen.h/.cpp,
  Race/SKartRaceOverlay.cpp, UI/KartFrontEnd.cpp (`Top()` by value); UI/KartFrontEndScreens.cpp (sanitize on change,
  clamp BotSkill); UI/Hud/SKartRaceHud.cpp, UI/KartUISubsystem.cpp, Race/KartItemPresenter.cpp (guards, weak
  capture); KartGameMode.cpp, Race/KartRaceManager.cpp (RemoveDynamic, null checks); Net/RoomProtocol.cpp,
  Net/RoomHostSubsystem.cpp (saturating numeric casts); Sim/KartTrack.cpp (validate substep/radius, finite guard).
  - Accept: gate green, fingerprint identical.
- [x] **3. Render-thread waste.** Done: gate 54/0, fingerprint identical, smoke clean (641/554 frames), rendered
  showcase run (GUI binary + -KartCapture) shows boxes, pop-ups and fireballs drawing and clearing. UE 5.8 sends
  ISM transform/custom-data/add changes incrementally, so the per-frame MarkRenderStateDirty calls (full proxy
  rebuilds) are gone; item/blast ISMs are rewritten in place when their count holds and untouched when empty;
  hidden preview/live boxes are skipped; skid marks no longer dirty per segment; box twinkles only update on
  show/hide; scenery parts switch on an enum by FName (no FString per part per frame); audio volume/pitch/low-pass
  only sent when the value changes. NOT done: a per-stream Niagara option cache (needs a field in
  KartFxSubsystem.h or KartFxComponent.h, which are unlisted) — KartFxSubsystem.cpp untouched; ask before doing it.
  Race/KartItemPresenter.cpp (dirty/clear ISMs only on change); TrackBuilder.cpp
  (item boxes); TrackScenery.cpp (part kind enum, lamp MID on change); FX/KartFxComponent.cpp,
  FX/KartFxSubsystem.cpp (constant Niagara params once, double dirty); Audio/KartAudioComponent.cpp (skip unchanged).
  - Accept: gate green, fingerprint identical, smoke race clean.
- [x] **4. Game-thread allocations.** Done: gate 55/0 (new MovecadeKart.Fx.Hex: 4000 random strings match
  FColor::FromHex bit-for-bit), fingerprint identical, smoke clean (534/471), rendered race shows HUD icons and
  minimap. Painter Polygon/Lines take TConstArrayView (braced point lists no longer build a TArray); Hex() parses an
  FStringView in place; FindPart walks the owned set; per-frame material/haptic/item names interned once; sound
  observer no longer copies its snapshot; rival scan rate-limited to 0.25 s while nobody is near. NOT done (need
  unlisted headers): KartFxComponent bScreenTried flag (KartFxComponent.h), GatherViews inline allocator
  (SKartRaceHud.h); Tint() left as a switch since Hex() no longer allocates. Test lives in FX/Tests/KartFxHexTests.cpp.
  Original scope: One allocation-free `Hex()` (FX/KartFx.cpp/.h, KartLook.cpp, UI/KartUIStyle.cpp,
  TrackBuilder.cpp, TrackScenery.cpp) + equivalence test; UI/KartUIPainter.cpp/.h, UI/KartUIStyle.cpp,
  UI/Hud/SKartRaceHud.cpp, UI/Hud/KartItemIcons.cpp; KartPawn.cpp (rival cache, static FNames);
  FX/KartFxComponent.cpp (cached manager/anchors, LoadObject once); Race/KartRaceManager.cpp,
  Audio/KartSoundObserver.h/.cpp (static FNames, no FString copies).
  - Accept: gate green, fingerprint identical, smoke race clean.
- [ ] **5. Sim, bit-exact.** Sim/KartTrack.h/.cpp, Sim/KartSim.cpp (inline candidates 512, direct-cell Raycast with
  lowest-index tie-break, cached checkpoint waypoint and pad cos/sin, precomputed centre D/LenSq, provable
  NearestCentre early-out); Race/KartBotDriver.cpp/.h (segment found once per tick, TConstArrayView).
  - Accept: gate green, fingerprint identical.
- [ ] **6. Load time.** TrackBuilder.cpp, Sim/KartTrack.cpp/.h (one JSON parse); TrackScenery.cpp (struct key, batched
  AddInstances); Render/KartRenderQualitySubsystem.cpp (scalability applied once).
  - Accept: gate green, fingerprint identical, smoke race clean.
- [ ] **7. Modern C++.** Sim/*.h, Race/KartBotDriver.h, Race/KartRaceManager.h ([[nodiscard]], constexpr pools,
  designated initializers, shared Forward/DriftTurn/MaxStepsPerFrame, ConsumeItemEvents keeps capacity);
  UI/* (FButtonList/FFocus dedupe, one NowSeconds, named Faded flag, dead members).
  - Accept: gate green, fingerprint identical, Game target compiles.

**Excluded (changes results):** Distance→DistSquared, `S.Time` accumulation, grid CellSize, shared ray queries, full
spatial index. **Deferred:** NewObject pooling, boost-label enum (16 files), pawn transform cascade (measure first),
RoomHost save cadence (crash-recovery data).
