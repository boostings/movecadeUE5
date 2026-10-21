#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/Bowling/BowlingPins.h"
#include "Games/GameAudio.h"
#include "Games/Bowling/BowlingRules.h"
#include "Games/Bowling/BowlingSim.h"
#include "KartLook.h"
#include "BowlingGame.generated.h"

class ABowlingAlley;
class UArcadeAvatarComponent;
class ACameraActor;
class APlayerController;
class SBowlingHud;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UENUM()
enum class EBowlingPhase : uint8
{
	/** The intro card: the lane at rest, nobody up yet. */
	Intro,
	Aiming,
	Delivery,
	Rolling,
	Settling,
	Reset,
	Finished,
};

UENUM()
enum class EBowlerKind : uint8
{
	/** Keyboard, mouse or a gamepad on this machine (turn-based: whoever is up uses any of them). */
	Local,
	/** A phone in the room: its own detector decides the throw (bowling:release). */
	Phone,
	Bot,
};

/** How a roll reads (presentation.ts classifyBowlingBeat), in that order of precedence. */
enum class EBowlingBeat : uint8
{
	None,
	Gutter,
	SplitConvert,
	Turkey,
	Strike,
	Spare,
	Split,
	Good,
	Open,
};

USTRUCT()
struct FBowlerSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EBowlerKind Kind = EBowlerKind::Bot;
	/** Bots: 0 wild .. 1 steady. */
	float Skill = 0.6f;
	FKartLook Look;
	/** Strikes in a row for this bowler (W3: per player, not across the lane). */
	int32 Streak = 0;
};

/** The local bowler's live inputs (keyboard, gamepad, mouse), shown by the HUD's guide and meter. */
struct FLocalBowl
{
	double Aim = 0.0;
	double Curve = 0.0;
	bool bGripped = false;
	double GripAt = 0.0;
	/** 0..1: the oscillating meter (keys, pad) or the flick's speed (mouse). */
	double Power = 0.0;
	bool bMouseFlick = false;
};

/**
 * One game of Bowling on the alley's middle lane (Docs/Port/06-bowling.md): 1-4 bowlers taking frames in turn,
 * any mix of local players, phones and bots. Each throw is resolved by the deterministic sim the moment it leaves
 * the hand; the ball's roll, the pins' tumble (FPinSolver) and the pinsetter act it out, and the roll is committed
 * to the card when the pins settle, as in the web build (runtime.ts). Owns the lane camera and the HUD.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ABowlingGame : public AActor
{
	GENERATED_BODY()

public:
	ABowlingGame();

	void Setup(ABowlingAlley* InAlley, const TArray<FBowlerSeat>& InSeats, APlayerController* InViewer);

	/** A phone's release (bowling:release): only from the bowler up, while aiming. */
	bool SubmitRelease(const FString& PlayerId, const MovecadeKart::Bowling::FShot& Shot);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD --------------------------------------------------------------------------------------------
	const MovecadeKart::Bowling::FMatch& GetMatch() const { return Match; }
	EBowlingPhase GetPhase() const { return Phase; }
	double GetPhaseAge() const { return Clock - PhaseAt; }
	double GetClock() const { return Clock; }
	const TArray<FBowlerSeat>& GetSeats() const { return Seats; }
	const FBowlerSeat* GetBowler() const;
	const FLocalBowl& GetLocal() const { return Local; }
	EBowlingBeat GetLastBeat() const { return LastBeat; }
	double GetLastBeatAt() const { return LastBeatAt; }
	int32 GetLastFelled() const { return LastFelled; }
	const MovecadeKart::Bowling::FShotResult* GetShot() const { return bHasShot ? &Shot : nullptr; }
	int32 GetStanding() const { return Standing.Num(); }
	/** Seconds since the match ended (results), or -1. */
	double GetFinishedAge() const { return Phase == EBowlingPhase::Finished ? GetPhaseAge() : -1.0; }

	/** Fires when a roll is committed: (bowler id, pins felled, beat). For phones and audio. */
	TFunction<void(const FString&, int32, EBowlingBeat)> OnRoll;
	/** Fires when the bowler up changes (advanceTurn). */
	TFunction<void(const FString&)> OnTurn;
	TFunction<void()> OnFinished;
	/** The results' "back to the menu" (Esc / B). */
	TFunction<void()> OnQuit;

	/** A fresh game for the same bowlers (the results' "play again"). */
	void Rematch();

private:
	void SetPhase(EBowlingPhase Next);
	void BeginTurn();
	void Launch(const MovecadeKart::Bowling::FShot& InShot);
	void Settle();
	void UpdateLocalInput(float Dt);
	void UpdateBot();
	void UpdateBall(float Dt);
	void UpdatePins(float Dt);
	void UpdatePinsetter();
	void UpdateGuide();
	void UpdateCamera(float Dt);
	void UpdateBowler(float Dt);
	/** How far the bowling arm is swung, radians: negative behind the hip, positive forward and up. */
	double ArmSwing() const;
	void PlaceRack();
	FVector BallWorld(double T, double& OutY) const;

	UPROPERTY()
	TObjectPtr<ABowlingAlley> Alley;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ball;

	/** The bowler up, dressed in their look; it changes clothes between turns. */
	UPROPERTY()
	TObjectPtr<UArcadeAvatarComponent> Avatar;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pins;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Rake;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Setter;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Guide;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BallMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GuideMaterial;

	TArray<FBowlerSeat> Seats;
	MovecadeKart::Bowling::FMatch Match;
	MovecadeKart::Bowling::FPinSolver Solver;
	MovecadeKart::Bowling::FShotResult Shot;
	bool bHasShot = false;
	TArray<int32> Standing;
	FLocalBowl Local;
	TArray<FVector2D> MouseTrail;

	EBowlingPhase Phase = EBowlingPhase::Intro;
	double Clock = 0.0;
	double PhaseAt = 0.0;
	double RollSeconds = 2.0;
	double ResetSeconds = 0.56;
	bool bRackResetPending = false;
	/** The roll just committed ended the bowler's turn. */
	bool bTurnEndedPending = false;
	/** The first ball of the frame left a split (for the split-convert beat). */
	bool bSplitLeft = false;
	bool bFxFired = false;
	EBowlingBeat LastBeat = EBowlingBeat::None;
	double LastBeatAt = -100.0;
	int32 LastFelled = 0;
	FString AnnouncedBowler;
	double BallSpin = 0.0;
	/** Where the ball left the hand (web units), for the delivery blend. */
	FVector ReleaseFrom = FVector::ZeroVector;
	/** The bowler's line (web x) and the swing at the moment of release. */
	double StanceX = 0.0;
	double SwingAtRelease = -1.0;
	/** Bots and phones show a backswing before their ball goes. */
	double BotGripAt = -1.0;
	FString DressedFor;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 39.0;
	bool bCamPlaced = false;

	TSharedPtr<SBowlingHud> Hud;
};
