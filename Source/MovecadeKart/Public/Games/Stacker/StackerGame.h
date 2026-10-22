#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "Games/Stacker/StackerRules.h"
#include "KartLook.h"
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

UENUM()
enum class EStackerStage : uint8
{
	/** The intro card and the camera's drop onto the roof; the round clock holds. */
	Intro,
	Match,
	Finished,
};

UENUM()
enum class EBuilderKind : uint8
{
	/** The left stick, the keys or the mouse tilt the palm; A, Space or a click drops. */
	Local,
	/** A phone is the palm: its attitude balances the tower, its DROP button drops. */
	Phone,
	Bot,
};

USTRUCT()
struct FStackerSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EBuilderKind Kind = EBuilderKind::Bot;
	/** Bots: 0 sloppy .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** The camera's projection, taken once per HUD paint (not once per projected point). */
struct FStackerView
{
	FMatrix ViewProjection = FMatrix::Identity;
	FIntRect Rect;
	FVector2D Size = FVector2D(1.0, 1.0);
	bool bValid = false;

	bool Project(const FVector& World, FVector2D& Out01) const;
};

/** The local builder's coach marks (the first round of a session teaches; later rounds stay quiet). */
struct FStackerCoach
{
	/** 0..1 how much each mark shows this frame. */
	float Drop = 0.f;
	float Lean = 0.f;
	/** The side to lean (-1 left, +1 right) against the wind. */
	float LeanSide = 1.f;
	/** The block would land perfectly if dropped now. */
	bool bAligned = false;
	/** The local tower (an index into the match's towers), or INDEX_NONE. */
	int32 Tower = INDEX_NONE;
};

/**
 * One round of Sky Stacker on the rooftop (Docs/Port/09-stacker.md): 1-4 builders, each with a tower on their own
 * lane. The rules (StackerRules) step on a fixed 10 ms clock (S1); every step reads each builder's palm (a phone's
 * attitude, the stick or keys, a bot countering the wind) and DROP lands the sweeping block. Presentation follows:
 * the towers sink into their plinths as they climb with a squash on each landing, bending with the lean of the palm
 * and wobbling as they near a topple; the trimmed shards tumble away, sparks and a ring for a perfect, dust for a
 * miss; a guide on the tower's top shows what the drop keeps and what it cuts; the builder rides a lift beside the
 * tower with the next block in hand; birds cross every five rows; the city sinks behind; the camera fits the lanes,
 * peeks back at milestones and, at the end, orbits the winner.
 *
 * Cost: the blocks and shards of a lane are instances of one mesh per lane material (5 batches per lane, updated
 * in one call each and only when something moved), not a component per block.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API AStackerGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	AStackerGame();

	void Setup(AStackerRooftop* InRooftop, const TArray<FStackerSeat>& InSeats, APlayerController* InViewer);
	/** A phone's sensor frame for a seat (by player id): its attitude is the palm. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The DROP button (a phone's stacker:drop), resolved at receipt (S2); ignored during the intro. */
	void Drop(const FString& PlayerId);
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Stacker::FMatch& GetMatch() const { return Match; }
	EStackerStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FStackerSeat>& GetSeats() const { return Seats; }
	const FStackerSeat* SeatOf(const FString& Id) const;
	/** The camera's view this frame, for projecting many points. */
	FStackerView GetView() const;
	/** Web world to UE cm. */
	static FVector W(const FVector& Web);
	double GetFinishedAge() const { return Stage == EStackerStage::Finished ? GetStageAge() : -1.0; }
	bool HasLocal() const;
	int32 LaneSpan() const;
	double ScrollOf(int32 TowerIndex) const { return Lanes.IsValidIndex(TowerIndex) ? Lanes[TowerIndex].Scroll : 0.0; }
	/** The match's tower on a lane, or INDEX_NONE. */
	int32 TowerOfLane(int32 LaneIndex) const { return TowerByLane.IsValidIndex(LaneIndex) ? TowerByLane[LaneIndex] : INDEX_NONE; }
	/** The web y of a tower's top, for the HUD's callouts. */
	double TopY(int32 TowerIndex) const;
	/** The HUD's order at the end: by score, then height (S7). Fixed when the round ends. */
	const TArray<int32>& Standings() const { return StandingOrder; }
	const FStackerCoach& GetCoach() const { return Coach; }

	TFunction<void()> OnMatchStart;
	/** Every new effect (land, perfect, miss, topout, complete): the phones' toasts. */
	TFunction<void(const MovecadeKart::Stacker::FEffect&)> OnEffect;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	void Rematch();

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -----------------------------------
	virtual FString GetArcadeId() const override { return TEXT("stacker"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	struct FLane
	{
		FString PlayerId;
		EBuilderKind Kind = EBuilderKind::Bot;
		double LaneX = 0.0;
		double Scroll = 0.0;
		// Local: the palm the stick, keys and mouse hold.
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

	void SetStage(EStackerStage Next);
	void StepFixed();
	void SteerLocal(FLane& Lane, MovecadeKart::Stacker::FTower& Tower, float Dt);
	void SteerBot(FLane& Lane, MovecadeKart::Stacker::FTower& Tower, const FStackerSeat& Seat);
	void PresentNewEffects();
	void PresentEffect(const MovecadeKart::Stacker::FEffect& Effect);
	void UpdateTowers(float Dt);
	void UpdateShards();
	void UpdateBuilders(float Dt);
	void UpdateBeats(float Dt);
	void UpdateCoach(float Dt);
	void UpdateCamera(float Dt);
	void FlushBatch(int32 Index);
	void ResetBatches();
	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true);
	UInstancedStaticMeshComponent* MakeBatch(const TCHAR* Name, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials, bool bShadows);
	double LaneXOf(int32 LaneIndex) const;
	void RankStandings();

	UPROPERTY()
	TObjectPtr<AStackerRooftop> Rooftop;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BlockMesh;

	UPROPERTY()
	TArray<TObjectPtr<UArcadeAvatarComponent>> Avatars;

	/** Per lane, BatchesPerLane batches of blocks (even, odd, perfect, dimmed, flashing) and one of shards. */
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

	/** Per lane: the part of the drop the tower keeps, and the overhang that will be cut. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GuideKeep;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GuideCut;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CursorMids;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GuideMids;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Birds;

	TArray<FStackerSeat> Seats;
	TArray<FLane> Lanes;
	TArray<FBatch> BatchState;
	TArray<int32> TowerByLane;
	TArray<int32> StandingOrder;
	MovecadeKart::Stacker::FMatch Match;
	/** Effect and shard ids grow (one counter); everything at or below these has been presented. */
	int32 LastSeenEffectId = 0;
	int32 LastSeenShardId = 0;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;
	/** The latest reaction per lane: the effect kind and when. */
	TArray<TPair<MovecadeKart::Stacker::EEffectKind, double>> Reactions;
	FBeats Beats;
	bool bBirdsOut = false;
	FStackerCoach Coach;
	/** Coach progress: landings so far and seconds held steady in a real wind. */
	int32 CoachLandings = 0;
	double CoachSteadySeconds = 0.0;
	double CoachLeanShown = 0.0;

	EStackerStage Stage = EStackerStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double Accumulator = 0.0;
	bool bPaused = false;
	double Shake = 0.0;
	/** OnFinished has run for this round (after the showcase, or early when the room is asked to move on). */
	bool bResultsCommitted = false;
	int32 AltitudeBand = 0;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 46.0;
	bool bCamPlaced = false;

	TSharedPtr<SStackerHud> Hud;
};
