#include "Race/KartRaceManager.h"

#include "Algo/Count.h"
#include "Audio/KartAudioComponent.h"
#include "Audio/KartAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartDebugFlags.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "KartPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Race/KartItemPresenter.h"
#include "Race/SKartRaceOverlay.h"
#include "Misc/Crc.h"
#include "Store/KartStoreSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartRace, Log, All);

using namespace MovecadeKart;

namespace
{
	// spec 04 §5.2, legacy store.ts RACE_CONFIG
	constexpr double WaitingSeconds = 1.0;
	constexpr double IntroSeconds = 6.0;
	constexpr double CountdownSeconds = 5.0;
	constexpr int32 CountdownFrom = 5;
	constexpr double MaxRaceMs = 300000.0;
	constexpr double FinishGraceMs = 30000.0;
	/** Up to this many steps of real time per frame: a busy GPU (down to 15 fps) keeps race time, and only a long
	 *  hitch drops time instead of spiralling. */
	constexpr int32 MaxStepsPerFrame = 4;
	/** The countdown crane lasts this long (AKartPawn::PlayCountdownSwoop). */
	constexpr double SwoopSeconds = 2.4;
	/** The item world's fixed seed; every restart takes the next one, so a rematch plays differently. */
	constexpr int32 ItemSeed = 1234;
	/** Bots rev on the grid for the last two counts, each in its own rhythm. */
	constexpr double BotRevLead = 2.0;

	/** MultiplayerKart.tsx:996-1042: the rocket aims at the waypoint 3 past the nearest (gain 2.5), and blends toward
	 *  the nearest kart ahead within 30 m (gain 3). */
	constexpr int32 RocketLookAheadWaypoints = 3;
	constexpr double RocketPathGain = 2.5;
	constexpr double RocketRivalRange = 30.0;
	constexpr double RocketRivalGain = 3.0;
}

AKartRaceManager::AKartRaceManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bStepTrace = FKartDebugFlags::Get().bStepTrace;
}

AKartRaceManager* AKartRaceManager::Get(const UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	if (TActorIterator<AKartRaceManager> It{World})
	{
		return *It;
	}
	return nullptr;
}

void AKartRaceManager::BeginPlay()
{
	Super::BeginPlay();
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Overlay = SNew(SKartRaceOverlay).Manager(this);
		Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 10);
	}
}

void AKartRaceManager::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		if (Overlay.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
		Viewport->SetForceDisableSplitscreen(false);
	}
	Overlay.Reset();
	Super::EndPlay(Reason);
}

void AKartRaceManager::Setup(TSharedPtr<const FTrack> InTrack, const TArray<FKartSeatSpec>& Seats, const TArray<APlayerController*>& Viewers)
{
	check(InTrack.IsValid());
	Track = InTrack;
	SpawnRacers(Seats, Viewers);
	SpawnCameras();
	Items.Init(Track.Get(), Track->ItemBoxes, Racers.Num(), ItemSeed);
	if (!ItemPresenter)
	{
		ItemPresenter = GetWorld()->SpawnActor<AKartItemPresenter>();
	}
	ItemPresenter->Bind(this);
	RestartRace();
}

void AKartRaceManager::SpawnRacers(const TArray<FKartSeatSpec>& Seats, const TArray<APlayerController*>& Viewers)
{
	// Setup may run again (a new room race): the previous karts go with the old racers.
	for (FKartRacer& Old : Racers)
	{
		if (Old.Kart)
		{
			Old.Kart->Destroy();
		}
	}
	Racers.Reset();
	const int32 Count = FMath::Min(Seats.Num(), Track->Grid.Num());
	for (int32 Seat = 0; Seat < Count; ++Seat)
	{
		const FSpawnSlot& Slot = Track->Grid[Seat];
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AKartPawn* Kart = GetWorld()->SpawnActor<AKartPawn>(AKartPawn::StaticClass(),
			FTransform(FRotator(0.0, Coords::YawToUE(Slot.Yaw), 0.0), Coords::ToUE(Slot.Position)), Params);
		if (!ensureMsgf(Kart, TEXT("Kart for seat %d failed to spawn"), Seat))
		{
			break; // seats stay contiguous: racer i is seat i everywhere
		}
		const FKartSeatSpec& Spec = Seats[Seat];
		FKartLook Look = Spec.bHasLook ? Spec.Look : FKartLook::MakeVaried(Seat);
		Look.Paint = Spec.Paint;
		Look.Name = Spec.Name;
		Kart->SetLook(Look);
		Kart->GetKartMovement()->SetExternallyStepped(true);
		Kart->GetKartMovement()->MutableSim().bLegacyStickyWalls = bLegacyWalls;
		Kart->GetKartMovement()->InitSim(Track, Seat);

		FKartRacer& Racer = Racers.AddDefaulted_GetRef();
		Racer.Seat = Seat;
		Racer.Name = Spec.Name;
		Racer.Kind = Spec.Kind;
		Racer.Paint = Spec.Paint;
		switch (Spec.BotSkill)
		{
		case EKartBotSkill::Easy: Racer.Bot.Tuning = FKartBotTuning::Easy(); break;
		case EKartBotSkill::Hard: Racer.Bot.Tuning = FKartBotTuning::Hard(); break;
		default: Racer.Bot.Tuning = FKartBotTuning::Normal(); break;
		}
		Racer.Kart = Kart;
		Racer.Viewer = Viewers.IsValidIndex(Seat) ? Viewers[Seat] : nullptr;
		if (Racer.Viewer)
		{
			// Read this frame's local input, not last frame's: the controllers tick first.
			AddTickPrerequisiteActor(Racer.Viewer);
			Racer.Viewer->Possess(Kart);
		}
		Kart->SetSplitScreenCamera(Count > 1 && Viewers.Num() > 1);
		Kart->SnapCamera();
		if (UKartAudioComponent* Audio = Kart->GetKartAudio())
		{
			Audio->SetSeat(Seat, Seat, Count);
		}
	}
}

void AKartRaceManager::RestartRace()
{
	for (FKartRacer& Racer : Racers)
	{
		Racer.Kart->GetKartMovement()->ResetToGrid();
		Racer.Kart->SnapCamera();
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			Audio->ResetAudio();
		}
		Racer.Bot.Reset();
		Racer.GasHeldSince = -1.0;
		Racer.Place = Racer.Seat + 1;
		Racer.LapsSeen = 0;
		Racer.LapStartMs = 0.0;
		Racer.BestLapMs = 0.0;
		Racer.LapMs.Reset();
		Racer.bFinished = false;
		Racer.FinishMs = 0.0;
		Racer.bUseItemHeld = false;
		Racer.LastTerrainRumble = -1000.0;
		Racer.Kart->SetFinishPlace(0);
	}
	++RaceNumber;
	Items.Reset(ItemSeed + RaceNumber);
	PendingItemEvents.Reset();
	Results.Reset();
	LeaderFinishMs = 0.0;
	RaceSteps = 0;
	Accumulator = 0.0;
	UpdateStandings();
	SetPhase(EKartRacePhase::Waiting);
}

void AKartRaceManager::SkipIntro()
{
	if (Phase == EKartRacePhase::Intro || Phase == EKartRacePhase::Waiting)
	{
		SetPhase(EKartRacePhase::Countdown);
	}
}

int32 AKartRaceManager::GetCountdownValue() const
{
	return Phase == EKartRacePhase::Countdown ? FMath::Clamp(CountdownFrom - FMath::FloorToInt32(PhaseSeconds), 1, CountdownFrom) : 0;
}

double AKartRaceManager::GetDeadlineMs() const
{
	return LeaderFinishMs > 0.0 ? FMath::Min(MaxRaceMs, LeaderFinishMs + FinishGraceMs) : MaxRaceMs;
}

int32 AKartRaceManager::GetLeadLap() const
{
	int32 Lap = 1;
	for (const FKartRacer& Racer : Racers)
	{
		Lap = FMath::Max(Lap, FMath::Min(Racer.Kart->GetKartMovement()->SimState().LapsCompleted + 1, Track->Laps));
	}
	return Lap;
}

const FKartRacer* AKartRaceManager::FindRacer(const AKartPawn* Kart) const
{
	return Racers.FindByPredicate([Kart](const FKartRacer& Racer) { return Racer.Kart == Kart; });
}

double AKartRaceManager::ProgressScore(const FKartRacer& Racer) const
{
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	return MovecadeKart::ProgressScore(S.LapsCompleted, S.ProgressPercent);
}

void AKartRaceManager::SetPhase(EKartRacePhase NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0;
	IntroShot = -1;
	LastCountdownCue = 0;

	// The intro flyover and the results are one full-screen shot; the race is one view per viewer.
	const bool bFullScreenShot = NewPhase == EKartRacePhase::Intro || NewPhase == EKartRacePhase::Finished;
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->SetForceDisableSplitscreen(bFullScreenShot);
	}
	if (NewPhase == EKartRacePhase::Finished && ResultsCamera)
	{
		CutToResults();
	}
	else if (!bFullScreenShot)
	{
		UseRaceViews();
	}
	// The crane down onto the grid opens the countdown after the flyover; without a flyover it is the race's very
	// first shot (so there is no cut from the chase view up to the crane).
	const bool bSwoop = bSkipIntro ? NewPhase == EKartRacePhase::Waiting : NewPhase == EKartRacePhase::Countdown;
	if (bSwoop)
	{
		for (FKartRacer& Racer : Racers)
		{
			Racer.Kart->PlayCountdownSwoop(bSkipIntro ? WaitingSeconds + SwoopSeconds : SwoopSeconds);
		}
	}
	SetAudioPhase(NewPhase);
	OnPhaseChanged.Broadcast(NewPhase);
}

void AKartRaceManager::SetPaused(bool bInPaused, const FString& Reason)
{
	bPaused = bInPaused;
	PauseReason = bInPaused ? Reason : FString();
	for (FKartRacer& Racer : Racers)
	{
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			Audio->SetPaused(bInPaused);
		}
	}
}

void AKartRaceManager::HandleHostKeys()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || bUiOwnsHostKeys)
	{
		return;
	}
	const bool bConfirm = PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right);
	const bool bPauseKey = PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::P) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left);

	// Local pause menu (not in room mode, where the room owns pauses).
	if (bPaused && PauseReason == TEXT("local"))
	{
		if (bPauseKey || bConfirm)
		{
			SetPaused(false);
		}
		else if (PC->WasInputKeyJustPressed(EKeys::R))
		{
			SetPaused(false);
			RestartRace();
		}
		else if (PC->WasInputKeyJustPressed(EKeys::Q))
		{
			UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
		}
		return;
	}
	if (bPaused)
	{
		return;
	}
	if (bPauseKey && !OnRestartRequested && (Phase == EKartRacePhase::Countdown || Phase == EKartRacePhase::Racing))
	{
		SetPaused(true, TEXT("local"));
		return;
	}
	if (!bConfirm)
	{
		return;
	}
	if (Phase == EKartRacePhase::Intro || Phase == EKartRacePhase::Waiting)
	{
		SkipIntro();
	}
	else if (Phase == EKartRacePhase::Finished)
	{
		if (OnRestartRequested)
		{
			OnRestartRequested();
		}
		else
		{
			RestartRace();
		}
	}
}

void AKartRaceManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Track.IsValid() || Racers.Num() == 0)
	{
		return;
	}
	HandleHostKeys();
	if (bPaused)
	{
		// Everything freezes: the pause menu, or the room waiting for a phone (pauseLifecycle.ts).
		return;
	}
	const double Dt = FMath::Min(static_cast<double>(DeltaSeconds), MaxStepsPerFrame * FKartSim::StepSeconds);
	PhaseSeconds += Dt;

	switch (Phase)
	{
	case EKartRacePhase::Waiting:
		if (PhaseSeconds >= WaitingSeconds)
		{
			SetPhase(bSkipIntro ? EKartRacePhase::Countdown : EKartRacePhase::Intro);
		}
		break;
	case EKartRacePhase::Intro:
		if (PhaseSeconds >= IntroSeconds)
		{
			SetPhase(EKartRacePhase::Countdown);
		}
		break;
	case EKartRacePhase::Countdown:
		UpdateCountdown();
		break;
	case EKartRacePhase::Racing:
	case EKartRacePhase::Finished:
		AdvanceSim(Dt);
		break;
	}

	UpdateCameras(DeltaSeconds);
	UpdateOccluders();
}

void AKartRaceManager::UpdateCountdown()
{
	if (GetCountdownValue() != LastCountdownCue)
	{
		LastCountdownCue = GetCountdownValue();
		OnCue.Broadcast(-1, TEXT("race.countdown"));
		if (UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this))
		{
			Mixer->PlayCountdown(LastCountdownCue);
		}
	}
	// Rocket start: when the gas went down (MultiplayerKart.tsx:850-898). Releasing clears it.
	for (FKartRacer& Racer : Racers)
	{
		if (Racer.Kind == EKartSeatKind::Bot)
		{
			const double Eager = PhaseSeconds > CountdownSeconds - BotRevLead ? 0.6 + 0.4 * FMath::Sin(PhaseSeconds * 5.0 + Racer.Seat * 1.7) : 0.0;
			Racer.Kart->SetGridRev(static_cast<float>(Eager));
			continue;
		}
		const FKartInput Input = GatherInput(Racer, false);
		Racer.Kart->SetGridRev(Input.bForward ? 1.f : 0.f);
		if (!Input.bForward)
		{
			Racer.GasHeldSince = -1.0;
		}
		else if (Racer.GasHeldSince < 0.0)
		{
			Racer.GasHeldSince = PhaseSeconds;
		}
	}
	if (PhaseSeconds >= CountdownSeconds)
	{
		BeginRacing();
	}
}

void AKartRaceManager::BeginRacing()
{
	for (FKartRacer& Racer : Racers)
	{
		Racer.Bot.Reset();
		Racer.Kart->SetGridRev(0.f);
		FDriftBoost Start;
		if (Racer.GasHeldSince >= 0.0 && RocketStartBoost((CountdownSeconds - Racer.GasHeldSince) * 1000.0, Start))
		{
			Racer.Kart->GetKartMovement()->MutableSim().ApplyBoost(Start.Multiplier, Start.Time, Start.Label);
			OnCue.Broadcast(Racer.Seat, Start.Multiplier >= 1.0 ? FName(TEXT("kart.boost")) : FName(TEXT("kart.bogged")));
		}
		Racer.GasHeldSince = -1.0;
	}
	RaceSteps = 0;
	Accumulator = 0.0;
	SetPhase(EKartRacePhase::Racing);
	for (FKartRacer& Racer : Racers)
	{
		Racer.Kart->PlayRaceStart();
	}
	OnCue.Broadcast(-1, TEXT("race.go"));
	if (UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this))
	{
		Mixer->PlayCountdown(0);
	}
}

void AKartRaceManager::AdvanceSim(double Dt)
{
	constexpr double Step = FKartSim::StepSeconds;
	Accumulator += Dt;
	int32 Steps = 0;
	while (Accumulator >= Step && Steps < MaxStepsPerFrame)
	{
		StepRace();
		Accumulator -= Step;
		++Steps;
	}
	if (Steps == MaxStepsPerFrame)
	{
		Accumulator = FMath::Min(Accumulator, Step);
	}
	// Every kart is drawn the same fraction of a step past its last one.
	const float Alpha = static_cast<float>(FMath::Clamp(Accumulator / Step, 0.0, 1.0));
	for (FKartRacer& Racer : Racers)
	{
		Racer.Kart->GetKartMovement()->Present(Alpha);
	}
}

bool AKartRaceManager::IsBotDriven(const FKartRacer& Racer) const
{
	return Racer.Kind == EKartSeatKind::Bot || (Racer.Kind == EKartSeatKind::Local && bAutoDriveLocal);
}

FKartInput AKartRaceManager::GatherInput(FKartRacer& Racer, bool bRacing)
{
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const double Now = RaceSteps * FKartSim::StepSeconds;
	if (Racer.bFinished || IsBotDriven(Racer))
	{
		// Finished karts cruise on the bot's line so they keep clear of the racers. Empty-handed racers may
		// detour through an item box they can collect.
		TArray<FVector2D, TInlineAllocator<16>> Boxes;
		const FKartItemSlot& Slot = Items.Slots()[Racer.Seat];
		if (!Racer.bFinished && Slot.Held == EKartItem::None && !Slot.bRolling)
		{
			for (const FKartItemBox& Box : Items.Boxes())
			{
				if (Box.CollectedAt.IsValidIndex(Racer.Seat) && Box.CollectedAt[Racer.Seat] < 0.0)
				{
					Boxes.Add(Box.Pos);
				}
			}
		}
		return Racer.Bot.Update(S, Track->Waypoints, bRacing, Now, Boxes);
	}
	if (Racer.Kind == EKartSeatKind::Local)
	{
		const AKartPlayerController* PC = Cast<AKartPlayerController>(Racer.Viewer);
		return PC ? PC->GetLocalInput() : FKartInput();
	}
	FKartInput Input;
	if (ExternalInput && ExternalInput(Racer.Seat, Input))
	{
		return Input;
	}
	return FKartInput();
}

FKartRaceContext AKartRaceManager::MakeContext(const FKartRacer& Racer) const
{
	return MakeRaceContext(StepStandings, Racer.Seat, Phase == EKartRacePhase::Racing, bKartContact);
}

void AKartRaceManager::StepRace()
{
	++RaceSteps;
	const double RaceMs = GetRaceMs();
	UpdateStandings();

	// Every kart sees the others where they were at the start of this step.
	TArray<FKartRaceContext>& Contexts = StepContexts;
	Contexts.Reset();
	for (const FKartRacer& Racer : Racers)
	{
		Contexts.Add(MakeContext(Racer));
	}
	const double ItemTime = GetItemTime();
	TArray<FKartItemRacer>& ItemRacers = StepItemRacers;
	ItemRacers.Reset();
	for (int32 i = 0; i < Racers.Num(); ++i)
	{
		FKartRacer& Racer = Racers[i];
		FKartInput Input = GatherInput(Racer, true);
		const bool bUsePressed = Input.bUseItem && !Racer.bUseItemHeld;
		Racer.bUseItemHeld = Input.bUseItem;
		if (Items.IsRocket(i, ItemTime))
		{
			Input = RocketAutopilot(Racer);
		}
		else if (Items.IsInputLocked(i, ItemTime))
		{
			Input = FKartInput();
		}
		TraceInput(Input);
		Racer.Kart->GetKartMovement()->StepOnce(Input, Contexts[i]);
		EmitStepEvents(Racer);

		FKartItemRacer& ItemRacer = ItemRacers.AddDefaulted_GetRef();
		const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		ItemRacer.Sim = &Racer.Kart->GetKartMovement()->MutableSim();
		ItemRacer.Laps = S.LapsCompleted;
		ItemRacer.Progress = S.ProgressPercent;
		ItemRacer.bFinished = Racer.bFinished;
		ItemRacer.bUseItem = bUsePressed;
		ItemRacer.bBot = Racer.bFinished || IsBotDriven(Racer);
	}
	TArray<FKartItemEvent>& ItemEvents = StepItemEvents;
	ItemEvents.Reset();
	Items.Step(ItemRacers, ItemTime, ItemEvents);
	DispatchItemEvents(ItemEvents);
	UpdateAudioStatus(ItemTime);
	RecordLaps(RaceMs);
	TraceStep();

	if (Phase == EKartRacePhase::Racing)
	{
		const bool bAllFinished = !Racers.ContainsByPredicate([](const FKartRacer& Racer) { return !Racer.bFinished; });
		if (bAllFinished || RaceMs >= GetDeadlineMs())
		{
			EndRace();
		}
	}
}

void AKartRaceManager::TraceInput(const FKartInput& Input)
{
	if (!bStepTrace)
	{
		return;
	}
	const uint8 Buttons = (Input.bLeft ? 1 : 0) | (Input.bRight ? 2 : 0) | (Input.bForward ? 4 : 0) | (Input.bBackward ? 8 : 0)
		| (Input.bJump ? 16 : 0) | (Input.bUseItem ? 32 : 0);
	TraceInputs = FCrc::MemCrc32(&Input.SteeringX, sizeof(Input.SteeringX), TraceInputs);
	TraceInputs = FCrc::MemCrc32(&Buttons, sizeof(Buttons), TraceInputs);
}

void AKartRaceManager::TraceStep() const
{
	if (!bStepTrace || RaceSteps % static_cast<int64>(StepsPerSecond) != 0)
	{
		return;
	}
	uint32 State = 0;
	for (const FKartRacer& Racer : Racers)
	{
		const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		const double Values[] = { S.Pos.X, S.Pos.Y, S.Yaw, S.Speed };
		State = FCrc::MemCrc32(Values, sizeof(Values), State);
	}
	UE_LOG(LogKartRace, Log, TEXT("KartStepTrace step %lld state %08x inputs %08x"), RaceSteps, State, TraceInputs);
}

void AKartRaceManager::RecordLaps(double RaceMs)
{
	for (FKartRacer& Racer : Racers)
	{
		const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		while (S.LapsCompleted > Racer.LapsSeen)
		{
			++Racer.LapsSeen;
			const double Lap = RaceMs - Racer.LapStartMs;
			Racer.LapMs.Add(Lap);
			Racer.BestLapMs = Racer.BestLapMs > 0.0 ? FMath::Min(Racer.BestLapMs, Lap) : Lap;
			Racer.LapStartMs = RaceMs;
			if (Racer.LapsSeen >= Track->Laps && !Racer.bFinished)
			{
				Racer.bFinished = true;
				Racer.FinishMs = RaceMs;
				Racer.Kart->SetFinishPlace(Algo::CountIf(Racers, [](const FKartRacer& R) { return R.bFinished; }));
				if (LeaderFinishMs <= 0.0)
				{
					LeaderFinishMs = RaceMs;
				}
				OnCue.Broadcast(Racer.Seat, TEXT("kart.finish"));
			}
			else if (!Racer.bFinished)
			{
				OnCue.Broadcast(Racer.Seat, Racer.LapsSeen == Track->Laps - 1 ? FName(TEXT("kart.final-lap")) : FName(TEXT("kart.lap")));
			}
		}
	}
}

FKartInput AKartRaceManager::RocketAutopilot(const FKartRacer& Racer) const
{
	// Full gas, aimed down the line, homing on the nearest kart ahead.
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const TArray<FVector2D>& Path = Track->Waypoints;
	const FVector2D Target = Path[(Track->NearestWaypoint(S.Pos) + RocketLookAheadWaypoints) % Path.Num()];
	auto AngleTo = [&S](const FVector2D& P)
	{
		const FVector2D D = P - S.Pos;
		return FMath::FindDeltaAngleRadians(S.Yaw, FMath::Atan2(-D.X, -D.Y));
	};
	double Steer = FMath::Clamp(AngleTo(Target) * RocketPathGain, -1.0, 1.0);
	double Nearest = RocketRivalRange;
	const FKartRacer* Rival = nullptr;
	for (const FKartRacer& Other : Racers)
	{
		const double D = FVector2D::Distance(Other.Kart->GetKartMovement()->SimState().Pos, S.Pos);
		if (Other.Place < Racer.Place && D < Nearest)
		{
			Nearest = D;
			Rival = &Other;
		}
	}
	if (Rival)
	{
		const double Blend = FMath::Max(0.0, 1.0 - Nearest / RocketRivalRange);
		Steer = Steer * (1.0 - Blend) + FMath::Clamp(AngleTo(Rival->Kart->GetKartMovement()->SimState().Pos) * RocketRivalGain, -1.0, 1.0) * Blend;
	}
	FKartInput Input;
	Input.bForward = true;
	Input.SteeringX = Steer;
	return Input;
}

void AKartRaceManager::UpdateStandings()
{
	StepStandings.Reset();
	for (const FKartRacer& Racer : Racers)
	{
		const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		StepStandings.Add({ S.Pos, S.LapsCompleted, S.ProgressPercent, Racer.bFinished, Racer.FinishMs });
	}
	RankRacers(StepStandings, Standings);
	for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
	{
		Racers[Seat].Place = StepStandings[Seat].Place;
	}
}

void AKartRaceManager::EndRace()
{
	UpdateStandings();
	Results.Reset();
	// GAMES-01/02: finishers by ascending time, then DNFs by progress; a DNF never gets a time.
	for (const int32 Seat : Standings)
	{
		const FKartRacer& Racer = Racers[Seat];
		FKartResult& Result = Results.AddDefaulted_GetRef();
		Result.Seat = Seat;
		Result.Rank = Results.Num();
		Result.Name = Racer.Name;
		Result.Paint = Racer.Paint;
		Result.bFinished = Racer.bFinished;
		Result.TimeMs = Racer.bFinished ? FMath::RoundToDouble(Racer.FinishMs) : 0.0;
		Result.BestLapMs = Racer.BestLapMs;
		Result.ProgressScore = ProgressScore(Racer);
	}
	SetPhase(EKartRacePhase::Finished);
	// Store: free unlocks count finished races and wins by players (not bots).
	const bool bPlayersRaced = Racers.ContainsByPredicate([](const FKartRacer& R) { return R.Kind != EKartSeatKind::Bot; });
	const bool bPlayerWon = Results.Num() > 0 && Racers[Results[0].Seat].Kind != EKartSeatKind::Bot && Racers[Results[0].Seat].bFinished;
	UKartStoreSubsystem::NoteRaceFinished(this, bPlayersRaced, bPlayerWon);
	OnResults.Broadcast(Results);
	OnCue.Broadcast(-1, TEXT("race.results"));
}
