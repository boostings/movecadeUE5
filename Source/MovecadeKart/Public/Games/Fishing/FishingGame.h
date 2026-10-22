#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/Fishing/FishingMotion.h"
#include "Games/Fishing/FishingRules.h"
#include "Games/GameAudio.h"
#include "KartLook.h"
#include "FishingGame.generated.h"

class AFishingLake;
class ACameraActor;
class APlayerController;
class SFishingHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Net { struct FSensorFrame; }

UENUM()
enum class EFishingStage : uint8
{
	/** The title card and the camera's swing round behind the dock; the round clock holds. */
	Intro,
	Match,
	Finished,
};

UENUM()
enum class EAnglerKind : uint8
{
	/** Hold and let go of Space, A or a click to cast; press it to hook; spin the right stick, alternate A/D or scroll to reel. */
	Local,
	/** A phone: flick forward to cast, jerk up to hook, roll the wrist to reel (the motion pad's button holds a cast too). */
	Phone,
	Bot,
};

USTRUCT()
struct FFishingSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EAnglerKind Kind = EAnglerKind::Bot;
	/** Bots: 0 sloppy .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/**
 * One round of Fishing Frenzy on the lake (FishingRules on a fixed 10 ms clock): 1-4 anglers side by side, all at
 * once. Each step reads every angler's hands (a phone's flicks and wrist roll through FFishingMotion, the local
 * button and crank, a bot's timing) and the rules decide the rest. Presentation follows the rules: the rod bends
 * with the line's tension and whips through the cast, the line sags or snaps taut, the bobber bobs, twitches on a
 * nibble and dives on the bite, the fish fights near the surface and leaps on its runs, splashes everywhere, and a
 * catch is lifted out, turned to the camera and held up while the HUD calls its weight (and a new record), then it
 * goes in the bucket (junk goes back in the lake). The camera frames the dock; at the end the anglers turn round
 * with their biggest catch.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API AFishingGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	AFishingGame();

	void Setup(AFishingLake* InLake, const TArray<FFishingSeat>& InSeats, APlayerController* InViewer, uint32 InSeed);
	/** A phone's sensor frame for a seat: flicks cast and hook, the wrist roll reels. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The motion pad's big button (pad:press / pad:release): hold to charge a cast, press to hook. */
	void PadPress(const FString& PlayerId);
	void PadRelease(const FString& PlayerId);
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Fishing::FMatch& GetMatch() const { return Match; }
	EFishingStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FFishingSeat>& GetSeats() const { return Seats; }
	const FFishingSeat* SeatOf(const FString& Id) const;
	bool ScreenOf(const FVector& World, FVector2D& Out01) const;
	double GetFinishedAge() const { return Stage == EFishingStage::Finished ? GetStageAge() : -1.0; }
	bool HasLocal() const;
	int32 LaneCount() const { return Match.Anglers.Num(); }
	/** The cast meter while a cast is held (0..1), or < 0. */
	double ChargeOf(int32 Lane) const;
	/** Over the angler's head, for the HUD's callouts and the catch card. */
	FVector HeadOf(int32 Lane) const;
	/** The bobber (or the fish on the line), for the HUD's pins. */
	FVector BobberOf(int32 Lane) const;
	/** The HUD's order at the end: by score, then the biggest fish. */
	TArray<const MovecadeKart::Fishing::FAngler*> Standings() const;

	TFunction<void()> OnMatchStart;
	/** Every new effect (bite, catch, snap, ...): the phones' toasts and haptics. */
	TFunction<void(const MovecadeKart::Fishing::FEffect&)> OnEffect;
	/** An angler's state changed (the phones' prompts). */
	TFunction<void(const MovecadeKart::Fishing::FAngler&)> OnAnglerState;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	void Rematch();

	// IArcadeGame ---------------------------------------------------------------------------------------------
	virtual FString GetArcadeId() const override { return TEXT("fishing"); }
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
		EAnglerKind Kind = EAnglerKind::Bot;
		// A held cast (the local button, the pad's, a bot's wind-up).
		bool bHolding = false;
		double HoldAt = 0.0;
		double Charge = 0.0;
		// Local: the crank.
		double CrankRevs = 0.0;
		double CrankRate = 0.0;
		double StickAngle = 0.0;
		bool bStickValid = false;
		double MouseAngle = 0.0;
		bool bMouseValid = false;
		int32 LastCrankKey = -1;
		// Phone.
		MovecadeKart::Fishing::FFishingMotion Motion;
		double PhoneReel = 0.0;
		double LastFrameAt = -10.0;
		double WindupAt = -10.0;
		// Bot (match ms).
		double NextActAt = 0.0;
		double BotPower = 0.6;
		double ReactAt = -1.0;
		double EaseFrom = -1.0;
		double BotReel = 0.0;
		double BotError = 0.0;
		// Presentation.
		MovecadeKart::Fishing::EAnglerState Shown = MovecadeKart::Fishing::EAnglerState::Idle;
		double ShownAt = -10.0;
		double TurnYaw = 0.0;
		double RodPitch = 40.0;
		double RodBend = 0.0;
		double ReelPhase = 0.0;
		double BobZ = 0.0;
		double BobV = 0.0;
		FVector CastFrom = FVector::ZeroVector;
		FVector Tip = FVector::ZeroVector;
		double JumpAt = -10.0;
		int32 JumpBurst = 0;
		double ThrashCarry = 0.0;
		double HookedAt = -10.0;
		double SnapAt = -10.0;
		MovecadeKart::Fishing::EEffectKind Reaction = MovecadeKart::Fishing::EEffectKind::Cast;
		double ReactionAt = -10.0;
	};

	void SetStage(EFishingStage Next);
	void StepFixed();
	void ReadLocal(FLane& Lane, float Dt);
	void Press(int32 LaneIndex);
	void Release(int32 LaneIndex);
	void SteerBot(int32 LaneIndex);
	void DrainEffects();
	void PresentEffect(const MovecadeKart::Fishing::FEffect& Effect);
	void Splash(const FVector& At, double Size, bool bRing = true);
	void UpdateAnglers(float Dt);
	void UpdateRod(int32 LaneIndex, const FVector& Base, const FRotator& Facing, double Pitch, double Yaw, double Bend, const FVector& Toward, bool bShow);
	void UpdateLine(int32 LaneIndex, const FVector& From, const FVector& To, double Sag, bool bShow);
	void UpdateCamera(float Dt);
	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true);
	UStaticMesh* FishMesh(int32 Species);
	void ShowFish(int32 LaneIndex, int32 Species, int32 WeightG, const FTransform& At);
	FVector HoldPoint(int32 LaneIndex) const;

	UPROPERTY()
	TObjectPtr<AFishingLake> Lake;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TArray<TObjectPtr<UArcadeAvatarComponent>> Avatars;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Handles;

	/** Per lane: RodSegments blank pieces, then LineSegments of line. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> RodPieces;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> LinePieces;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Bobbers;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Fish;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> FishMeshes;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> LineMaterial;

	TArray<int32> FishShown;
	TArray<FFishingSeat> Seats;
	TArray<FLane> Lanes;
	MovecadeKart::Fishing::FMatch Match;
	uint32 Seed = 1;
	TSet<int32> SeenEffects;
	FRandomStream BotRng;
	FRandomStream FxRng;
	MovecadeKart::Games::FCuePlayer Cues;

	EFishingStage Stage = EFishingStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double Accumulator = 0.0;
	bool bPaused = false;
	double Shake = 0.0;
	bool bResultsCommitted = false;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 40.0;
	bool bCamPlaced = false;

	TSharedPtr<SFishingHud> Hud;
};
