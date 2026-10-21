#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/Darts/DartsRules.h"
#include "Games/Darts/DartsThrow.h"
#include "Games/GameAudio.h"
#include "KartLook.h"
#include "DartsGame.generated.h"

class ADartsPub;
class ACameraActor;
class APlayerController;
class SDartsHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
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

/**
 * One game of Bullseye Bash on the pub's board (Docs/Port/07-darts.md): 1-4 throwers taking three-dart visits in
 * turn, any mix of local players, phones and bots. The rules engine (DartsRules) runs on a fixed 60 Hz clock and
 * decides every dart the moment it leaves the hand; this actor flies it (the scripted 620 ms arc), sticks and
 * wobbles it, wobbles the board, lights the bounty sector, moves the reticle, poses the thrower, directs the camera,
 * and plays the cues and impact effects. Owns the HUD.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ADartsGame : public AActor
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

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Darts::FDartsMatch& GetMatch() const { return Match; }
	EDartsStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FDartsSeat>& GetSeats() const { return Seats; }
	const FDartsSeat* SeatOf(const FString& Id) const;
	const FDartsSeat* GetThrower() const;
	const FLocalThrow& GetLocal() const { return Local; }
	/** Where the reticle is, -1..1 aim units, and whether it shows. */
	bool GetReticle(FVector2D& OutAim) const;
	/** World position (UE cm) of a board point in board radii, for callouts. */
	FVector BoardWorld(double X, double Y) const;
	/** Where a world point (UE cm) is on screen, 0..1 each way; false when behind the camera. */
	bool ScreenOf(const FVector& World, FVector2D& Out01) const;
	/** Seconds since the match ended (results), or -1. */
	double GetFinishedAge() const { return Stage == EDartsStage::Finished ? GetStageAge() : -1.0; }

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

private:
	void SetStage(EDartsStage Next);
	void StepFixed(double DtMs);
	void Throw(const FString& PlayerId, const MovecadeKart::Darts::FDartGesture& Gesture);
	void UpdateLocalInput(float Dt);
	void UpdateBot();
	void UpdateDarts();
	void UpdateBoard(float Dt);
	void UpdateReticle();
	void UpdateThrower(float Dt);
	void UpdateCamera(float Dt);
	void UpdateAmbience();
	void OnArrival(const MovecadeKart::Darts::FDartsEffect& Effect);
	/** The arrival's impact: dust, sparks, ring and flash by tier (impactFx.ts). */
	void ImpactFx(const MovecadeKart::Darts::FDartLanding& Landing);
	void Celebrate(double Confetti, const FLinearColor& Color);
	FVector FlightWeb(const MovecadeKart::Darts::FDartFlight& Flight, double T) const;
	void PlaceDart(UStaticMeshComponent* Dart, const FVector& TipWeb, double Pitch, double Yaw);

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
	bool bFinishedFired = false;
	FString AnnouncedThrower;
	double LastReleaseAt = -10.0;
	double BotReadyAt = 0.0;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;
	double NextChatterAt = 2.5;

	// Presentation state.
	double WobbleTarget = 0.0;
	double Wobble = 0.0;
	int32 WobbleSeed = 0;
	/** The latest landing, for the reaction shot and the avatar's reaction. */
	MovecadeKart::Darts::FDartLanding LastLanding;
	double LastLandingAt = -10.0;
	FVector2D ReticleShown = FVector2D::ZeroVector;
	/** The visit just finished stays on the board for a beat after the board passes (the web clears it at once). */
	TArray<MovecadeKart::Darts::FDartLanding> ShownVisit;
	TArray<MovecadeKart::Darts::FDartLanding> HeldOver;
	FString ShownVisitor;
	double HeldOverUntil = -10.0;
	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 44.0;
	bool bCamPlaced = false;
	FString DressedFor;

	TSharedPtr<SDartsHud> Hud;
};
