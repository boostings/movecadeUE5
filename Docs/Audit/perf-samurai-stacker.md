# Perf audit: Samurai, Stacker (2026-10-21)

Presentation-side only; rules (SamuraiRules, StackerRules, SamuraiBlade) untouched (golden tests).

1. HIGH: Samurai trails `SamuraiGame.cpp:769-799`: ClearAllMeshSections / CreateMeshSection_LinearColor every frame per lane; both run UpdateCollision() unconditionally (new body setup, physics + render proxy rebuild). Create the section once (fixed 12-sample topology), then UpdateMeshSection_LinearColor with reused member arrays; toggle visibility instead of clearing; SetMaterial once in Setup.
2. HIGH/MED: Stacker city bands move nearly every frame (`StackerGame.cpp:701,752`, `StackerRooftop.cpp:88-97`): exponential scroll never settles; each band move updates ~8 ISMs -> CalcBounds over all instances, GPUScene upload, VSM invalidation. Snap when within 1e-3; only SetScroll when changed > 0.002; band 0 facades no shadows (`StackerRooftop.cpp:310`).
3. HIGH: Stacker blocks are separate 2-section shadow-casting components (61 per lane; `StackerGame.cpp:229-234`, update 708-731): ~128 draws/pass in play, ~490 at the showcase. Use one ISM per lane material (5 per lane, trim in slot 1) and BatchUpdateInstancesTransforms; same for 32 shards.
4. MED: Samurai pools assigned by array order (`SamuraiGame.cpp:616-621` targets, `660-680` pieces with RemoveAll/RemoveAt(0)) -> every later component re-dressed (SetStaticMesh + 5 SetMaterial). Stable slots: TMap target id -> slot; pieces ring buffer, dress once at birth.
5. MED: 52 target/piece comps x 4-5 sections all shadowed (`SamuraiGame.cpp:121-128`). No shadows on pieces (live 0.65 s).
6. MED GPU: `bRealTimeCapture = true` in `SamuraiDojo.cpp:53`, `StackerRooftop.cpp:45`. Turn off, RecaptureSky once after Build (verify visuals).
7. MED/LOW: venue geometry Movable (`StackerRooftop.cpp:124` all ISMs; `SamuraiDojo.cpp:145` Part; dojo ISMs inherit Movable root). Make root + non-moving groups Static; keep bands, fan, flags, lantern pivots Movable.
8. LOW/MED: rooftop ~3.9k (1 lane) to ~5.9k (4 lanes) window instances. Bake each band's windows into one FMeshBuilder mesh per material (or emissive window mask in facade material).
9. LOW/MED: HUD ProjectWorldToScreen per element (`SSamuraiHud.cpp:84,147`, `SStackerHud.cpp:97,167`) via ScreenOf (rebuilds projection + GetViewportSize). Compute VP matrix once per paint.
10. LOW: HUD string/array building every frame (Samurai `SSamuraiHud.cpp:70,68,93,202-206,236`, Heart Polygon x12 :20; Stacker `SStackerHud.cpp:65,80-81,116,119,231-235,260-261`). Cache by last value; names in Construct.
11. LOW: Standings sorted twice per frame at finish (`StackerGame.cpp:909`, `SStackerHud.cpp:243`, `SSamuraiHud.cpp:214-219`). Cache at SetStage(Finished).
12. LOW: SeenEffects sets grow all match (`SamuraiGame.cpp:383-394`, `StackerGame.cpp:385-393, 484-492`, shards 774-777). Ids are monotonic -> track LastSeenEffectId / LastSeenShardId.
13. LOW: redundant per-frame SetMaterial (`StackerGame.cpp:729,788`; `SamuraiGame.cpp:618-621,677-680,640,799`).
14. LOW: `SamuraiGame.cpp:604` TSet Live per frame; `:626` Match.FindPlayer string compare per target -> index by LaneIndex.
15. LOW: moving shadow casters: fan blades, flags (`StackerRooftop.cpp:408-441`), lanterns (`SamuraiDojo.cpp:530-539`) -> no shadows on fan/flags.
16. LOW (load): duplicate MIDs per call (`SamuraiDojo.cpp:493-494`, `StackerRooftop.cpp:247`); OnConstruction builds with 1 lane then SetLanes(N) rebuilds everything incl. sky MID + RecaptureSky. Cache materials per Build; avoid double build.
17. LOW: `StackerGame.cpp:768` IndexOfByPredicate per shard (use LaneIndex), `TopY` string compare (:319); `SamuraiDojo.cpp:597-598` Hex parse per petal; Samurai PushScores sends unchanged scores every second (copy Stacker's LastScores check).

Correctness:
- `StackerRooftop.cpp:399` fan created with fixed name "HvacFan" -> MakeUniqueObjectName (rebuild collision).
- Stacker room rematch could restart before CompleteGame (OnFinished at 8 s vs rematch at 2.5 s): now owned by SArcadeOverlay; ensure final scores are posted before RestartGame.
- Camera actors never destroyed in EndPlay (`SamuraiGame.cpp:172`, `StackerGame.cpp:263`).
