#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/Darts/DartsGameTypes.h"
#include "Games/Darts/DartsRules.h"
#include "Games/Darts/DartsThrow.h"
#include "Games/GameAudio.h"
#include "DartsGame.generated.h"

class ACameraActor;
class ADartsPub;
class APlayerController;
class SDartsHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMeshComponent;

/**
 * One game of Bullseye Bash on the pub's board (Docs/Port/07-darts.md): 1-4 throwers taking three-dart visits in
 * turn, any mix of local players, phones and bots. ADartsGameMode spawns and seats it; it owns the HUD, the camera,
 * the thrower's avatar and the darts.
 *
 * The rules engine (DartsRules) runs on a fixed 60 Hz clock and decides every dart the moment it leaves the hand;
 * this actor presents it: flies the dart along the scripted 620 ms arc with its trail, sticks and wobbles it, shakes
 * the board, lights the bounty sector, moves the reticle, poses the thrower, directs the camera (with a punch on the
 * big hits) and plays the cues and impact effects. The work is split by job across DartsGame*.cpp (see below).
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ADartsGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ADartsGame();

	/** Seats the throwers and builds the darts, avatar, camera and HUD. Pub must be built; Viewer may be null (tests). */
	void Setup(ADartsPub* InPub, const TArray<FDartsSeat>& InSeats, APlayerController* InViewer, bool bInShotOnBigHit = false);
	/** Starts the same match over with the same seats (local play again). */
	void Rematch();

	/** A phone's release (darts:release): only from the thrower up, after the intro card. */
	bool SubmitRelease(const FString& PlayerId, const MovecadeKart::Darts::FDartGesture& Gesture);
	/** A phone's sensor frame: the thrower up's detector drives the reticle (the release itself comes from the phone). */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);

#if !UE_BUILD_SHIPPING
	/** -DartsLocalAuto: holds the grip as if Space were down, so a run can play the local thrower's controls. */
	void SetTestGrip(bool bDown) { bTestGrip = bDown; }
#endif

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The intro card's length (presentation only: the match clock holds through it). */
	static constexpr double IntroSeconds = 2.4;

	// For the HUD and the game mode ---------------------------------------------------------------------------
	const MovecadeKart::Darts::FDartsMatch& GetMatch() const { return Match; }
	EDartsStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FDartsSeat>& GetSeats() const { return Seats; }
	APlayerController* GetViewer() const { return Viewer; }
	const FDartsSeat* SeatOf(const FString& Id) const;
	/** A player's seat by its lane (the seat order), falling back to the id. */
	const FDartsSeat* SeatOf(const MovecadeKart::Darts::FDartsPlayer& Player) const;
	const FDartsSeat* GetThrower() const;
	const FLocalThrow& GetLocal() const { return Local; }
	/** Where the reticle is, -1..1 aim units, and whether it shows. */
	bool GetReticle(FVector2D& OutAim) const;
	/** World position (UE cm) of a board point in board radii, for callouts. */
	FVector BoardWorld(double X, double Y) const;
	/** Seconds since the match ended (results), or -1. */
	double GetFinishedAge() const { return Stage == EDartsStage::Finished ? GetStageAge() : -1.0; }
	/** The latest finished visit (its card shows for a couple of seconds). */
	const FDartsVisitSummary& GetLastVisit() const { return LastVisit; }
	/** A big hit's flash, 0..1 (the HUD washes the screen with it). */
	float GetFlash() const;
	FLinearColor GetFlashColor() const { return FlashColor; }
	/** The camera is on the landing's reaction shot (the HUD keeps the aim read out of it). */
	bool IsReacting() const { return bReacting; }
	/** The first-visit coach marks for local controls, 0..1 (they fade once the first dart flies). */
	float GetCoachAlpha() const;

	/** Fires when the thrower up changes (darts:turn). */
	TFunction<void(const FString&)> OnTurn;
	/** Fires for every dart thrown: (player id, outcome). */
	TFunction<void(const FString&, const MovecadeKart::Darts::FThrowOutcome&)> OnThrow;
	TFunction<void()> OnFinished;
	/** The intro card went: the match's own countdown starts (set-countdown match 3000). */
	TFunction<void()> OnMatchStart;
	TFunction<void()> OnQuit;
	/** Play again; when unset the game restarts itself (local play). */
	TFunction<void()> OnRematch;

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -----------------------------------
	virtual FString GetArcadeId() const override { return TEXT("darts"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	// DartsGame.cpp: setup, the stage flow and the fixed-step clock.
	UStaticMeshComponent* MakeDart(FName Name);
	void MakeReticle();
	void MakeAvatar();
	void MakeAmbientFx();
	void MakeCameraAndHud();
	void CreateFreshMatch();
	void SetStage(EDartsStage Next);
	void TickMatch(float Dt);
	/** Runs every whole fixed step the accumulator holds. */
	void StepPending();
	void StepFixed(double DtMs);
	/** The board passed to someone else: reset the per-turn state, dress the avatar, maybe show the coach marks. */
	void HandleTurnChange();
	void Throw(const FString& PlayerId, const MovecadeKart::Darts::FDartGesture& Gesture);

	// DartsGameInput.cpp: the local thrower's controls and the house bots.
	void UpdateLocalInput(float Dt);
	void UpdateLocalAim(const APlayerController& PC, float Dt, float MouseX, float MouseY);
	void UpdateLocalPower(float Dt, float MouseX, float MouseY);
	void ReleaseLocalDart(const APlayerController& PC, const FDartsSeat& Thrower);
	void UpdateBot();

	// DartsGameReactions.cpp: what a landing sets off.
	void OnArrival(const MovecadeKart::Darts::FDartsEffect& Effect);
	void PlayLandingCues(const MovecadeKart::Darts::FDartLanding& Landing);
	/** The big hits' camera punch, flash and cheer; returns the punch's kick (0 for an ordinary dart). */
	double PunchFor(const MovecadeKart::Darts::FDartLanding& Landing, const FDartsSeat* Seat);
	void ScheduleBigHitShot();
	/** The arrival's impact: dust, chips, sparks, ring and flash by tier (impactFx.ts, plus the port's chips). */
	void ImpactFx(const MovecadeKart::Darts::FDartLanding& Landing);
	/** The visit's last dart stuck: its card, the round's cue and confetti. */
	void FinishVisit(const MovecadeKart::Darts::FDartsPlayer& Player, const MovecadeKart::Darts::FDartLanding& Last);
	void Celebrate(double Confetti, const FLinearColor& Color);

	// DartsGameBoard.cpp: the darts, the trail, the board, the reticle, the chalkboard and the room's murmur.
	void UpdateDarts();
	/** Which landings are on the board now: the current visit's, or the visit just finished while it lingers. */
	void StickVisibleVisit(int32& Used);
	void StickDart(const MovecadeKart::Darts::FDartLanding& Landing, int32& Used);
	void FlyDarts(int32& Used);
	void UpdateTrail();
	void UpdateBoard(float Dt);
	void UpdateReticle();
	void UpdateChalk();
	void UpdateAmbience();
	FVector FlightWeb(const MovecadeKart::Darts::FDartFlight& Flight, double T) const;
	void PlaceDart(UStaticMeshComponent* Dart, const FVector& TipWeb, double Pitch, double Yaw);
	void SetFlightColor(UStaticMeshComponent* Dart, int32 Lane);

	// DartsGameThrower.cpp / DartsGameCamera.cpp: the avatar on the oche and the camera.
	void UpdateThrower();
	void UpdateHeldDart(bool bHeld, const FVector2D& Aim);
	void UpdateCamera(float Dt);

	UPROPERTY()
	TObjectPtr<ADartsPub> Pub;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UArcadeAvatarComponent> Avatar;

	/** The visit's stuck darts and the flights draw from this pool. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Darts;

	/** The dart in the thrower's hand. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> HeldDart;

	/** One per seat: the flights wear the thrower's colour. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlightMaterials;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Reticle;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ReticleMaterial;

	/** The flying dart's glowing trail. */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Trail;

	/** Dust motes drifting through the board's light. */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Motes;

	TSharedPtr<SDartsHud> Hud;

	// The match.
	TArray<FDartsSeat> Seats;
	MovecadeKart::Darts::FDartsMatch Match;
	EDartsStage Stage = EDartsStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	/** Real time not yet stepped by the rules, ms. */
	double Accumulator = 0.0;
	bool bPaused = false;
	/** Effects already presented (by id): arrivals, rounds and finishes trigger once. */
	TSet<int32> SeenEffects;
	/** Flights already announced (by id): the release cue. */
	TSet<int32> SeenFlights;
	/** Round bonuses announced at the third dart's release, paid on the visit card when it lands (by player). */
	TMap<FString, int32> PendingBonus;
	FDartsVisitSummary LastVisit;

	// Whose turn it is, and their inputs.
	FString AnnouncedThrower;
	/** The seat of the thrower up (set on the turn change; GetThrower checks it against the match). */
	int32 ThrowerSeat = INDEX_NONE;
	FLocalThrow Local;
	/** The mouse's recent vertical deltas while gripped: (clock, delta). */
	TArray<FVector2D> MouseTrail;
	TMap<FString, MovecadeKart::Darts::FDartThrowDetector> PhoneDetectors;
	double LastReleaseAt = -10.0;
	double BotReadyAt = 0.0;
	FRandomStream BotRng;
	bool bTestGrip = false;
	/** Coach marks: shown on the first local visit until the first dart flies. */
	bool bCoachShown = false;
	double CoachDoneAt = -1.0;

	// Sound.
	MovecadeKart::Games::FCuePlayer Cues;
	/** The crowd's own player, so a cheer isn't throttled by the room's chatter. */
	MovecadeKart::Games::FCuePlayer CrowdCues;
	double NextChatterAt = 2.5;

	// The board and the darts on it.
	double WobbleTarget = 0.0;
	double Wobble = 0.0;
	int32 WobbleSeed = 0;
	/** The latest landing, for the reaction shot and the avatar's reaction. */
	MovecadeKart::Darts::FDartLanding LastLanding;
	double LastLandingAt = -10.0;
	/** The visit just finished stays on the board for a beat after the board passes (the web clears it at once):
	 *  the previous visitor's index and that visit's round. */
	int32 HeldOverPlayer = INDEX_NONE;
	int32 HeldOverVisit = 0;
	FString ShownVisitor;
	double HeldOverUntil = -10.0;
	/** The chalkboard's last scores (it re-chalks only on a change). */
	uint32 ChalkKey = 0;
	bool bChalked = false;

	// The thrower and the camera.
	FString DressedFor;
	/** The avatar's throwing-hand bone, looked up once per look. */
	int32 HandBone = INDEX_NONE;
	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 44.0;
	bool bCamPlaced = false;
	/** The big-hit punch: an FOV kick and a shake that decay. */
	double Punch = 0.0;
	bool bReacting = false;
	double FlashAt = -10.0;
	float FlashStrength = 0.f;
	FLinearColor FlashColor = FLinearColor::White;

	/** The coach marks show once per session (play again and next game don't repeat them): a deliberate session
	 *  cache, set the first time a local thrower's first dart flies. */
	static bool bCoachMarksSeenThisSession;

	// Test hooks (FDartsDebugFlags).
	bool bShotOnBigHit = false;
	FTimerHandle BigHitShotTimer;
};
