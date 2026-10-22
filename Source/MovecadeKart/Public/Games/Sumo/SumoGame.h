#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "Games/Sumo/SumoRules.h"
#include "KartLook.h"
#include "SumoGame.generated.h"

class ASumoArena;
class ACameraActor;
class APlayerController;
class SSumoHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Net { struct FSensorFrame; }

UENUM()
enum class ESumoStage : uint8
{
	/** The intro card and the camera's sweep in from the sea; the rules hold. */
	Intro,
	Match,
	Finished,
};

UENUM()
enum class ESumoPilot : uint8
{
	/** The left stick or WASD roll; A, Space or a click dashes. */
	Local,
	/** A phone's tilt rolls (its attitude in sensor frames); the pad's A button dashes. */
	Phone,
	Bot,
};

USTRUCT()
struct FSumoSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	ESumoPilot Kind = ESumoPilot::Bot;
	/** Bots: 0 sloppy .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** A word that pops over the action (or the screen's middle): KNOCKOUT!, SHIELD!, a round's winner. */
struct FSumoCallout
{
	FString Text;
	FString Sub;
	FLinearColor Color = FLinearColor::White;
	/** World position to float over, or zero for the screen's middle. */
	FVector World = FVector::ZeroVector;
	bool bCentre = false;
	bool bBig = false;
	double At = 0.0;
	double Duration = 1.4;
};

/** A line of the knockout feed: who put whom in the sea. */
struct FSumoFeedLine
{
	int32 Killer = INDEX_NONE;
	int32 Victim = INDEX_NONE;
	double At = 0.0;
};

/**
 * A match of Sumo Bumpers on the floating ring (ASumoArena): 2-4 bumper pucks, each with its player's avatar
 * riding on top. The rules (SumoRules) step on a fixed 60 Hz clock; every step reads each pilot's tilt (a phone's
 * attitude, the stick or keys, or a house bot's brain) and dash presses. Presentation follows, interpolated
 * between steps: the pucks lean into their acceleration and squash on a hit, their riders crouch, push both
 * hands into a dash, flail as they fall and cheer a won round; dashes leave a trail of fire, hits throw sparks,
 * shake the camera and hold the frame for a beat; a fall ends in a splash. The camera frames the ring and closes
 * in as it shrinks, pushes in on a round's winner, and orbits the champion at the end.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ASumoGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ASumoGame();

	void Setup(ASumoArena* InArena, const TArray<FSumoSeat>& InSeats, APlayerController* InViewer, uint32 InSeed);
	/** A phone's sensor frame for a seat (by player id): its attitude is the tilt. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The pad's A button (pad:press): a dash at the next step. */
	void PressDash(const FString& PlayerId);
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Sumo::FMatch& GetMatch() const { return Match; }
	ESumoStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FSumoSeat>& GetSeats() const { return Seats; }
	const FSumoSeat* SeatOf(int32 Slot) const { return Seats.IsValidIndex(Slot) ? &Seats[Slot] : nullptr; }
	bool ScreenOf(const FVector& World, FVector2D& Out01) const;
	/** Where a puck is drawn (cm), between steps and through its fall. */
	FVector PuckWorld(int32 Slot) const;
	double GetFinishedAge() const { return Stage == ESumoStage::Finished ? GetStageAge() : -1.0; }
	bool HasLocal() const;
	/** A phone that has gone quiet (no frame for a second). */
	bool IsPhoneQuiet(int32 Slot) const;
	const TArray<FSumoCallout>& GetCallouts() const { return Callouts; }
	const TArray<FSumoFeedLine>& GetFeed() const { return Feed; }
	/** The age of the round's fight (s), or < 0 before it. */
	double GetFightAge() const;
	FLinearColor ColorOf(int32 Slot) const;

	TFunction<void()> OnMatchStart;
	/** Each round's countdown: the phones' prompts. */
	TFunction<void(int32 Round)> OnRoundStart;
	/** Every rules event, after the game presented it (the room's feedback and haptics). */
	TFunction<void(const MovecadeKart::Sumo::FRingEvent&)> OnEvent;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	void Rematch();

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -----------------------------------
	virtual FString GetArcadeId() const override { return TEXT("sumo"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	struct FPilot
	{
		ESumoPilot Kind = ESumoPilot::Bot;
		MovecadeKart::Sumo::FBotBrain Brain;
		// Local: the smoothed keys and stick, a queued press.
		FVector2D LocalTilt = FVector2D::ZeroVector;
		bool bDashQueued = false;
		// Phone: the latest tilt and when it came.
		FVector2D PhoneTilt = FVector2D::ZeroVector;
		double PhoneAt = -100.0;
		// Presentation.
		FVector2D PrevPos = FVector2D::ZeroVector;
		FVector2D PrevVel = FVector2D::ZeroVector;
		FVector2D AccelSmooth = FVector2D::ZeroVector;
		double Yaw = 0.0;
		double Squash = 0.0;
		double HitAt = -10.0;
		FVector2D HitDir = FVector2D::ZeroVector;
		double DashAt = -10.0;
		double ReadyFlashAt = -10.0;
		bool bWasReady = true;
		double CheerAt = -10.0;
		double FallSpin = 0.0;
	};

	struct FPuckParts
	{
		TObjectPtr<USceneComponent> Root;
		TObjectPtr<USceneComponent> Tilt;
		TObjectPtr<UStaticMeshComponent> Body;
		TObjectPtr<UStaticMeshComponent> Shield;
		TObjectPtr<UStaticMeshComponent> Weight;
		TObjectPtr<UArcadeAvatarComponent> Rider;
		TObjectPtr<UPointLightComponent> Light;
		TObjectPtr<UNiagaraComponent> Trail;
		TObjectPtr<UNiagaraComponent> Sparks;
		TObjectPtr<UMaterialInstanceDynamic> Paint;
		TObjectPtr<UMaterialInstanceDynamic> Trim;
		TObjectPtr<UMaterialInstanceDynamic> ShieldMat;
	};

	struct FPowerParts
	{
		int32 Id = INDEX_NONE;
		double BornAt = 0.0;
		double GoneAt = -1.0;
		MovecadeKart::Sumo::EPower Kind = MovecadeKart::Sumo::EPower::None;
		FVector2D Pos = FVector2D::ZeroVector;
		TObjectPtr<USceneComponent> Root;
		TObjectPtr<UStaticMeshComponent> Icons[3];
		TObjectPtr<UStaticMeshComponent> Halo;
		TObjectPtr<UMaterialInstanceDynamic> HaloMat;
		TObjectPtr<UPointLightComponent> Light;
	};

	void SetStage(ESumoStage Next);
	void StepFixed();
	void ReadLocal(FPilot& Pilot, float Dt);
	void Present(const MovecadeKart::Sumo::FRingEvent& E);
	void Impact(const FVector& At, const FLinearColor& A, const FLinearColor& B, double Strength, bool bDash);
	void Callout(const FString& Text, const FLinearColor& Color, const FVector& World, bool bBig, const FString& Sub = FString(), double Duration = 1.4);
	void UpdatePucks(float Dt, double Alpha);
	void UpdatePowerUps(float Dt);
	void UpdateCamera(float Dt);
	void BuildPucks();
	void BuildPowerPool();
	UStaticMeshComponent* MakeMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative, bool bShadows = true);
	FVector2D DrawnPos(int32 Slot, double Alpha) const;

	UPROPERTY()
	TObjectPtr<ASumoArena> Arena;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PuckMesh;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> Keep;

	TArray<FSumoSeat> Seats;
	TArray<FPilot> Pilots;
	TArray<FPuckParts> Parts;
	TArray<FPowerParts> Powers;
	MovecadeKart::Sumo::FMatch Match;
	uint32 Seed = 1;
	int32 LastEventId = 0;
	MovecadeKart::Games::FCuePlayer Cues;
	TArray<FSumoCallout> Callouts;
	TArray<FSumoFeedLine> Feed;

	ESumoStage Stage = ESumoStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double Accumulator = 0.0;
	double HitStop = 0.0;
	double Shake = 0.0;
	double RoundOverAt = -10.0;
	int32 LastCountdown = 0;
	bool bPaused = false;
	bool bResultsCommitted = false;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 40.0;
	bool bCamPlaced = false;

	TSharedPtr<SSumoHud> Hud;
};
