#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterialCache.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/ArcadeHud.h"
#include "Games/GameAudio.h"
#include "Games/Stacker/StackerDebugFlags.h"
#include "Games/Stacker/StackerRules.h"
#include "Games/Stacker/StackerTypes.h"
#include "StackerGame.generated.h"

class AStackerRooftop;
class ACameraActor;
class APlayerController;
class SStackerHud;
class UArcadeAvatarComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Net { struct FSensorFrame; }

/**
 * One round of Sky Stacker on the rooftop (Docs/Port/09-stacker.md): 1-4 builders, each with a tower on their own
 * lane. Spawned and seated by AStackerGameMode, which also owns the room glue; the HUD (SStackerHud) and the arcade
 * overlay read it.
 *
 * The rules (StackerRules) step on a fixed 10 ms clock (S1); every step reads each builder's palm (a phone's
 * attitude, the stick or keys, a bot countering the wind) and DROP lands the sweeping block. Presentation follows
 * the rules each frame: the towers bend with the lean and wobble toward a topple, trimmed shards tumble away, a guide
 * on the top shows what a drop keeps and cuts, the builder rides a lift with the next block in hand, birds cross
 * every five rows, and the camera fits the lanes, peeks back at milestones and orbits the winner at the end.
 *
 * The implementation is split by job: StackerGame.cpp (setup and round flow), StackerGameInput.cpp (palms, drops,
 * bots), StackerGameEffects.cpp (sound and particles per effect), StackerGameTowers.cpp (blocks, shards, cursor and
 * guide), StackerGameBuilders.cpp (lifts and avatars), StackerGameCamera.cpp (camera and altitude beats) and
 * StackerGameCoach.cpp (the coach marks).
 *
 * Cost: a lane's blocks and shards are instances in BatchesPerLane batches of one mesh, rewritten in one call each
 * and only when something moved, not a component per block.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API AStackerGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	/** The intro card's length (the camera drops onto the roof; the round clock holds). */
	static constexpr double IntroSeconds = 3.4;

	AStackerGame();

	/** Seats the builders, builds each lane's pieces, the camera and the HUD, and starts on the intro. */
	void Setup(AStackerRooftop* InRooftop, const TArray<FStackerSeat>& InSeats, APlayerController* InViewer);
	/** A phone's sensor frame for a seat (by player id): its attitude is the palm. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The DROP button (a phone's stacker:drop), resolved at receipt (S2); ignored during the intro. */
	void Drop(const FString& PlayerId);
	/** A new round with the same seats (local play again). */
	void Rematch();
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Callbacks (AStackerGameMode binds them in Party Mode) ---------------------------------------------------

	TFunction<void()> OnMatchStart;
	/** Every new effect (land, perfect, miss, topout, complete): the phones' toasts. */
	TFunction<void(const MovecadeKart::Stacker::FEffect&)> OnEffect;
	/** The showcase is over (or the room was asked to move on): the results are final. */
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	/** Play again; when unset, the round restarts itself (Rematch). */
	TFunction<void()> OnRematch;

	// For the HUD ---------------------------------------------------------------------------------------------

	const MovecadeKart::Stacker::FMatch& GetMatch() const { return Match; }
	EStackerStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	double GetFinishedAge() const { return Stage == EStackerStage::Finished ? GetStageAge() : -1.0; }
	const TArray<FStackerSeat>& GetSeats() const { return Seats; }
	const FStackerSeat* SeatOf(const FString& Id) const;
	bool HasLocal() const;
	/** The player whose camera the round is seen through (the HUD projects through it). */
	APlayerController* GetViewer() const { return Viewer; }
	/** Web world to UE cm. */
	static FVector W(const FVector& Web);
	/** Lanes the rooftop lays out: one past the highest lane in use, at most four. */
	int32 LaneSpan() const;
	double ScrollOf(int32 TowerIndex) const { return Lanes.IsValidIndex(TowerIndex) ? Lanes[TowerIndex].Scroll : 0.0; }
	/** The match's tower on a lane, or INDEX_NONE. */
	int32 TowerOfLane(int32 LaneIndex) const { return TowerByLane.IsValidIndex(LaneIndex) ? TowerByLane[LaneIndex] : INDEX_NONE; }
	/** The web y of a tower's top, for the HUD's callouts. */
	double TopY(int32 TowerIndex) const;
	/** Tower indices by score, then height (S7), best first. Fixed when the round ends; empty before. */
	const TArray<int32>& Standings() const { return StandingOrder.Get(); }
	const FStackerCoach& GetCoach() const { return Coach; }

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -------------------------------------

	virtual FString GetArcadeId() const override { return TEXT("stacker"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	/** A builder's lane: its input, its bot's intent and what the presentation smooths. Index = tower index. */
	struct FLane
	{
		FString PlayerId;
		EBuilderKind Kind = EBuilderKind::Bot;
		/** The lane's centre across the deck (web x). */
		double LaneX = 0.0;
		/** Rows the tower has sunk so its work rows stay in frame (eases toward height - work rows). */
		double Scroll = 0.0;
		// Local: the palm the mouse nudges and leaves.
		double MouseRoll = 0.0;
		double MousePitch = 0.0;
		bool bDropQueued = false;
		// Phone: the latest palm.
		double PhoneRoll = 0.0;
		double PhonePitch = 0.0;
		// Bot: where it means to drop (relative to the support), its palm's error, when it may next drop.
		double AimError = 0.0;
		double PalmError = 0.0;
		double NextDropAt = 0.0;
		int32 AimedRow = -1;
		// Presentation.
		double LateralSmooth = 0.0;
		/** The topple's shudder (1 on a topout, decaying) and a smaller kick on a miss. */
		double Topple = 0.0;
		/** The lean the tower shows, smoothed so a phone's jitter doesn't rattle it. */
		double ShownLean = 0.0;
		/** When the last perfect landed (the block flashes white). */
		double PerfectAt = -10.0;
		/** The latest reaction (StackerAvatar.tsx): the effect kind and when. */
		MovecadeKart::Stacker::EEffectKind Reaction = MovecadeKart::Stacker::EEffectKind::Land;
		double ReactionAt = -10.0;
	};

	/** The altitude beats (StackerScene.tsx updateAltitudeBeats): birds every five rows, a camera peek from ten. */
	struct FBeats
	{
		int32 LastMilestone = 0;
		double BirdLife = 0.0;
		int32 BirdRow = 0;
		double PeekRemaining = 0.0;
	};

	/**
	 * One batch of instances with a fixed number of slots (a block's slot is its row, a shard's its id): the count
	 * never changes in play, so nothing is reallocated, and a slot's previous transform is the same block's.
	 * Unused slots are parked out of sight. Rewritten only when something moved.
	 */
	struct FBatch
	{
		TArray<FTransform> Want;
		TArray<FTransform> Have;
		TArray<FTransform> Prev;
	};

	/** Where the camera wants to be this frame (web units, vertical FOV in degrees), before smoothing. */
	struct FCameraShot
	{
		FVector Pos = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		double Fov = 46.0;
	};

	// StackerGame.cpp: setup and the round's flow.
	void SeedLanes();
	void ResetLanesForRound();
	void BuildCameraAndHud();
	void SetStage(EStackerStage Next);
	void AdvanceStage(float Dt);
	void StepFixed();
	void PlayAltitudeChime();
	void RankStandings();
	double LaneXOf(int32 LaneIndex) const;
	int32 TallestHeight() const;

	// StackerGameInput.cpp: palms, drops and the house bots.
	void SteerLocals();
	void SteerLocal(FLane& Lane, MovecadeKart::Stacker::FTower& Tower) const;
	void SteerBot(FLane& Lane, MovecadeKart::Stacker::FTower& Tower, const FStackerSeat& Seat);
	void DropQueued();

	// StackerGameEffects.cpp: the sound and particles of each new effect.
	void PresentNewEffects();
	void PresentEffect(const MovecadeKart::Stacker::FEffect& Effect);

	// StackerGameTowers.cpp: the blocks, the shards, the sweeping block and the drop guide.
	void BuildTowerPieces(int32 TowerIndex);
	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true);
	UInstancedStaticMeshComponent* MakeBatch(const TCHAR* Name, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Looks, bool bShadows);
	void ResetBatches();
	void FlushBatches();
	void FlushBatch(int32 Index);
	void UpdateTowers(float Dt);
	void UpdateTowerBlocks(int32 TowerIndex, double Compare, double Scroll, bool bDimLosers);
	/** Places the sweeping block; false when it is hidden (no guide then). */
	bool UpdateCursor(int32 TowerIndex, double Scroll);
	void UpdateGuide(int32 TowerIndex, double Scroll);
	void HideGuide(int32 TowerIndex);
	void UpdateShards();

	// StackerGameBuilders.cpp: the lifts and the builders on them.
	void BuildBuilderPieces(int32 TowerIndex);
	void UpdateBuilders(float Dt);
	void UpdateBuilder(int32 TowerIndex, float Dt);
	/** Raises the lane's lift and piston; returns where the builder stands (web). */
	FVector PlaceLift(int32 TowerIndex);
	/** Hands: forward, side and up in pose units. */
	void UpdateHeldBlock(int32 TowerIndex, bool bHold, const FVector& Hands);

	// StackerGameCamera.cpp: the altitude beats (birds, peek) and the camera.
	void UpdateBeats(float Dt);
	void UpdateBirds();
	void UpdateCamera(float Dt);
	FCameraShot FrameShot(double Aspect) const;
	FCameraShot ShowcaseShot(double Aspect, int32 Span) const;
	double ViewportAspect() const;

	// StackerGameCoach.cpp: the local builder's coach marks.
	void UpdateCoach(float Dt);

	UPROPERTY()
	TObjectPtr<AStackerRooftop> Rooftop;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BlockMesh;

	/** The engine's basic shapes (lifts, pistons, guides, birds); null, with one warning, if the engine lacks them. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	/** The looks every lane shares (trim, light, paints, lift, piston, birds), made once each. */
	UPROPERTY()
	FKartMaterialCache Materials;

	UPROPERTY()
	TArray<TObjectPtr<UArcadeAvatarComponent>> Avatars;

	/** Per lane, BatchesPerLane batches (Scene::EBlockBatch): the block looks, then the lane's shards. */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;

	/** Per lane: the sweeping block, the block in the builder's hands, the lift and its piston, the drop guide. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Cursors;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> HeldBlocks;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Lifts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pistons;

	/** Per lane: the part of the drop the tower keeps, and the overhang that will be cut (both translucent). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GuideKeep;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GuideCut;

	/** Per lane, its own instances: the cursor glows and the guide changes colour every frame. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CursorMids;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GuideKeepMids;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GuideCutMids;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Birds;

	TArray<FStackerSeat> Seats;
	TArray<FLane> Lanes;
	TArray<FBatch> BatchState;
	TArray<int32> TowerByLane;
	FArcadeStandingsOrder StandingOrder;
	MovecadeKart::Stacker::FMatch Match;
	/** Effect and shard ids grow (one counter); everything at or below these has been presented. */
	int32 LastSeenEffectId = 0;
	int32 LastSeenShardId = 0;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;
	FStackerDebugFlags DebugFlags;
	FBeats Beats;
	/** The birds' slots hold a flight (they need one more write to park). */
	bool bBirdsOut = false;
	/** This frame's wing transforms (kept to reuse its allocation). */
	TArray<FTransform> BirdWings;
	FStackerCoach Coach;
	/** Coach progress: landings so far and seconds held steady in a real wind. */
	int32 CoachLandings = 0;
	double CoachSteadySeconds = 0.0;
	double CoachLeanShown = 0.0;

	EStackerStage Stage = EStackerStage::Intro;
	/** Presentation time (s): advances with the frame, holds while paused. */
	double Clock = 0.0;
	double StageAt = 0.0;
	/** Frame time not yet stepped by the rules (ms). */
	double Accumulator = 0.0;
	bool bPaused = false;
	/** The camera's shake (decays; landings and misses kick it). */
	double Shake = 0.0;
	/** OnFinished has run for this round (after the showcase, or early when the room is asked to move on). */
	bool bResultsCommitted = false;
	/** The tallest tower's five-row band last chimed for. */
	int32 AltitudeBand = 0;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 46.0;
	bool bCamPlaced = false;

	TSharedPtr<SStackerHud> Hud;
};
