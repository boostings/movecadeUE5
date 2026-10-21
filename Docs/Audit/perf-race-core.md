# Perf audit: Race, FX, Sim, Audio, Track, Render, Avatar, core (2026-10-21)

Sim/ is clean; propose no changes there (determinism). Do NOT window KartTrack NearestWaypoint/NearestCentre (tie-breaks).

1. HIGH: `KartItemPresenter.cpp:751-755` ClearInstances on ~13 ISMs + re-add + MarkRenderStateDirty (`:984-990`) every frame even with zero projectiles; UpdateBlasts same (`:1001,:1018`); UpdateBoxes explicit MarkRenderStateDirty on 4 ISMs (`:732-735`). Pool instances, zero-scale unused, BatchUpdateInstancesTransforms with dirty=true on last update, skip when count 0 now and before. No shadows on BoxCores/item ISMs (`:83`).
2. HIGH: Niagara streams never deactivated (`KartFxSubsystem.cpp:185-205`); 18 streams per kart (`KartFxComponent.cpp:109-200`) + box twinkles (`KartItemPresenter.cpp:729`); 10 SetVariable per stream per frame (`ApplyOptions :84-103`). Deactivate at rate 0, Activate when >0; set constants once in MakeStream; per frame push only Rate/Direction/Inherit when changed.
3. MED-HIGH: kart hierarchy re-propagated 4-5x per frame (`KartPawn.cpp:375-376`; ChassisPivot :450 before SlipPivot :457, BodyPivot :458 and :464). Use *_Direct setters then one UpdateComponentToWorld on SlipPivot; at least top-down and one BodyPivot SetRelativeTransform.
4. MED: `AvatarRig.h:28-33` SetRotation recomputes the whole skeleton per joint (~1-2.5k transform multiplies per kart). Recompute only the subtree (parents-first order; bit-identical). TInlineAllocator<40> may spill (>40 bones) -> make member or raise.
5. MED-LOW: `KartFxComponent.cpp:343` RefreshAnchors -> FindPart -> GetComponents + string compares every tick (:211, :36-48). Cache Body pointer.
6. MED: `TrackLife.cpp:814-831` firework trails spawn a pooled Niagara system per shell per frame (~800/s). One stream per shell moved with SetWorldLocation.
7. MED (Harbor): `TrackScenery.cpp:691` M.Part.ToString() + ~10 string compares per part per frame -> EMotionKind enum at GatherMotion. `:577` shadows on gulls (27), jet skis, buoys, gondolas -> only ferris frame/wheel/ships.
8. MED GPU: `TrackBuilder.cpp:106` bRealTimeCapture=true with a static dome; RecaptureSky already at :462 -> false (verify visually).
9. MED-LOW GPU: `TrackLife.cpp:618-673` trample RT 1024^2 decay + canvas every frame -> 30 Hz with accumulated dt (exact, Exp decay).
10. LOW-MED: `TrackBuilder.cpp:603-607` animates hidden preview item boxes each frame -> early out if hidden; no shadows.
11. MED: no cull distances on small scenery prefab ISMs (`TrackScenery.cpp:416-435`), crowd ISM (`TrackLife.cpp:445-451`); movable point lights no draw distance (`TrackScenery.cpp:480-494`). SetCullDistances by bounds radius; shadows only if R>60; light MaxDrawDistance.
12. LOW-MED: `KartAudioComponent.cpp:293-310`/`:149` destroys/creates loop audio components on every stop/start -> keep and Stop/Play; `:210-211` 56 engine voices set volume/pitch every frame -> skip if |delta|<1e-3.
13. LOW: `KartPawn.cpp:576` FName("eyeBlink") per frame; `:657` 10 SetMorphTarget per kart per frame; `:471` PaintColor every frame -> change-only.
14. LOW: `TrackLife.cpp:682` TArray per frame; Hex() parsing ~25x per kart per frame in KartFxComponent recipes/twinkles; `KartPawn.cpp:612` TActorIterator per frame with no rival.
15. LOW-MED: `KartRenderQualitySubsystem.cpp:17-19,114` dynamic-res budget fixed at 60 fps -> derive from frame cap / refresh.
16. LOW: identical rocket mesh built per kart (`KartFxComponent.cpp:122`); new point light per explosion (`KartItemPresenter.cpp:407`) -> pool.

Correctness:
A. HIGH: `KartItemMeshes.cpp:235` NewObject<UStaticMesh>(Outer, Name) fixed name; SM_Item_Shell built twice (`KartItemPresenter.cpp:161-162`); every Bind() (`:284`) rebuilds under same names -> in-place reconstruction of a live mesh (use-after-free risk). Use MakeUniqueObjectName or build once.
C. LOW: presenter Bind never removes OnCue from the previous manager (`:281`).
D. LOW: pad flash meshes created with null parent (`KartFxComponent.cpp:471`).
