#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/Bowling/BowlingPins.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "Games/Bowling/BowlingRules.h"
#include "Games/Bowling/BowlingSim.h"
#include "Games/Bowling/BowlingThrow.h"
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

/** Where a line meets the rack, as the commentary reads it (presentation only). */
enum class EBowlingLine : uint8
{
	Pocket,
	/** A spare ball through the pins that are left. */
	OnTarget,
	Brooklyn,
	HeadOn,
	Light,
	Wide,
	Gutter,
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

/** How the last throw left the hand, for the HUD's swing read-out (presentation only). */
struct FBowlingSwing
{
	double At = -100.0;
	double Speed = 0.0;
	double Power = 0.0;
	double Curve = 0.0;
	EBowlingLine Line = EBowlingLine::Light;
};

/**
 * One game of Bowling on the alley's middle lane (Docs/Port/06-bowling.md): 1-4 bowlers taking frames in turn,
 * any mix of local players, phones and bots. Each throw is resolved by the deterministic sim the moment it leaves
 * the hand; the ball's roll, the pins' tumble (FPinSolver) and the pinsetter act it out, and the roll is committed
 * to the card when the pins settle, as in the web build (runtime.ts). Owns the lane camera and the HUD, and the
 * show around a roll: the ball's trail, the impact, the slow-motion strike cam, the alley's light show, the
 * screen flash and the crowd.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ABowlingGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ABowlingGame();

	void Setup(ABowlingAlley* InAlley, const TArray<FBowlerSeat>& InSeats, APlayerController* InViewer);

	/**
	 * A phone's release (bowling:release): only from the bowler up, while aiming. One that arrives while the intro
	 * card shows is held and rolls when the card goes (runtime.ts releaseDisposition): the phone never resends it.
	 */
	bool SubmitRelease(const FString& PlayerId, const MovecadeKart::Bowling::FShot& Shot);

	/** A phone's sensor frame: the bowler up's arm and lane guide follow it (the release itself comes from the phone). */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);

	/** The room paused (a phone dropped): everything holds, with no catch-up on resume. */
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD --------------------------------------------------------------------------------------------
	const MovecadeKart::Bowling::FMatch& GetMatch() const { return Match; }
	EBowlingPhase GetPhase() const { return Phase; }
	double GetPhaseAge() const { return Clock - PhaseAt; }
	double GetClock() const { return Clock; }
	const TArray<FBowlerSeat>& GetSeats() const { return Seats; }
	/** The bowler up (cached on every turn change), or null once the match is over. */
	const FBowlerSeat* GetBowler() const { return Seats.IsValidIndex(BowlerSeat) ? &Seats[BowlerSeat] : nullptr; }
	int32 GetBowlerSeat() const { return BowlerSeat; }
	const FLocalBowl& GetLocal() const { return Local; }
	EBowlingBeat GetLastBeat() const { return LastBeat; }
	double GetLastBeatAt() const { return LastBeatAt; }
	int32 GetLastFelled() const { return LastFelled; }
	/** The streak of the bowler who threw the last committed roll (for the TURKEY banner's count). */
	int32 GetLastStreak() const { return LastStreak; }
	const MovecadeKart::Bowling::FShotResult* GetShot() const { return bHasShot ? &Shot : nullptr; }
	int32 GetStanding() const { return Standing.Num(); }
	/** Seconds since the match ended (results), or -1. */
	double GetFinishedAge() const { return Phase == EBowlingPhase::Finished ? GetPhaseAge() : -1.0; }
	/** The last throw's read-out (speed, hook, where the line meets the rack). */
	const FBowlingSwing& GetSwing() const { return Swing; }
	/** Where the live line would meet the rack, while someone aims; false when there is no live line. */
	bool GetLiveLine(EBowlingLine& OutLine) const;
	/** The screen flash: 0..1 and its colour. */
	float GetFlash(FLinearColor& OutColor) const;
	/** Throws by local bowlers so far this session (the coach marks fade after the first ones). */
	int32 GetLocalThrows() const { return LocalThrows; }
	/** Whether the local bowler up has moved their line / set a hook this turn (the coach ticks them off). */
	bool HasLocalAimed() const { return bLocalAimed; }
	bool HasLocalHooked() const { return bLocalHooked; }

	/** Fires when a roll is committed: (bowler id, pins felled, beat). For phones and audio. */
	TFunction<void(const FString&, int32, EBowlingBeat)> OnRoll;
	/** Fires when the bowler up changes (advanceTurn). */
	TFunction<void(const FString&)> OnTurn;
	TFunction<void()> OnFinished;
	/** The results' "back to the menu" (Esc / B). */
	TFunction<void()> OnQuit;
	/** The results' "play again" (Enter / A); unset, it is a local Rematch(). A room rematches through its lobby. */
	TFunction<void()> OnRematch;

	/** A fresh game for the same bowlers (the results' "play again"). */
	void Rematch();

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -----------------------------------
	virtual FString GetArcadeId() const override { return TEXT("bowling"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

	/**
	 * Where a line ending at EndX (sim metres at the head pin) meets the rack; Gutter when it drops off first. On a
	 * spare (Rack: the pins standing) the read is whether it goes through what's left.
	 */
	static EBowlingLine ReadLine(double EndX, bool bGutter, const TArray<int32>& Rack);

	/** A known strike. With -BowlShowcase (screenshots, demos) bots throw it at every full rack. */
	static MovecadeKart::Bowling::FShot ShowcaseShot();

private:
	void SetPhase(EBowlingPhase Next);
	void BeginTurn();
	/** Re-reads the bowler up from the match (after every change to the turn). */
	void RefreshBowler();
	void Launch(const MovecadeKart::Bowling::FShot& InShot);
	void Settle();
	/** The house's reaction to the beat just committed: FX, flash, light show, crowd. Mine is the thrower's colour. */
	void Celebrate(const FLinearColor& Mine);
	void UpdateLocalInput(float Dt);
	void UpdateBot();
	void UpdateBall(float Dt);
	void UpdateTrail(float Dt);
	void UpdatePins(float Dt);
	void UpdatePinsetter();
	void UpdateGuide();
	void UpdateShow();
	/** The live line for the bowler up (local inputs or the phone's detector): false when there is none to show. */
	bool LiveAim(double& OutAim, double& OutCurve, double& OutCharge) const;
	void UpdateCamera(float Dt);
	void UpdateBowler(float Dt);
	/** How far the bowling arm is swung, radians: negative behind the hip, positive forward and up. */
	double ArmSwing() const;
	/** Seconds the bowler up has been in the backswing (local grip, bot wind-up, phone swing), or -1. */
	double GripAge() const;
	void PlaceRack();
	FVector BallWorld(double T, double& OutY) const;
	/** Slows the actor's clock around the moment a strike-bound ball meets the rack. */
	double TimeScale() const;
	void Shake(double Metres);

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

	/** The played rack: one instance per pin (id - 1), hidden pins scaled to nothing. */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> PinRack;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Rake;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Setter;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Guide;

	/** The ring on the deck where the live line meets the rack. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Target;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BallMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GuideMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TargetMaterial;

	TArray<FBowlerSeat> Seats;
	int32 BowlerSeat = INDEX_NONE;
	MovecadeKart::Bowling::FMatch Match;
	MovecadeKart::Bowling::FPinSolver Solver;
	MovecadeKart::Bowling::FShotResult Shot;
	bool bHasShot = false;
	TArray<int32> Standing;
	FLocalBowl Local;
	TArray<FVector2D> MouseTrail;
	TArray<FTransform> PinTransforms;

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
	/** This ball clears a full rack: the strike cam slows down for it. */
	bool bStrikeBound = false;
	EBowlingBeat LastBeat = EBowlingBeat::None;
	double LastBeatAt = -100.0;
	int32 LastFelled = 0;
	int32 LastStreak = 0;
	FString AnnouncedBowler;
	double BallSpin = 0.0;
	/** Where the ball left the hand (web units), for the delivery blend. */
	FVector ReleaseFrom = FVector::ZeroVector;
	/** The bowler's line (web x), where they stand down the approach (web z), and the swing at release. */
	double StanceX = 0.0;
	double StanceZ = 1.72;
	double ReleaseZ = 1.72;
	double SwingAtRelease = -1.0;
	/** Bots and phones show a backswing before their ball goes. */
	double BotGripAt = -1.0;
	double PhoneGripAt = -1.0;
	FString DressedFor;
	FRandomStream BotRng;
	/** One detector per phone, for the live guide and the arm (throw.ts on the host). */
	TMap<FString, MovecadeKart::Bowling::FThrowDetector> PhoneDetectors;
	/** The phone bowler up's smoothed swing energy, 0..1. */
	double PhoneCharge = 0.0;
	TOptional<MovecadeKart::Bowling::FShot> HeldRelease;
	bool bPaused = false;
	MovecadeKart::Games::FCuePlayer Cues;
	/** The crowd's layers have their own throttles, so a cheer can stack over the lane's cues. */
	MovecadeKart::Games::FCuePlayer Crowd;
	MovecadeKart::Games::FCuePlayer CrowdHigh;

	// Presentation ------------------------------------------------------------------------------------------
	FBowlingSwing Swing;
	int32 LocalThrows = 0;
	bool bLocalAimed = false;
	bool bLocalHooked = false;
	double TrailCarry = 0.0;
	double SparkleCarry = 0.0;
	double FlashAt = -100.0;
	double FlashStrength = 0.0;
	FLinearColor FlashColor = FLinearColor::White;
	double ShowAt = -100.0;
	double ShowEnergy = 0.0;
	double ShowSeconds = 0.0;
	FLinearColor ShowTint = FLinearColor::White;
	double ShakeAt = -100.0;
	double ShakeMetres = 0.0;
	double NextAmbience = 0.0;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 39.0;
	bool bCamPlaced = false;

	TSharedPtr<SBowlingHud> Hud;
};
