#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KartLook.h"
#include "Race/KartBotDriver.h"
#include "Sim/KartItems.h"
#include "Sim/KartSim.h"
#include "KartRaceManager.generated.h"

class ACameraActor;
class AKartPawn;
class APlayerController;
class SKartRaceOverlay;

UENUM(BlueprintType)
enum class EKartRacePhase : uint8
{
	Waiting,
	Intro,
	Countdown,
	Racing,
	Finished,
};

UENUM(BlueprintType)
enum class EKartSeatKind : uint8
{
	/** Keyboard or a local gamepad, through this seat's player controller. */
	Local,
	Bot,
	/** A phone in the room, through ExternalInput. */
	Phone,
};

/** How well a bot drives (FKartBotTuning). Normal is the web build's lobby bot. */
UENUM(BlueprintType)
enum class EKartBotSkill : uint8
{
	Easy,
	Normal,
	Hard,
};

/** Who sits in a seat, before the race starts. */
USTRUCT(BlueprintType)
struct FKartSeatSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Kart")
	FString Name;

	UPROPERTY(EditAnywhere, Category = "Kart")
	EKartSeatKind Kind = EKartSeatKind::Bot;

	UPROPERTY(EditAnywhere, Category = "Kart")
	FLinearColor Paint = FLinearColor::White;

	/** Vehicle and driver; without one the seat gets FKartLook::MakeVaried(seat). Paint and Name above win. */
	UPROPERTY(EditAnywhere, Category = "Kart")
	FKartLook Look;

	UPROPERTY(EditAnywhere, Category = "Kart")
	bool bHasLook = false;

	/** Bot seats only. */
	UPROPERTY(EditAnywhere, Category = "Kart")
	EKartBotSkill BotSkill = EKartBotSkill::Normal;
};

USTRUCT()
struct FKartRacer
{
	GENERATED_BODY()

	int32 Seat = 0;
	FString Name;
	EKartSeatKind Kind = EKartSeatKind::Bot;
	FLinearColor Paint = FLinearColor::White;

	UPROPERTY()
	TObjectPtr<AKartPawn> Kart;

	/** The controller whose viewport follows this kart (and, for Local seats, whose input drives it). */
	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	MovecadeKart::FKartBotDriver Bot;
	/** Race-clock seconds when the gas went down during the countdown, or -1 (rocket start). */
	double GasHeldSince = -1.0;
	int32 Place = 1;
	int32 LapsSeen = 0;
	double LapStartMs = 0.0;
	double BestLapMs = 0.0;
	TArray<double> LapMs;
	bool bFinished = false;
	double FinishMs = 0.0;
	double LastTerrainRumble = -1000.0;
	bool bUseItemHeld = false;
};

USTRUCT(BlueprintType)
struct FKartResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	int32 Seat = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	int32 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	FLinearColor Paint = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	bool bFinished = false;

	/** Finish time in ms (race clock) for finishers. */
	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	double TimeMs = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	double BestLapMs = 0.0;

	/** laps * 100 + lap progress %, for ranking DNFs. */
	UPROPERTY(BlueprintReadOnly, Category = "Kart")
	double ProgressScore = 0.0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnKartRacePhase, EKartRacePhase);
/** A sound or HUD cue: Seat is -1 for race-wide cues ("race.countdown", "race.go", "race.results"). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnKartRaceCue, int32 /*Seat*/, FName /*Cue*/);
/** Phone haptics (spec 04 §6): collisionType "barrier", "kart" or "terrain". */
DECLARE_MULTICAST_DELEGATE_FourParams(FOnKartHaptic, int32 /*Seat*/, float /*Intensity*/, FName /*Type*/, bool /*bSustained*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnKartRaceResults, const TArray<FKartResult>&);

/**
 * Runs one race (spec 04 §5.2): waiting 1 s, intro 6 s (static cameras, skippable), countdown 5..1,
 * racing, then results. Every kart's sim steps here in lockstep at 60 Hz, with the race context
 * (position, leader gap, kart ahead, other karts) the web build computes from its store.
 * The race ends when everyone has finished or at min(5 min, leader + 30 s).
 */
UCLASS()
class MOVECADEKART_API AKartRaceManager : public AActor
{
	GENERATED_BODY()

public:
	AKartRaceManager();

	static AKartRaceManager* Get(const UObject* WorldContext);

	/** Spawns a kart per seat on the grid. Viewers[i] (may be null) watches seat i. */
	void Setup(TSharedPtr<const MovecadeKart::FTrack> InTrack, const TArray<FKartSeatSpec>& Seats, const TArray<APlayerController*>& Viewers);
	/** Back to the grid and through the intro again. */
	void RestartRace();
	void SkipIntro();

	EKartRacePhase GetPhase() const { return Phase; }
	double GetPhaseSeconds() const { return PhaseSeconds; }
	/** 5..1 during the countdown, else 0. */
	int32 GetCountdownValue() const;
	double GetRaceMs() const { return RaceSteps * 1000.0 / 60.0; }
	double GetDeadlineMs() const;
	int32 GetLeadLap() const;
	const TArray<FKartRacer>& GetRacers() const { return Racers; }
	/** Seat indices in race order. */
	const TArray<int32>& GetStandings() const { return Standings; }
	const TArray<FKartResult>& GetResults() const { return Results; }
	const FKartRacer* FindRacer(const AKartPawn* Kart) const;
	const MovecadeKart::FTrack* GetTrack() const { return Track.Get(); }
	double ProgressScore(const FKartRacer& Racer) const;
	const MovecadeKart::FKartItemWorld& GetItems() const { return Items; }
	/** The item world's clock (race seconds). */
	double GetItemTime() const { return RaceSteps / 60.0; }
	/** Item events since the last call (for presentation); clears the queue. */
	TArray<MovecadeKart::FKartItemEvent> ConsumeItemEvents() { return MoveTemp(PendingItemEvents); }

	/** Room pause (a phone disconnected): the race clock, sims and countdown freeze. */
	void SetPaused(bool bInPaused, const FString& Reason = FString());
	bool IsPaused() const { return bPaused; }
	/** "local" for the host's own pause menu (Esc / P / Back), else the room's reason. */
	const FString& GetPauseReason() const { return PauseReason; }

	/** If set, "race again" on the results screen calls this (room mode sends the room back to the lobby). */
	TFunction<void()> OnRestartRequested;

	/** Input for Phone seats; returns false if the seat has no input yet. */
	TFunction<bool(int32 /*Seat*/, MovecadeKart::FKartInput& /*Out*/)> ExternalInput;

	FOnKartRacePhase OnPhaseChanged;
	FOnKartRaceCue OnCue;
	FOnKartHaptic OnHaptic;
	FOnKartRaceResults OnResults;

	/** -KartAutoDrive: Local seats drive themselves (for automated checks). */
	bool bAutoDriveLocal = false;
	/** -KartSkipIntro. */
	bool bSkipIntro = false;
	/** B2: kart-to-kart contact (dead on the web). */
	bool bKartContact = true;
	/** B11: -KartLegacyWalls keeps the web's sticky walls. */
	bool bLegacyWalls = false;
	/** Set by the race UI (SKartRaceOverlay) while it handles pause, skip-intro and results input itself. */
	bool bUiOwnsHostKeys = false;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void SetPhase(EKartRacePhase NewPhase);
	void BeginRacing();
	void StepRace();
	void UpdateStandings();
	MovecadeKart::FKartInput GatherInput(FKartRacer& Racer, bool bRacing);
	MovecadeKart::FKartRaceContext MakeContext(const FKartRacer& Racer) const;
	void EmitStepEvents(FKartRacer& Racer);
	void DispatchItemEvents(const TArray<MovecadeKart::FKartItemEvent>& Events);
	MovecadeKart::FKartInput RocketAutopilot(const FKartRacer& Racer) const;
	void EndRace();
	void UpdateCameras(float DeltaSeconds);
	void UpdateOccluders();
	void UseRaceViews();
	void HandleHostKeys();

	TSharedPtr<const MovecadeKart::FTrack> Track;

	UPROPERTY()
	TArray<FKartRacer> Racers;

	UPROPERTY()
	TArray<TObjectPtr<ACameraActor>> IntroCameras;

	UPROPERTY()
	TObjectPtr<ACameraActor> ResultsCamera;

	TArray<int32> Standings;
	TArray<FKartResult> Results;
	EKartRacePhase Phase = EKartRacePhase::Waiting;
	double PhaseSeconds = 0.0;
	int64 RaceSteps = 0;
	double Accumulator = 0.0;
	double LeaderFinishMs = 0.0;
	int32 IntroShot = -1;
	int32 LastCountdownCue = 0;
	bool bPaused = false;
	FString PauseReason;
	FVector ResultsCameraPos = FVector::ZeroVector;
	TSharedPtr<SKartRaceOverlay> Overlay;

	MovecadeKart::FKartItemWorld Items;
	TArray<MovecadeKart::FKartItemEvent> PendingItemEvents;
	int32 RaceNumber = 0;

	UPROPERTY()
	TObjectPtr<class AKartItemPresenter> ItemPresenter;
};
