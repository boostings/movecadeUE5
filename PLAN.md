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
- [ ] **2. Correctness.** FX/KartFxComponent.cpp (null BoostFire pipe); UI/KartScreen.h/.cpp,
  Race/SKartRaceOverlay.cpp, UI/KartFrontEnd.cpp (`Top()` by value); UI/KartFrontEndScreens.cpp (sanitize on change,
  clamp BotSkill); UI/Hud/SKartRaceHud.cpp, UI/KartUISubsystem.cpp, Race/KartItemPresenter.cpp (guards, weak
  capture); KartGameMode.cpp, Race/KartRaceManager.cpp (RemoveDynamic, null checks); Net/RoomProtocol.cpp,
  Net/RoomHostSubsystem.cpp (saturating numeric casts); Sim/KartTrack.cpp (validate substep/radius, finite guard).
  - Accept: gate green, fingerprint identical.
- [ ] **3. Render-thread waste.** Race/KartItemPresenter.cpp (dirty/clear ISMs only on change); TrackBuilder.cpp
  (item boxes); TrackScenery.cpp (part kind enum, lamp MID on change); FX/KartFxComponent.cpp,
  FX/KartFxSubsystem.cpp (constant Niagara params once, double dirty); Audio/KartAudioComponent.cpp (skip unchanged).
  - Accept: gate green, fingerprint identical, smoke race clean.
- [ ] **4. Game-thread allocations.** One allocation-free `Hex()` (FX/KartFx.cpp/.h, KartLook.cpp, UI/KartUIStyle.cpp,
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
