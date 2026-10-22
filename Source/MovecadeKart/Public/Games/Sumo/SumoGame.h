#pragma once

#include "Art/KartMaterialCache.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "Games/Sumo/SumoGameTypes.h"
#include "Games/Sumo/SumoRules.h"
#include "SumoGame.generated.h"

class ACameraActor;
class APlayerController;
class ASumoArena;
class SSumoHud;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Fx { struct FKartFxEmit; }
namespace MovecadeKart::Net { struct FSensorFrame; }

/**
 * A match of Sumo Bumpers on the floating ring (ASumoArena): 2-4 bumper pucks, each with its player's avatar riding
 * on top. ASumoGameMode spawns it and calls Setup once; SArcadeOverlay drives pause, results and play again through
 * IArcadeGame, and SSumoHud paints over it.
 *
 * The rules (SumoRules) step on a fixed 60 Hz clock; every step reads each pilot's tilt (a phone's attitude, the
 * stick or keys, or a house bot's brain) and dash presses. Presentation follows, interpolated between steps: the
 * pucks lean into their acceleration and squash on a hit, their riders crouch, push both hands into a dash, flail as
 * they fall and cheer a won round; dashes leave a trail of fire, hits throw sparks, shake the camera and hold the
 * frame for a beat; a fall ends in a splash. The camera frames the ring and closes in as it shrinks, pushes in on a
 * round's winner, and orbits the champion at the end.
 *
 * The implementation is split by job: SumoGame.cpp (flow and IArcadeGame), SumoGameInput.cpp (pilots and the fixed
 * step), SumoGameParts.cpp (building the pucks and power-ups), SumoGamePresent.cpp (rules events to FX, sound and
 * callouts), SumoGamePucks.cpp (per-frame pucks, riders and power-ups) and SumoGameCamera.cpp.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ASumoGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ASumoGame();

	/** Seats the match (at most MovecadeKart::Sumo::MaxPlayers), builds the pucks, the camera and the HUD. */
	void Setup(ASumoArena* InArena, const TArray<FSumoSeat>& InSeats, APlayerController* InViewer, uint32 InSeed);
	/** A phone's sensor frame for a seat (by player id): its attitude is the tilt. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** The pad's A button (pad:press): a dash at the next step. */
	void PressDash(const FString& PlayerId);
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	/** A new match with the same seats (local play again; a room rematches through its lobby instead). */
	void Rematch();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD and the game mode.
	const MovecadeKart::Sumo::FMatch& GetMatch() const { return Match; }
	ESumoStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FSumoSeat>& GetSeats() const { return Seats; }
	const FSumoSeat* SeatOf(int32 Slot) const { return Seats.IsValidIndex(Slot) ? &Seats[Slot] : nullptr; }
	/** The player whose camera the HUD projects through. */
	APlayerController* GetViewer() const { return Viewer; }
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

	/** The match leaves its intro: the room's simultaneous turn and countdown. */
	TFunction<void()> OnMatchStart;
	/** Each round's countdown: the phones' prompts. */
	TFunction<void(int32 Round)> OnRoundStart;
	/** Every rules event, after the game presented it (the room's feedback and haptics). */
	TFunction<void(const MovecadeKart::Sumo::FRingEvent&)> OnEvent;
	/** The showcase is over (or the results bar committed early): the room's final scores. */
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	/** Set in Party Mode (the room restarts); locally empty, and ArcadeRestart rematches in place. */
	TFunction<void()> OnRematch;

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game).
	virtual FString GetArcadeId() const override { return TEXT("sumo"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void ArcadeCommitResults() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	// Flow (SumoGame.cpp).
	TArray<MovecadeKart::Sumo::FSeed> MatchSeeds() const;
	uint32 BotSeedFor(int32 Slot) const;
	void SkipStartEvents();
	void SetStage(ESumoStage Next);
	void CelebrateChampion();
	void TickIntro();
	void TickMatch(float Dt);
	void TickShowcase(float Dt);
	void StepUntilCaughtUp(float Dt);
	void PlayCountdownTicks();
	void UpdateArenaRing() const;
	void ExpireCallouts();
	void SpawnCamera();
	void AttachHud();

	// Pilots and the fixed step (SumoGameInput.cpp).
	void ReadLocal(FSumoPilot& Pilot, float Dt) const;
	MovecadeKart::Sumo::FInput InputFor(int32 Slot);
	void StepFixed();

	// Parts (SumoGameParts.cpp).
	void BuildPucks();
	void BuildPuck(int32 Slot, UStaticMesh* WeightMesh, UStaticMesh* Sphere);
	void BuildPowerPool();
	UStaticMeshComponent* MakeMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative, bool bShadows = true);
	USceneComponent* MakePivot(USceneComponent* Parent, const TCHAR* Name);

	// Rules events (SumoGamePresent.cpp).
	void Present(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentRoundStart(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentHit(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentShieldBlock(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentDash(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentRingOut(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentSplash(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentPowerPickup(const MovecadeKart::Sumo::FRingEvent& E);
	void PresentRoundWin(const MovecadeKart::Sumo::FRingEvent& E);
	void Impact(const FVector& At, const FLinearColor& A, const FLinearColor& B, double Strength, bool bDash);
	void Callout(const FString& Text, const FLinearColor& Color, const FVector& World, bool bBig, const FString& Sub, double Duration);
	void EmitFx(const TArray<MovecadeKart::Fx::FKartFxEmit>& Emits) const;

	// Per frame (SumoGamePucks.cpp).
	void UpdatePucks(float Dt, double Alpha);
	void UpdatePuck(int32 Slot, float Dt, double Alpha, bool bRoundOver);
	/** Places, turns, leans and squashes the puck; returns its lean (pitch, roll in degrees) for the rider. */
	FVector2D PlacePuck(int32 Slot, float Dt, double Alpha);
	void UpdateDashGlow(int32 Slot);
	void UpdateShield(int32 Slot) const;
	void UpdateStreams(int32 Slot) const;
	void PoseRider(int32 Slot, const FVector2D& Lean, bool bRoundOver) const;
	void UpdatePowerUps();
	void MatchPowerPool();
	FVector2D DrawnPos(int32 Slot, double Alpha) const;

	// The camera (SumoGameCamera.cpp): a shot in metres on the rules plane, smoothed into Cam*.
	struct FShot
	{
		FVector Pos = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		/** Vertical field of view, degrees. */
		double Fov = 40.0;
	};
	void UpdateCamera(float Dt);
	double ViewAspect() const;
	FShot PlayShot(double Aspect) const;
	void ApplyIntroShot(FShot& Shot) const;
	void ApplyWinnerShot(FShot& Shot) const;

	UPROPERTY()
	TObjectPtr<ASumoArena> Arena;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PuckMesh;

	/** The looks every puck and pickup shares (rubber, iron, the icons' glows). */
	UPROPERTY()
	FKartMaterialCache Materials;

	UPROPERTY()
	TArray<FSumoPuckParts> Parts;

	UPROPERTY()
	TArray<FSumoPowerParts> Powers;

	TArray<FSumoSeat> Seats;
	TArray<FSumoPilot> Pilots;
	MovecadeKart::Sumo::FMatch Match;
	uint32 Seed = 1;
	/** The newest rules event already presented (events are presented once, in id order). */
	int32 LastEventId = 0;
	MovecadeKart::Games::FCuePlayer Cues;
	TArray<FSumoCallout> Callouts;
	TArray<FSumoFeedLine> Feed;

	ESumoStage Stage = ESumoStage::Intro;
	/** Game time (s): stops while paused, capped per frame. */
	double Clock = 0.0;
	double StageAt = 0.0;
	/** Unstepped time (ms) toward the next fixed step. */
	double Accumulator = 0.0;
	/** Seconds the frame still holds after a hit (the rules wait meanwhile). */
	double HitStop = 0.0;
	/** Camera shake, 0..1, decaying. */
	double Shake = 0.0;
	double RoundOverAt = SumoLongAgo;
	int32 LastCountdown = 0;
	bool bPaused = false;
	bool bResultsCommitted = false;

	// The camera's smoothed shot (metres, rules plane).
	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 40.0;
	bool bCamPlaced = false;

	TSharedPtr<SSumoHud> Hud;
};
