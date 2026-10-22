#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/Derby/DerbyRules.h"
#include "Games/Derby/DerbySwing.h"
#include "Games/GameAudio.h"
#include "KartLook.h"
#include "DerbyGame.generated.h"

class ADerbyBallpark;
class ACameraActor;
class APlayerController;
class SDerbyHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Net { struct FSensorFrame; }

UENUM()
enum class EDerbyPhase : uint8
{
	/** The intro card and the crane over the park. */
	Intro,
	/** "Now batting": the batter steps in (the room hears whose turn it is). */
	StepIn,
	/** The pitcher on the rubber, waiting a moment (or for the batter's READY). */
	Set,
	/** The windup, then the release. */
	Windup,
	/** The ball on its way to the plate (and past it, if nobody hits it). */
	Pitch,
	/** Hit: the ball in the air. */
	Flight,
	/** The call: the distance, the count, the crowd. */
	Result,
	Finished,
};

UENUM()
enum class EDerbyKind : uint8
{
	/** Keyboard, mouse or pad on this machine (turn-based: whoever is up uses any of them). */
	Local,
	/** A phone in the room: swing it like a bat. */
	Phone,
	Bot,
};

USTRUCT()
struct FDerbySeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EDerbyKind Kind = EDerbyKind::Bot;
	/** Bots: 0 wild .. 1 locked in. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** The local batter's live inputs, for the HUD's aim and load meter. */
struct FDerbyLocal
{
	/** -1 pull (left field) .. +1 opposite (right field). */
	double Aim = 0.0;
	bool bLoading = false;
	double LoadAt = 0.0;
	/** 0..1, how loaded the swing is. */
	double Load = 0.0;
};

/**
 * One Home Run Derby (Docs/Port/10-derby.md): 1-4 batters in turn, ten pitches each (three strikes end a turn
 * early). The pitcher sets, winds up and throws the pitch the rules scheduled; the batter's swing (a local release,
 * a bot's, or a phone's swing event from FSwingDetector) is judged by DerbyRules at the moment it peaks against the
 * ball's arrival. A hit flies the rules' precomputed path with a trail, the broadcast camera following it out; home
 * runs set off fireworks and the crowd, moonshots go to slow motion. Owns the camera and the HUD.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ADerbyGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ADerbyGame();

	void Setup(ADerbyBallpark* InPark, const TArray<FDerbySeat>& InSeats, APlayerController* InViewer, uint32 InSeed);
	/** A phone's sensor frame: only the batter up's swings count. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The phone's big button: READY while the pitcher waits, a button swing while the ball is on its way. */
	void PressButton(const FString& PlayerId);
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Derby::FMatch& GetMatch() const { return Match; }
	EDerbyPhase GetPhase() const { return Phase; }
	double GetPhaseAge() const { return Clock - PhaseAt; }
	double GetClock() const { return Clock; }
	const TArray<FDerbySeat>& GetSeats() const { return Seats; }
	/** The batter on screen (during the call this is still the one who just hit, before the turn moves on). */
	const FDerbySeat* GetBatter() const;
	const MovecadeKart::Derby::FBatter* GetBatterStats() const;
	const FDerbySeat* SeatOf(const FString& Id) const;
	const FDerbyLocal& GetLocal() const { return Local; }
	/** The pitch thrown (or about to be). */
	const MovecadeKart::Derby::FPitch* GetPitch() const { return bHasPitch ? &Pitch : nullptr; }
	const MovecadeKart::Derby::FOutcome* GetOutcome() const { return bHasOutcome ? &Outcome : nullptr; }
	double GetOutcomeAt() const { return OutcomeAt; }
	/** The count the HUD shows: pitches thrown and strikes in the turn on screen. */
	int32 ShownPitch() const { return ShownPitchCount; }
	int32 ShownStrikes() const { return ShownStrikeCount; }
	/** Where the hit came down (world), once it has. */
	bool GetLanding(FVector& Out) const;
	FVector GetBallWorld() const;
	bool ScreenOf(const FVector& World, FVector2D& Out01) const;
	double GetFinishedAge() const { return Phase == EDerbyPhase::Finished ? GetPhaseAge() : -1.0; }
	bool HasLocal() const;
	bool IsSlowMo() const { return SlowMo < 0.9; }
	/** The phone batter's live swing energy, 0..1. */
	double GetPhoneCharge() const { return PhoneCharge; }

	/** The batter up changed (the room's advance-turn). */
	TFunction<void(const FString&)> OnTurn;
	/** A pitch left the hand: (batter id, pitch). For the phones' timing hint. */
	TFunction<void(const FString&, const MovecadeKart::Derby::FPitch&)> OnPitch;
	/** A pitch was decided: (batter id, outcome). */
	TFunction<void(const FString&, const MovecadeKart::Derby::FOutcome&)> OnOutcome;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	void Rematch();

	// IArcadeGame ---------------------------------------------------------------------------------------------
	virtual FString GetArcadeId() const override { return TEXT("derby"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	void SetPhase(EDerbyPhase Next);
	void BeginTurn();
	void BeginPitch();
	/** A swing for the pitch in flight: its peak on the game clock (seconds). */
	void SubmitSwing(double PeakAt, double Aim, double Strength, double AnimLead);
	void Resolve(const MovecadeKart::Derby::FSwing& Swing, double ContactAt);
	void Land();
	void UpdateLocal(float Dt);
	void UpdateBot();
	void UpdateBall(float Dt);
	void UpdatePitcher(float Dt);
	void UpdateBatter(float Dt);
	void UpdateCamera(float Dt);
	void UpdateBoard();
	void Fireworks(int32 Bursts, double Delay);
	void ContactBurst(const MovecadeKart::Derby::FOutcome& Hit);
	/** Pitch progress: the ball (world) at t seconds after the release. */
	FVector PitchBall(double T) const;
	FVector HitBall(double T) const;
	double ReleaseAt() const { return PhaseAt; }
	double ArriveAt() const { return PitchReleasedAt + Pitch.FlightMs / 1000.0; }

	UPROPERTY()
	TObjectPtr<ADerbyBallpark> Park;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UArcadeAvatarComponent> Batter;

	UPROPERTY()
	TObjectPtr<UArcadeAvatarComponent> Pitcher;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ball;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Bat;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Glove;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Helmet;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HelmetMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Trail;

	TArray<FDerbySeat> Seats;
	EDerbyPhase Phase = EDerbyPhase::Intro;
	double Clock = 0.0;
	double PhaseAt = 0.0;
	MovecadeKart::Derby::FMatch Match;
	uint32 Seed = 0;
	int32 Rematches = 0;
	MovecadeKart::Derby::FPitch Pitch;
	bool bHasPitch = false;
	MovecadeKart::Derby::FOutcome Outcome;
	bool bHasOutcome = false;
	double OutcomeAt = -100.0;
	double PitchReleasedAt = 0.0;
	/** The swing for this pitch: its peak (game clock), when its animation started, and whether it has been judged. */
	bool bSwingTaken = false;
	double SwingPeakAt = 0.0;
	double SwingAnimAt = -100.0;
	double SwingAim = 0.0;
	double SwingStrength = 0.0;
	bool bResolved = false;
	/** Hit: when the ball left the bat and the flight's own (slow-motion) clock. */
	double ContactAt = 0.0;
	double FlightClock = 0.0;
	double SlowMo = 1.0;
	bool bLanded = false;
	double HitStop = 0.0;
	int32 ShownUp = 0;
	int32 ShownPitchCount = 0;
	int32 ShownStrikeCount = 0;
	FString DressedFor;
	FString AnnouncedBatter;
	FDerbyLocal Local;
	/** Bots: the swing they mean (peak offset, aim, strength), drawn at the release. */
	bool bBotSwings = false;
	double BotTiming = 0.0;
	double BotAim = 0.0;
	double BotStrength = 0.0;
	FRandomStream BotRng;
	TMap<FString, MovecadeKart::Derby::FSwingDetector> Detectors;
	double PhoneCharge = 0.0;
	bool bReady = false;
	bool bPaused = false;
	bool bResultsCommitted = false;
	double Shake = 0.0;
	TArray<TPair<double, FVector>> PendingFireworks;
	MovecadeKart::Games::FCuePlayer Cues;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 50.0;
	bool bCamPlaced = false;

	TSharedPtr<SDerbyHud> Hud;
};
