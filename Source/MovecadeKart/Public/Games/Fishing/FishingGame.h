#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterialCache.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/Fishing/FishingDebugFlags.h"
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
 * One round of Fishing Frenzy on the lake, spawned and set up by AFishingGameMode. FishingRules decide everything on
 * a fixed 10 ms clock: 1-4 anglers fish side by side, all at once. Each step reads every angler's hands (a phone's
 * flicks and wrist roll through FFishingMotion, the local button and crank, a bot's timing), then the rules step and
 * their new effects are presented (sounds, splashes, the phones' toasts).
 *
 * Presentation follows the rules every frame: the rod bends with the line's tension and whips through the cast, the
 * line sags or snaps taut, the bobber bobs, twitches on a nibble and dives on the bite, the fish fights near the
 * surface and leaps on its runs, and a catch is lifted out, turned to the camera and held up while SFishingHud calls
 * its weight, then goes in the bucket (junk goes back in the lake). At the end the anglers turn round with their
 * biggest catch.
 *
 * The implementation is split by concern: FishingGame.cpp (flow), FishingGameAnglers.cpp (hands, bots, poses),
 * FishingGameTackle.cpp (rod, line, bobber), FishingGameCatch.cpp (the fish), FishingGameEffects.cpp (effects to
 * sound and FX) and FishingGameCamera.cpp.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API AFishingGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	/** The title card and the camera's swing round before the round's 3-2-1 (s). */
	static constexpr double IntroSeconds = 3.6;

	AFishingGame();

	void Setup(AFishingLake* InLake, const TArray<FFishingSeat>& InSeats, APlayerController* InViewer, uint32 InSeed, const FFishingDebugFlags& InDebugFlags);
	/** A phone's sensor frame for a seat: flicks cast and hook, the wrist roll reels. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The motion pad's big button (pad:press / pad:release): hold to charge a cast, press to hook. */
	void PadPress(const FString& PlayerId);
	void PadRelease(const FString& PlayerId);
	/** A fresh round with the same anglers (and the next seed). */
	void Rematch();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Fishing::FMatch& GetMatch() const { return Match; }
	EFishingStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FFishingSeat>& GetSeats() const { return Seats; }
	const FFishingSeat* SeatOf(const FString& Id) const;
	/** The player whose camera the HUD projects through. */
	APlayerController* GetViewer() const { return Viewer; }
	double GetFinishedAge() const { return Stage == EFishingStage::Finished ? GetStageAge() : -1.0; }
	bool HasLocal() const;
	int32 LaneCount() const { return Match.Anglers.Num(); }
	/** The cast meter while a cast is held (0..1), or < 0. */
	double ChargeOf(int32 Lane) const;
	/** Over the angler's head, for the HUD's callouts and the catch card. */
	FVector HeadOf(int32 Lane) const;
	/** The bobber (or the fish on the line), for the HUD's pins. */
	FVector BobberOf(int32 Lane) const;

	TFunction<void()> OnMatchStart;
	/** Every new effect (bite, catch, snap, ...): the phones' toasts and haptics. */
	TFunction<void(const MovecadeKart::Fishing::FEffect&)> OnEffect;
	/** An angler's state changed, or the horn went (the phones' prompts). */
	TFunction<void(const MovecadeKart::Fishing::FAngler&)> OnAnglerState;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	// IArcadeGame ---------------------------------------------------------------------------------------------
	virtual FString GetArcadeId() const override { return TEXT("fishing"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	/** Local hands: the crank's inputs, each read as revolutions of a reel handle. */
	struct FLocalCrank
	{
		/** Revolutions a second, smoothed. */
		double Rate = 0.0;
		double StickAngle = 0.0;
		bool bStickValid = false;
		double MouseAngle = 0.0;
		bool bMouseValid = false;
		/** The last of the alternating keys pressed (pair * 2 + side), or -1. */
		int32 LastKey = -1;
	};

	struct FPhoneRod
	{
		MovecadeKart::Fishing::FFishingMotion Motion;
		double Reel = 0.0;
		/** Game clock (s) of the last sensor frame, and of the last backward flick (the wind-up the avatar shows). */
		double LastFrameAt = -10.0;
		double WindupAt = -10.0;
	};

	/** A house angler's plans, in match ms. */
	struct FBotMind
	{
		double NextActAt = 0.0;
		double CastPower = 0.6;
		/** When it will hook the bite it saw (< 0: none seen yet). */
		double ReactAt = -1.0;
		/** When it eases off for the fish's run (< 0: no run). */
		double EaseFrom = -1.0;
		double Reel = 0.0;
		/** Its drifting misjudgement of the green (tension units). */
		double Error = 0.0;
	};

	/** What the lane shows: smoothed poses and the moments presentation keys off (game clock, s). */
	struct FLaneShow
	{
		MovecadeKart::Fishing::EAnglerState State = MovecadeKart::Fishing::EAnglerState::Idle;
		double StateAt = -10.0;
		double TurnYaw = 0.0;
		double RodPitch = 40.0;
		double RodBend = 0.0;
		double ReelPhase = 0.0;
		/** The floating bobber's spring: height and speed. */
		double BobZ = 0.0;
		double BobV = 0.0;
		FVector CastFrom = FVector::ZeroVector;
		FVector Tip = FVector::ZeroVector;
		double JumpAt = -10.0;
		double ThrashCarry = 0.0;
		double HookedAt = -10.0;
		MovecadeKart::Fishing::EEffectKind Reaction = MovecadeKart::Fishing::EEffectKind::Cast;
		double ReactionAt = -10.0;
	};

	struct FLane
	{
		FString PlayerId;
		EAnglerKind Kind = EAnglerKind::Bot;
		/** A held cast (the local button, the pad's, a bot's wind-up): since when, and the meter. */
		bool bHolding = false;
		double HoldAt = 0.0;
		double Charge = 0.0;
		FLocalCrank Crank;
		FPhoneRod Phone;
		FBotMind Bot;
		FLaneShow Show;
	};

	/** One lane's frame of presentation, worked out by the pose and drawn by the tackle and the catch. */
	struct FLaneFrame;

	// Flow (FishingGame.cpp).
	void StartRound();
	void SetStage(EFishingStage Next);
	void TickStage(float Dt);
	void StepFixed();
	void DrainEffects();
	void AnnounceAnglerStates();
	void ReleaseAllHolds();
	void LogResults() const;
	void ApplyDemoCatch();

	// Hands and bots (FishingGameAnglers.cpp).
	int32 LaneOf(const FString& PlayerId) const;
	void ReadHands(float Dt);
	void ReadLocal(int32 LaneIndex, float Dt);
	double ReadCrankRevs(FLocalCrank& Crank) const;
	void Press(int32 LaneIndex);
	void Release(int32 LaneIndex);
	void FeedReels();
	void SteerBot(int32 LaneIndex);
	void SteerBotCast(int32 LaneIndex, double Skill);
	void SteerBotFight(int32 LaneIndex, double Skill);

	// Poses (FishingGameAnglers.cpp).
	void UpdateAnglers(float Dt);
	void PoseForState(int32 LaneIndex, FLaneFrame& Frame, float Dt);
	void PoseIdle(int32 LaneIndex, FLaneFrame& Frame);
	void PoseCasting(int32 LaneIndex, FLaneFrame& Frame);
	void PoseWaiting(int32 LaneIndex, FLaneFrame& Frame);
	void PoseFighting(int32 LaneIndex, FLaneFrame& Frame, float Dt);
	void PoseLanded(int32 LaneIndex, FLaneFrame& Frame);
	void PoseFinished(int32 LaneIndex, FLaneFrame& Frame);
	void ApplyPose(int32 LaneIndex, const FLaneFrame& Frame);

	// Tackle (FishingGameTackle.cpp).
	/** Each lane's avatar, rod, line, bobber and catch components (and their materials). */
	void BuildLanes();
	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true);
	void UpdateTackle(int32 LaneIndex, FLaneFrame& Frame, float Dt);
	void UpdateRod(int32 LaneIndex, const FVector& Base, const FRotator& Facing, double Pitch, double Yaw, double Bend, const FVector& Toward, bool bShow);
	void UpdateLine(int32 LaneIndex, const FVector& From, const FVector& To, double Sag, bool bShow);
	void UpdateBobber(int32 LaneIndex, const FLaneFrame& Frame, float Dt);
	FVector HoldPoint(int32 LaneIndex) const;

	// The catch (FishingGameCatch.cpp).
	UStaticMesh* FishMesh(int32 Species);
	void ShowFish(int32 LaneIndex, int32 Species, int32 WeightG, const FTransform& At);
	void UpdateCatch(int32 LaneIndex, const FLaneFrame& Frame, float Dt);
	bool ShowFightingFish(int32 LaneIndex, const FLaneFrame& Frame, float Dt);
	bool ShowLandedCatch(int32 LaneIndex, const FLaneFrame& Frame, float Dt);

	// Effects (FishingGameEffects.cpp).
	void PresentEffect(const MovecadeKart::Fishing::FEffect& Effect);
	void Splash(const FVector& At, double Size, bool bRing = true);
	void SnapFlash(const FVector& At);
	void RecordBurst(int32 LaneIndex);

	// Camera (FishingGameCamera.cpp).
	void UpdateCamera(float Dt);
	double ViewportAspect() const;

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

	/** Per lane: RodSegments pieces of the blank. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> RodPieces;

	/** Per lane: LineSegments pieces of line. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> LinePieces;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Bobbers;

	/** Per lane: the fish (or junk) on show, its mesh swapped per species. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Fish;

	/** One mesh per species, built the first time it is caught. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> FishMeshes;

	UPROPERTY()
	FKartMaterialCache Materials;

	/** The species each lane's Fish component is dressed as (-1: none yet). */
	TArray<int32> FishShown;
	TArray<FFishingSeat> Seats;
	TArray<FLane> Lanes;
	MovecadeKart::Fishing::FMatch Match;
	uint32 Seed = 1;
	FFishingDebugFlags DebugFlags;
	/** Effect ids rise with every effect: everything above this one is new. */
	int32 LastPresentedEffectId = 0;
	FRandomStream BotRng;
	FRandomStream FxRng;
	MovecadeKart::Games::FCuePlayer Cues;

	EFishingStage Stage = EFishingStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double StepAccumulatorMs = 0.0;
	bool bPaused = false;
	bool bResultsCommitted = false;
	bool bHornAnnounced = false;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 40.0;
	bool bCamPlaced = false;
	/** Camera shake, 0..1, decaying. */
	double Shake = 0.0;

	TSharedPtr<SFishingHud> Hud;
};
