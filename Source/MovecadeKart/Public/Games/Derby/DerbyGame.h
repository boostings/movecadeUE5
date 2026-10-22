#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/Derby/DerbyRules.h"
#include "Games/Derby/DerbySwing.h"
#include "Games/Derby/DerbyTypes.h"
#include "Games/GameAudio.h"
#include "DerbyGame.generated.h"

class ADerbyBallpark;
class ACameraActor;
class APlayerController;
class SDerbyHud;
class UArcadeAvatarComponent;
class UNiagaraComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FArcadePoseInput;

namespace MovecadeKart::Net { struct FSensorFrame; }

/**
 * One Home Run Derby (Docs/Port/10-derby.md): 1-4 batters in turn, ten pitches each (three strikes end a turn
 * early). The pitcher sets, winds up and throws the pitch the rules scheduled; the batter's swing (a local release,
 * a bot's, or a phone's swing event from FSwingDetector) is judged by DerbyRules at the moment it peaks against the
 * ball's arrival. A hit flies the rules' precomputed path with a trail, the broadcast camera following it out; home
 * runs set off fireworks and the crowd, moonshots go to slow motion.
 *
 * ADerbyGameMode spawns it and calls Setup; it owns the camera, the players' avatars and props, and the HUD.
 * The work is split by job: DerbyGame.cpp (the loop), DerbyGameInput.cpp (swings), DerbyGameStage.cpp (props and
 * the ball), DerbyGameAvatars.cpp (poses), DerbyGameCamera.cpp, DerbyGameFx.cpp (bursts, fireworks, scoreboard).
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ADerbyGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ADerbyGame();

	/** Seats in batting order; Seed draws the pitch schedule. Viewer gets the camera and the local controls. */
	void Setup(ADerbyBallpark* InPark, const TArray<FDerbySeat>& InSeats, APlayerController* InViewer, uint32 InSeed);
	/** A phone's sensor frame: only the batter up's swings count. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The phone's big button: READY while the pitcher waits, a button swing while the ball is on its way. */
	void PressButton(const FString& PlayerId);
	/** The same seats again, a new schedule. */
	void Rematch();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD and the game mode -------------------------------------------------------------------------------
	const FDerbyTuning& GetTuning() const { return Tuning; }
	const MovecadeKart::Derby::FMatch& GetMatch() const { return Match; }
	EDerbyPhase GetPhase() const { return Phase; }
	double GetPhaseAge() const { return Clock - PhaseAt; }
	double GetClock() const { return Clock; }
	const TArray<FDerbySeat>& GetSeats() const { return Seats; }
	/** The seat of the match's batter BatterIndex (seats and batters share the batting order). */
	const FDerbySeat* GetSeat(int32 BatterIndex) const { return Seats.IsValidIndex(BatterIndex) ? &Seats[BatterIndex] : nullptr; }
	const FDerbySeat* SeatOf(const FString& Id) const;
	/** The batter on screen (during the call this is still the one who just hit, before the turn moves on). */
	const FDerbySeat* GetBatter() const { return GetSeat(ShownUp); }
	int32 GetBatterIndex() const { return ShownUp; }
	const MovecadeKart::Derby::FBatter* GetBatterStats() const;
	const FDerbyLocalSwing& GetLocal() const { return Local; }
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
	/** Whose camera the HUD projects through. */
	APlayerController* GetViewer() const { return Viewer; }
	double GetFinishedAge() const { return Phase == EDerbyPhase::Finished ? GetPhaseAge() : -1.0; }
	bool IsSlowMo() const { return SlowMo < SlowMoShownBelow; }
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
	/** Set in Party Mode: a rematch goes through the room's lobby instead of Rematch(). */
	TFunction<void()> OnRematch;

	// IArcadeGame ---------------------------------------------------------------------------------------------------
	virtual FString GetArcadeId() const override { return TEXT("derby"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

protected:
	/** Timing and feel; the defaults are the shipped game. */
	UPROPERTY(EditAnywhere, Category = "Derby")
	FDerbyTuning Tuning;

private:
	/** The time scale under which the HUD calls it slow motion ("IT'S DEEP..."). */
	static constexpr double SlowMoShownBelow = 0.9;

	// The loop (DerbyGame.cpp).
	void SetPhase(EDerbyPhase Next);
	void EnterFinished();
	void BeginTurn();
	void DressBatter(int32 SeatIndex);
	void BeginPitch();
	void TickPhase(float Dt);
	void TickSet(double Age);
	void ReleasePitch();
	void TickPitch();
	void TickFlight(float Dt);
	void TickResult(double Age);
	double ResultHoldSeconds() const;
	void Resolve(const MovecadeKart::Derby::FSwing& Swing, double InContactAt);
	/** Commits the decided pitch to the match and moves to the call. */
	void RecordOutcome();
	bool IsPhoneUp() const;
	/** When the pitch in flight reaches the plate (game clock). */
	double ArriveAt() const { return PitchReleasedAt + Pitch.FlightMs / 1000.0; }

	// Swings (DerbyGameInput.cpp).
	/** A swing for the pitch in flight: its peak on the game clock (seconds). */
	void SubmitSwing(double PeakAt, double Aim, double Strength);
	void UpdateLocal(float Dt);
	bool IsLocalSwingHeld() const;
	void UpdateLocalAim(float Dt);
	void UpdateBot();

	// Props and the ball (DerbyGameStage.cpp).
	void BuildStage();
	UStaticMeshComponent* AddProp(const TCHAR* Name, UStaticMesh* Mesh);
	UArcadeAvatarComponent* AddAvatar(const TCHAR* Name, float Scale);
	/** The pitch in flight: the ball (world) T seconds after the release. */
	FVector PitchBall(double T) const;
	/** The hit: the ball (world) T seconds after contact. */
	FVector HitBall(double T) const;
	bool HasHitFlight() const;
	void UpdateBall();
	void UpdateTrail(const FVector& At, bool bTrail);

	// Poses (DerbyGameAvatars.cpp).
	void UpdatePitcher();
	void UpdateBatter();
	/** The barrel's angle round the batter (A) and elevation (E), degrees, and the coil 0..1 of a loaded stance. */
	void BatAngles(double& OutA, double& OutE, double& OutCoil) const;
	double WaitingLoad() const;
	void PoseBatterCall(FArcadePoseInput& Pose) const;
	void PlaceBat(const FVector& BatDirLocal, bool bFlipped);

	// The camera (DerbyGameCamera.cpp).
	void UpdateCamera(float Dt);
	void FlightShot(double Age, FVector& Pos, FVector& Look, double& Fov, double& Response) const;

	// Bursts, fireworks and the scoreboard (DerbyGameFx.cpp).
	void ContactBurst(const MovecadeKart::Derby::FOutcome& Hit);
	void Land();
	void QueueFireworks(int32 Bursts, double Delay);
	void LaunchDueFireworks();
	void UpdateBoard();

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
	TObjectPtr<UNiagaraComponent> Trail;

	TArray<FDerbySeat> Seats;
	MovecadeKart::Derby::FMatch Match;
	uint32 Seed = 0;
	int32 Rematches = 0;

	// The loop.
	EDerbyPhase Phase = EDerbyPhase::Intro;
	/** The derby's own clock (seconds): it holds through a pause and crawls through hit-stop. */
	double Clock = 0.0;
	double PhaseAt = 0.0;
	bool bPaused = false;
	bool bResultsCommitted = false;
	/** The phone batter pressed READY during the set. */
	bool bReady = false;

	// The pitch up.
	MovecadeKart::Derby::FPitch Pitch;
	bool bHasPitch = false;
	double PitchReleasedAt = 0.0;
	MovecadeKart::Derby::FOutcome Outcome;
	bool bHasOutcome = false;
	double OutcomeAt = -100.0;
	bool bResolved = false;

	/** The swing for this pitch: its peak (game clock), when its animation started, and its aim and strength. */
	bool bSwingTaken = false;
	double SwingPeakAt = 0.0;
	double SwingAnimAt = -100.0;
	double SwingAim = 0.0;
	double SwingStrength = 0.0;

	/** Hit: when the ball left the bat, the flight's own (slow-motion) clock and its time scale. */
	double ContactAt = 0.0;
	double FlightClock = 0.0;
	double SlowMo = 1.0;
	bool bLanded = false;
	/** Seconds of hit-stop left. */
	double HitStop = 0.0;

	// What the HUD and the scoreboard show (they lag the match by the call).
	int32 ShownUp = 0;
	int32 ShownPitchCount = 0;
	int32 ShownStrikeCount = 0;
	/** The seat the batter avatar is dressed as, and the last batter the room was told about. */
	FString DressedFor;
	FString AnnouncedBatter;

	// Swings.
	FDerbyLocalSwing Local;
	/** Bots and fireworks: deterministic per run, separate from the rules' pitch RNG. */
	FRandomStream BotRng;
	TMap<FString, MovecadeKart::Derby::FSwingDetector> Detectors;
	double PhoneCharge = 0.0;

	// Presentation.
	double Shake = 0.0;
	/** Fireworks queued: (game clock when it bursts, where). */
	TArray<TPair<double, FVector>> PendingFireworks;
	MovecadeKart::Games::FCuePlayer Cues;
	/** The camera's eased position and target (metres) and vertical field of view. */
	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 50.0;
	/** False: the next camera update cuts instead of easing. */
	bool bCamPlaced = false;

	TSharedPtr<SDerbyHud> Hud;
};
