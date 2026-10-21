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

/**
 * One round of Sky Stacker on the rooftop (Docs/Port/09-stacker.md): 1-4 builders, each with a tower on their own
 * lane. The rules (StackerRules) step on a fixed 10 ms clock (S1); every step reads each builder's palm (a phone's
 * attitude, the stick or keys, a bot countering the wind) and DROP lands the sweeping block. Presentation follows:
 * the towers sink into their plinths as they climb with a squash on each landing and the lean of the palm, the
 * trimmed shards tumble away, sparks for a perfect, dust for a miss; the builder rides a lift beside the tower with
 * the next block in hand; the city sinks behind; the camera fits the lanes and, at the end, orbits the winner.
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
	bool ScreenOf(const FVector& World, FVector2D& Out01) const;
	/** Web world to UE cm. */
	static FVector W(const FVector& Web);
	double GetFinishedAge() const { return Stage == EStackerStage::Finished ? GetStageAge() : -1.0; }
	bool HasLocal() const;
	int32 LaneSpan() const;
	double ScrollOf(int32 Lane) const { return Lanes.IsValidIndex(Lane) ? Lanes[Lane].Scroll : 0.0; }
	/** The web y of a tower's top, for the HUD's callouts. */
	double TopY(const MovecadeKart::Stacker::FTower& Tower) const;
	/** The HUD's order at the end: by score, then height (S7). */
	TArray<const MovecadeKart::Stacker::FTower*> Standings() const;

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
		double LastAltitudeBand = 0.0;
	};

	void SetStage(EStackerStage Next);
	void StepFixed();
	void SteerLocal(FLane& Lane, MovecadeKart::Stacker::FTower& Tower, float Dt);
	void SteerBot(FLane& Lane, MovecadeKart::Stacker::FTower& Tower, const FStackerSeat& Seat);
	void PresentEffect(const MovecadeKart::Stacker::FEffect& Effect);
	void UpdateTowers(float Dt);
	void UpdateShards();
	void UpdateBuilders(float Dt);
	void UpdateCamera(float Dt);
	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true);
	double LaneXOf(int32 LaneIndex) const;

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

	/** Per lane: the visible rows, the sweeping block, the block in the builder's hands, the lift and its piston. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BlockPool;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Cursors;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> HeldBlocks;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Lifts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pistons;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ShardPool;

	/** Per lane: base, odd, perfect, cursor, dimmed (the losers at the end). */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> LaneMaterials;

	TArray<FStackerSeat> Seats;
	TArray<FLane> Lanes;
	MovecadeKart::Stacker::FMatch Match;
	TSet<int32> SeenEffects;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;
	/** The latest reaction per lane: the effect kind and when. */
	TArray<TPair<MovecadeKart::Stacker::EEffectKind, double>> Reactions;

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
