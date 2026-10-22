#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/Darts/DartsRules.h"
#include "Games/Darts/DartsThrow.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "KartLook.h"
#include "DartsGame.generated.h"

class ADartsPub;
class ACameraActor;
class APlayerController;
class SDartsHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMeshComponent;

UENUM()
enum class EDartsStage : uint8
{
	/** The intro card: the pub, the board and the line-up; the match clock holds. */
	Intro,
	/** The engine runs: its own 3-2-1, then the visits. */
	Match,
	/** The engine ended: final scores. */
	Finished,
};

UENUM()
enum class EThrowerKind : uint8
{
	/** Keyboard, mouse or a gamepad on this machine (whoever is up uses any of them). */
	Local,
	/** A phone in the room: its own detector decides the throw (darts:release). */
	Phone,
	Bot,
};

USTRUCT()
struct FDartsSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EThrowerKind Kind = EThrowerKind::Bot;
	/** Bots: 0 wild .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** The local thrower's live inputs (keyboard, gamepad, mouse), shown by the reticle and the HUD's meter. */
struct FLocalThrow
{
	/** Aim, -1..1 each way (the reticle; a landing point is Aim * AimReach). */
	FVector2D Aim = FVector2D::ZeroVector;
	/** The aim's smoothed speed (units/s), for steadiness. */
	double AimSpeed = 0.0;
	bool bGripped = false;
	double GripAt = 0.0;
	/** 0..1: the oscillating meter (keys, pad) or the flick's speed (mouse). */
	double Power = 0.0;
	bool bMouseFlick = false;
	double FlickSide = 0.0;
};

/** A finished visit, for the HUD's visit card (shown once its third dart sticks). */
struct FDartsVisitSummary
{
	FString PlayerId;
	int32 Round = 0;
	/** The three darts: label ("T20", "Miss") and points. */
	TArray<FString, TInlineAllocator<3>> Labels;
	TArray<int32, TInlineAllocator<3>> Points;
	int32 Total = 0;
	int32 Bonus = 0;
	int32 Bounties = 0;
	/** The checkout-style call ("ONE-EIGHTY!", "TON!", ...). */
	FText Call;
	/** Clock when it was shown; < 0 when there is none. */
	double At = -10.0;
	/** Clock when the next dart landed (the card makes way for it). */
	double EndAt = 1e9;
};

/** The camera's view-projection for one HUD paint: projects any number of world points without asking the engine each time. */
struct FDartsView
{
	FMatrix ViewProjection = FMatrix::Identity;
	FIntRect Rect;
	bool bValid = false;

	/** Where a world point (UE cm) is on screen, 0..1 each way; false when behind the camera. */
	bool Project(const FVector& World, FVector2D& Out01) const;
};

/**
 * One game of Bullseye Bash on the pub's board (Docs/Port/07-darts.md): 1-4 throwers taking three-dart visits in
 * turn, any mix of local players, phones and bots. The rules engine (DartsRules) runs on a fixed 60 Hz clock and
 * decides every dart the moment it leaves the hand; this actor flies it (the scripted 620 ms arc) with its trail,
 * sticks and wobbles it, wobbles the board, lights the bounty sector, moves the reticle, poses the thrower, directs
 * the camera (with a punch on the big hits), and plays the cues and impact effects. Owns the HUD.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ADartsGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ADartsGame();

	void Setup(ADartsPub* InPub, const TArray<FDartsSeat>& InSeats, APlayerController* InViewer);

	/** A phone's release (darts:release): only from the thrower up, after the intro card. */
	bool SubmitRelease(const FString& PlayerId, const MovecadeKart::Darts::FDartGesture& Gesture);
	/** A phone's sensor frame: the thrower up's detector drives the reticle (the release itself comes from the phone). */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);

	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	/** Test hook (-DartsLocalAuto): holds the grip as if Space were down, so a run can play the local thrower's controls. */
	void SetTestGrip(bool bDown) { bTestGrip = bDown; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The intro card's length (presentation only: the match clock holds through it). */
	static constexpr double IntroSeconds = 2.4;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Darts::FDartsMatch& GetMatch() const { return Match; }
	EDartsStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FDartsSeat>& GetSeats() const { return Seats; }
	const FDartsSeat* SeatOf(const FString& Id) const;
	/** A player's seat by its lane (the seat order), falling back to the id. */
	const FDartsSeat* SeatOf(const MovecadeKart::Darts::FDartsPlayer& Player) const;
	const FDartsSeat* GetThrower() const;
	const FLocalThrow& GetLocal() const { return Local; }
	/** Where the reticle is, -1..1 aim units, and whether it shows. */
	bool GetReticle(FVector2D& OutAim) const;
	/** World position (UE cm) of a board point in board radii, for callouts. */
	FVector BoardWorld(double X, double Y) const;
	/** The camera's projection for this frame (one per HUD paint). */
	FDartsView MakeView() const;
	/** Seconds since the match ended (results), or -1. */
	double GetFinishedAge() const { return Stage == EDartsStage::Finished ? GetStageAge() : -1.0; }
	/** The final order (player indices, best first), fixed when the match ends. */
	const TArray<int32>& GetStandingsOrder() const { return StandingsOrder; }
	/** The latest finished visit (its card shows for a couple of seconds). */
	const FDartsVisitSummary& GetLastVisit() const { return LastVisit; }
	/** A bull's white flash, 0..1 (the HUD washes the screen with it). */
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
	TFunction<void()> OnRematch;

	void Rematch();

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -----------------------------------
	virtual FString GetArcadeId() const override { return TEXT("darts"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	void SetStage(EDartsStage Next);
	void StepFixed(double DtMs);
	void Throw(const FString& PlayerId, const MovecadeKart::Darts::FDartGesture& Gesture);
	void UpdateLocalInput(float Dt);
	void UpdateBot();
	void UpdateDarts();
	void UpdateTrail();
	void UpdateBoard(float Dt);
	void UpdateReticle();
	void UpdateThrower(float Dt);
	void UpdateCamera(float Dt);
	void UpdateAmbience();
	void UpdateChalk();
	void OnArrival(const MovecadeKart::Darts::FDartsEffect& Effect);
	/** The arrival's impact: dust, chips, sparks, ring and flash by tier (impactFx.ts, plus the port's chips). */
	void ImpactFx(const MovecadeKart::Darts::FDartLanding& Landing);
	/** The visit's last dart stuck: its card, the round's cue and confetti. */
	void FinishVisit(const MovecadeKart::Darts::FDartsPlayer& Player, const MovecadeKart::Darts::FDartLanding& Last);
	void Celebrate(double Confetti, const FLinearColor& Color);
	FVector FlightWeb(const MovecadeKart::Darts::FDartFlight& Flight, double T) const;
	void PlaceDart(UStaticMeshComponent* Dart, const FVector& TipWeb, double Pitch, double Yaw);
	void SetFlightColor(UStaticMeshComponent* Dart, int32 Lane);

	UPROPERTY()
	TObjectPtr<ADartsPub> Pub;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UArcadeAvatarComponent> Avatar;

	/** Darts: the visit's stuck darts and flights draw from this pool; one more is the held dart. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Darts;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> HeldDart;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlightMaterials;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Reticle;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ReticleMaterial;

	/** The flying dart's glowing trail, and the dust motes drifting through the board's light. */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Trail;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Motes;

	TArray<FDartsSeat> Seats;
	MovecadeKart::Darts::FDartsMatch Match;
	FLocalThrow Local;
	TArray<FVector2D> MouseTrail;
	TMap<FString, MovecadeKart::Darts::FDartThrowDetector> PhoneDetectors;
	/** Effects already presented (by id): arrivals, rounds and finishes trigger once. */
	TSet<int32> SeenEffects;
	/** Flights already announced (by id): the release cue and the flick. */
	TSet<int32> SeenFlights;

	EDartsStage Stage = EDartsStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double Accumulator = 0.0;
	bool bPaused = false;
	bool bTestGrip = false;
	FString AnnouncedThrower;
	/** The seat of the thrower up (set on the turn change; GetThrower checks it against the match). */
	int32 ThrowerSeat = INDEX_NONE;
	double LastReleaseAt = -10.0;
	double BotReadyAt = 0.0;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;
	/** The crowd's own player, so a cheer isn't throttled by the room's chatter. */
	MovecadeKart::Games::FCuePlayer CrowdCues;
	double NextChatterAt = 2.5;
	/** The chalkboard's last scores (it re-chalks only on a change). */
	uint32 ChalkKey = 0;
	bool bChalked = false;
	TArray<int32> StandingsOrder;
	/** Round bonuses announced at the third dart's release, paid on the visit card when it lands (by player). */
	TMap<FString, int32> PendingBonus;
	FDartsVisitSummary LastVisit;

	// Presentation state.
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
	FString DressedFor;
	/** The avatar's throwing-hand bone, looked up once. */
	int32 HandBone = INDEX_NONE;
	/** Coach marks: shown on the first local visit until the first dart flies. */
	bool bCoachShown = false;
	double CoachDoneAt = -1.0;

	TSharedPtr<SDartsHud> Hud;
};
