#include "Race/KartRaceManager.h"

#include "Algo/Count.h"

#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "Kismet/KismetSystemLibrary.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "KartPlayerController.h"
#include "Audio/KartAudioComponent.h"
#include "Audio/KartAudioSubsystem.h"
#include "Race/KartItemPresenter.h"
#include "Race/SKartRaceOverlay.h"

using namespace MovecadeKart;

namespace
{
	// spec 04 §5.2, legacy store.ts RACE_CONFIG
	constexpr double WaitingSeconds = 1.0;
	constexpr double IntroSeconds = 6.0;
	constexpr double CountdownSeconds = 5.0;
	constexpr double IntroShotSeconds = 1.2;
	constexpr int32 IntroShots = 5;
	constexpr double MaxRaceMs = 300000.0;
	constexpr double FinishGraceMs = 30000.0;
	constexpr int32 MaxStepsPerFrame = 4;

	// Occluders: a kart is hidden from a viewport when it sits between that viewport's camera and its own kart.
	constexpr double OccluderCentreHeight = 80.0;    // cm above the kart's origin: the body, not the wheels
	constexpr double OccluderMaxAlong = 0.92;        // only karts in front of the own kart along the sight line
	constexpr double OccluderRadius = 220.0;         // cm from the sight line

	/** The web's item ids, which the audio observer keys item-spend sounds on. */
	FName WebItemId(EKartItem Item)
	{
		switch (Item)
		{
		case EKartItem::Shot: return TEXT("GREEN_SHELL");
		case EKartItem::Seeker: return TEXT("RED_SHELL");
		case EKartItem::SkySeeker: return TEXT("BLUE_SHELL");
		case EKartItem::Banana: return TEXT("BANANA");
		case EKartItem::Nitro: return TEXT("MUSHROOM");
		case EKartItem::TripleNitro: return TEXT("TRIPLE_MUSHROOM");
		case EKartItem::Prism: return TEXT("STAR");
		case EKartItem::Lightning: return TEXT("LIGHTNING");
		case EKartItem::Ink: return TEXT("BLOOPER");
		case EKartItem::Bomb: return TEXT("BOB_OMB");
		case EKartItem::Rocket: return TEXT("BULLET_BILL");
		default: return NAME_None;
		}
	}

	EKartAudioPhase AudioPhase(EKartRacePhase Phase)
	{
		switch (Phase)
		{
		case EKartRacePhase::Countdown: return EKartAudioPhase::Countdown;
		case EKartRacePhase::Racing: return EKartAudioPhase::Racing;
		case EKartRacePhase::Finished: return EKartAudioPhase::Results;
		default: return EKartAudioPhase::Waiting;
		}
	}

	double HorizontalFov(double VerticalDegrees, double Aspect)
	{
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalDegrees) * 0.5) * Aspect));
	}
}

AKartRaceManager::AKartRaceManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
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
		FKartLook Look = Seats[Seat].bHasLook ? Seats[Seat].Look : FKartLook::MakeVaried(Seat);
		Look.Paint = Seats[Seat].Paint;
		Look.Name = Seats[Seat].Name;
		Kart->SetLook(Look);
		Kart->GetKartMovement()->SetExternallyStepped(true);
		Kart->GetKartMovement()->MutableSim().bLegacyStickyWalls = bLegacyWalls;
		Kart->GetKartMovement()->InitSim(Track, Seat);

		FKartRacer& Racer = Racers.AddDefaulted_GetRef();
		Racer.Seat = Seat;
		Racer.Name = Seats[Seat].Name;
		Racer.Kind = Seats[Seat].Kind;
		Racer.Paint = Seats[Seat].Paint;
		switch (Seats[Seat].BotSkill)
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

	// Intro shots: the track's static cameras (store.ts: 5 shots x 1.2 s).
	for (ACameraActor* Camera : IntroCameras)
	{
		if (Camera)
		{
			Camera->Destroy();
		}
	}
	IntroCameras.Reset();
	for (int32 i = 0; i < FMath::Min(IntroShots, Track->StaticCameras.Num()); ++i)
	{
		const FStaticCamera& Shot = Track->StaticCameras[i];
		const FVector Eye = Coords::ToUE(Shot.Position + FVector(0.0, Track->SuggestedRebaseY, 0.0));
		const FVector Look = Coords::ToUE(Shot.LookAt + FVector(0.0, Track->SuggestedRebaseY, 0.0));
		ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Eye, (Look - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(HorizontalFov(60.0, 16.0 / 9.0));
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		IntroCameras.Add(Camera);
	}
	if (!ResultsCamera)
	{
		ResultsCamera = GetWorld()->SpawnActor<ACameraActor>();
		ResultsCamera->GetCameraComponent()->SetFieldOfView(HorizontalFov(64.0, 16.0 / 9.0));
		ResultsCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}

	Items.Init(Track.Get(), Track->ItemBoxes, Racers.Num(), 1234);
	if (!ItemPresenter)
	{
		ItemPresenter = GetWorld()->SpawnActor<AKartItemPresenter>();
	}
	ItemPresenter->Bind(this);

	RestartRace();
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
	Items.Reset(1234 + RaceNumber);
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
	return Phase == EKartRacePhase::Countdown ? FMath::Clamp(5 - FMath::FloorToInt32(PhaseSeconds), 1, 5) : 0;
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

	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	const bool bFullScreenShot = NewPhase == EKartRacePhase::Intro || NewPhase == EKartRacePhase::Finished;
	if (Viewport)
	{
		Viewport->SetForceDisableSplitscreen(bFullScreenShot);
	}
	if (NewPhase == EKartRacePhase::Finished && ResultsCamera)
	{
		ResultsCameraPos = FVector::ZeroVector;
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			// A cut under a quick white flash: blending between two moving cameras flies through the scenery.
			PC->SetViewTarget(ResultsCamera);
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraFade(0.85f, 0.f, 0.55f, FLinearColor(1.f, 0.97f, 0.92f), false, false);
			}
		}
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
			Racer.Kart->PlayCountdownSwoop(bSkipIntro ? WaitingSeconds + 2.4 : 2.4);
		}
	}
	for (FKartRacer& Racer : Racers)
	{
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			Audio->SetRacePhase(AudioPhase(NewPhase));
		}
	}
	OnPhaseChanged.Broadcast(NewPhase);
}

void AKartRaceManager::UseRaceViews()
{
	for (FKartRacer& Racer : Racers)
	{
		if (Racer.Viewer && Racer.Viewer->GetViewTarget() != Racer.Kart)
		{
			// A cut: the countdown opens on the crane shot (AKartPawn::PlayCountdownSwoop), which carries the motion.
			Racer.Viewer->SetViewTarget(Racer.Kart);
		}
	}
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
	// Up to MaxStepsPerFrame steps of real time per frame: a busy GPU (down to 15 fps) keeps race time, and only a
	// long hitch drops time instead of spiralling.
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
				// Bots rev up for the last two counts, in their own rhythm.
				const double Eager = PhaseSeconds > CountdownSeconds - 2.0 ? 0.6 + 0.4 * FMath::Sin(PhaseSeconds * 5.0 + Racer.Seat * 1.7) : 0.0;
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
		break;
	}

	case EKartRacePhase::Racing:
	case EKartRacePhase::Finished:
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
		const float Alpha = static_cast<float>(FMath::Clamp(Accumulator / Step, 0.0, 1.0));
		for (FKartRacer& Racer : Racers)
		{
			Racer.Kart->GetKartMovement()->Present(Alpha);
		}
		break;
	}
	}

	UpdateCameras(DeltaSeconds);
	UpdateOccluders();
}

void AKartRaceManager::UpdateOccluders()
{
	// A kart bunched between a viewport's chase camera and its own kart blocks the view (the web has the
	// same problem). Hide such karts for that viewport only.
	for (FKartRacer& Racer : Racers)
	{
		APlayerController* PC = Racer.Viewer;
		if (!PC || !PC->PlayerCameraManager)
		{
			continue;
		}
		// Only un-hide the karts this function hid, so other systems can hide actors too.
		for (FKartRacer& Other : Racers)
		{
			PC->HiddenActors.Remove(Other.Kart);
		}
		if (PC->GetViewTarget() != Racer.Kart)
		{
			continue;
		}
		const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
		const FVector Own = Racer.Kart->GetActorLocation();
		const FVector Seg = Own - Eye;
		const double SegLenSq = Seg.SizeSquared();
		if (SegLenSq < 1.0)
		{
			continue;
		}
		for (FKartRacer& Other : Racers)
		{
			if (&Other == &Racer)
			{
				continue;
			}
			const FVector P = Other.Kart->GetActorLocation() + FVector(0.0, 0.0, OccluderCentreHeight);
			const double T = FVector::DotProduct(P - Eye, Seg) / SegLenSq;
			if (T > 0.0 && T < OccluderMaxAlong && FVector::Dist(P, Eye + Seg * T) < OccluderRadius)
			{
				PC->HiddenActors.Add(Other.Kart);
			}
		}
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

FKartInput AKartRaceManager::GatherInput(FKartRacer& Racer, bool bRacing)
{
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const double Now = RaceSteps * FKartSim::StepSeconds;
	if (Racer.bFinished || Racer.Kind == EKartSeatKind::Bot || (Racer.Kind == EKartSeatKind::Local && bAutoDriveLocal))
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
		if (const AKartPlayerController* PC = Cast<AKartPlayerController>(Racer.Viewer))
		{
			return PC->GetLocalInput();
		}
		return FKartInput();
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
		Racer.Kart->GetKartMovement()->StepOnce(Input, Contexts[i]);
		EmitStepEvents(Racer);

		FKartItemRacer& ItemRacer = ItemRacers.AddDefaulted_GetRef();
		const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		ItemRacer.Sim = &Racer.Kart->GetKartMovement()->MutableSim();
		ItemRacer.Laps = S.LapsCompleted;
		ItemRacer.Progress = S.ProgressPercent;
		ItemRacer.bFinished = Racer.bFinished;
		ItemRacer.bUseItem = bUsePressed;
		ItemRacer.bBot = Racer.bFinished || Racer.Kind == EKartSeatKind::Bot || (Racer.Kind == EKartSeatKind::Local && bAutoDriveLocal);
	}
	TArray<FKartItemEvent>& ItemEvents = StepItemEvents;
	ItemEvents.Reset();
	Items.Step(ItemRacers, ItemTime, ItemEvents);
	DispatchItemEvents(ItemEvents);
	for (FKartRacer& Racer : Racers)
	{
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			const FKartItemSlot& Slot = Items.Slots()[Racer.Seat];
			MovecadeKart::Audio::FKartAudioStatus Status;
			Status.bRollingItem = Slot.bRolling;
			Status.bHasItem = Slot.Held != EKartItem::None;
			Status.HeldItem = WebItemId(Slot.Held);
			Status.bInvincible = Items.IsInvincible(Racer.Seat, ItemTime);
			Status.bBulletBill = Items.IsRocket(Racer.Seat, ItemTime);
			Status.bShrunk = ItemTime < Slot.ShrinkUntil;
			Status.bInk = ItemTime < Slot.InkUntil;
			Audio->SetStatus(Status);
		}
	}

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

	if (Phase == EKartRacePhase::Racing)
	{
		const bool bAllFinished = !Racers.ContainsByPredicate([](const FKartRacer& Racer) { return !Racer.bFinished; });
		if (bAllFinished || RaceMs >= GetDeadlineMs())
		{
			EndRace();
		}
	}
}

void AKartRaceManager::EmitStepEvents(FKartRacer& Racer)
{
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	if (S.BarrierEvent > 0.0)
	{
		OnHaptic.Broadcast(Racer.Seat, static_cast<float>(S.BarrierEvent), TEXT("barrier"), S.bBarrierEventSustained);
	}
	if (S.BumpEvent > 0.0)
	{
		OnHaptic.Broadcast(Racer.Seat, static_cast<float>(S.BumpEvent), TEXT("kart"), false);
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			Audio->PlayImpact(static_cast<float>(S.BumpEvent));
		}
	}
	// Off-road rumble: 0.075 every 200 ms while moving (MultiplayerKart.tsx:1552-1568).
	if (S.bOnDirt && FMath::Abs(S.Speed) > 5.0 && S.Time - Racer.LastTerrainRumble >= 0.2)
	{
		Racer.LastTerrainRumble = S.Time;
		OnHaptic.Broadcast(Racer.Seat, 0.075f, TEXT("terrain"), true);
	}
}

FKartInput AKartRaceManager::RocketAutopilot(const FKartRacer& Racer) const
{
	// MultiplayerKart.tsx:996-1042: full gas, aim at waypoint nearest + 3, gain 2.5, and blend toward the
	// nearest kart ahead within 30 m (gain 3).
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const TArray<FVector2D>& Path = Track->Waypoints;
	const FVector2D Target = Path[(Track->NearestWaypoint(S.Pos) + 3) % Path.Num()];
	auto AngleTo = [&S](const FVector2D& P)
	{
		const FVector2D D = P - S.Pos;
		return FMath::FindDeltaAngleRadians(S.Yaw, FMath::Atan2(-D.X, -D.Y));
	};
	double Steer = FMath::Clamp(AngleTo(Target) * 2.5, -1.0, 1.0);
	double Nearest = 30.0;
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
		const double Blend = FMath::Max(0.0, 1.0 - Nearest / 30.0);
		Steer = Steer * (1.0 - Blend) + FMath::Clamp(AngleTo(Rival->Kart->GetKartMovement()->SimState().Pos) * 3.0, -1.0, 1.0) * Blend;
	}
	FKartInput Input;
	Input.bForward = true;
	Input.SteeringX = Steer;
	return Input;
}

void AKartRaceManager::DispatchItemEvents(const TArray<FKartItemEvent>& Events)
{
	// Cue names follow app/src/audio/kartCues.ts.
	for (const FKartItemEvent& E : Events)
	{
		switch (E.Type)
		{
		case EKartItemEventType::Pickup: OnCue.Broadcast(E.Seat, TEXT("kart.item-pickup")); break;
		case EKartItemEventType::Ready: OnCue.Broadcast(E.Seat, TEXT("kart.item-ready")); break;
		case EKartItemEventType::Used:
			if (Racers.IsValidIndex(E.Seat))
			{
				Racers[E.Seat].Kart->PlayItemThrow();
			}
			break;
		case EKartItemEventType::Launch:
			OnCue.Broadcast(E.Seat, TEXT("kart.projectile-launch"));
			if (E.Item == EKartItem::Seeker || E.Item == EKartItem::SkySeeker)
			{
				OnCue.Broadcast(-1, TEXT("kart.warning"));
			}
			break;
		case EKartItemEventType::Drop: OnCue.Broadcast(E.Seat, TEXT("kart.hazard-drop")); break;
		case EKartItemEventType::Bounce: OnCue.Broadcast(E.Other, TEXT("kart.projectile-bounce")); break;
		case EKartItemEventType::Hit: OnCue.Broadcast(E.Seat, TEXT("kart.projectile-hit")); break;
		case EKartItemEventType::SpinOut:
			OnCue.Broadcast(E.Seat, TEXT("kart.spinout"));
			OnHaptic.Broadcast(E.Seat, 0.8f, TEXT("item"), false);
			break;
		case EKartItemEventType::Explosion:
			OnCue.Broadcast(-1, TEXT("kart.explosion"));
			for (const FKartRacer& Racer : Racers)
			{
				if (FVector2D::Distance(Racer.Kart->GetKartMovement()->SimState().Pos, E.Position) < E.Radius)
				{
					OnHaptic.Broadcast(Racer.Seat, 1.0f, TEXT("item"), false);
				}
			}
			break;
		case EKartItemEventType::Lightning: OnCue.Broadcast(E.Seat, TEXT("kart.lightning")); break;
		case EKartItemEventType::Inked: OnCue.Broadcast(E.Seat, TEXT("kart.ink")); break;
		case EKartItemEventType::Invincible: OnCue.Broadcast(E.Seat, TEXT("kart.invincible-start")); break;
		case EKartItemEventType::RocketStart: OnCue.Broadcast(E.Seat, TEXT("kart.boost")); break;
		case EKartItemEventType::Bump: OnHaptic.Broadcast(E.Seat, 0.6f, TEXT("kart"), false); break;
		default: break;
		}
	}
	// Sounds the audio observer can't derive from state: projectiles, hits, explosions, the shell warning.
	UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this);
	auto KartCue = [this](int32 Seat, const TCHAR* Cue)
	{
		if (Racers.IsValidIndex(Seat))
		{
			if (UKartAudioComponent* Audio = Racers[Seat].Kart->GetKartAudio())
			{
				Audio->PlayCue(Cue);
			}
		}
	};
	for (const FKartItemEvent& E : Events)
	{
		switch (E.Type)
		{
		case EKartItemEventType::Launch:
			KartCue(E.Seat, TEXT("kart.projectile-launch"));
			if (Mixer && (E.Item == EKartItem::Seeker || E.Item == EKartItem::SkySeeker))
			{
				Mixer->PlayCue(TEXT("kart.warning"), 0.6f);
			}
			break;
		case EKartItemEventType::Drop: KartCue(E.Seat, TEXT("kart.hazard-drop")); break;
		case EKartItemEventType::Bounce: KartCue(E.Other, TEXT("kart.projectile-bounce")); break;
		case EKartItemEventType::Hit: KartCue(E.Seat, TEXT("kart.projectile-hit")); break;
		case EKartItemEventType::SpinOut: KartCue(E.Seat, TEXT("kart.spinout")); break;
		case EKartItemEventType::Explosion:
			if (Mixer)
			{
				Mixer->PlayCue(TEXT("kart.explosion"));
			}
			break;
		case EKartItemEventType::Bump:
			if (Racers.IsValidIndex(E.Seat) && Racers[E.Seat].Kart->GetKartAudio())
			{
				Racers[E.Seat].Kart->GetKartAudio()->PlayImpact(0.8f);
			}
			break;
		default:
			break;
		}
	}
	PendingItemEvents.Append(Events);
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
	OnResults.Broadcast(Results);
	OnCue.Broadcast(-1, TEXT("race.results"));
}

void AKartRaceManager::UpdateCameras(float DeltaSeconds)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	if (Phase == EKartRacePhase::Intro && IntroCameras.Num() > 0)
	{
		const int32 Shot = FMath::Min(FMath::FloorToInt32(PhaseSeconds / IntroShotSeconds), IntroCameras.Num() - 1);
		if (Shot != IntroShot)
		{
			IntroShot = Shot;
			PC->SetViewTargetWithBlend(IntroCameras[Shot], Shot == 0 ? 0.f : 0.8f, VTBlend_Cubic);
		}
		// Each shot is a slow dolly (a push in, and a crab that alternates side per shot) instead of a still,
		// like a broadcast flyover. Every camera keeps moving through the blend into the next one.
		for (int32 i = 0; i < IntroCameras.Num(); ++i)
		{
			const FStaticCamera& Base = Track->StaticCameras[i];
			const FVector Rebase(0.0, Track->SuggestedRebaseY, 0.0);
			const FVector Eye = Coords::ToUE(Base.Position + Rebase);
			const FVector Look = Coords::ToUE(Base.LookAt + Rebase);
			const FVector Toward = (Look - Eye).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward).GetSafeNormal() * (i % 2 == 0 ? 1.0 : -1.0);
			const double T = PhaseSeconds - i * IntroShotSeconds + 0.4;
			const FVector Moved = Eye + (Toward * 70.0 + Side * 40.0) * T;
			IntroCameras[i]->SetActorLocationAndRotation(Moved, (Look + Side * 20.0 * T - Moved).Rotation());
		}
	}
	else if (Phase == EKartRacePhase::Finished && ResultsCamera && Results.Num() > 0)
	{
		// Results shot: a low, slow orbit round the winner (the web sat high at (6.4, 4.6, 8.4)), close enough
		// to see the driver celebrate.
		const AKartPawn* Winner = Racers[Results[0].Seat].Kart;
		const FVector2D P = Winner->GetKartMovement()->RenderPosition();
		const double Orbit = FMath::Atan2(8.4, 6.4) + 0.18 * PhaseSeconds;
		constexpr double Radius = 6.5;
		const FVector Target = Coords::ToUE(FVector(P.X + Radius * FMath::Cos(Orbit), 2.1, P.Y + Radius * FMath::Sin(Orbit)));
		const FVector Look = Coords::ToUE(P, 1.2);
		const double Alpha = 1.0 - FMath::Exp(-4.6 * FMath::Min(static_cast<double>(DeltaSeconds), 1.0 / 30.0));
		ResultsCameraPos = ResultsCameraPos.IsZero() ? Target : FMath::Lerp(ResultsCameraPos, Target, Alpha);
		// The standings card fills the left of the screen: turn the view so the winner sits on the right third.
		FRotator View = (Look - ResultsCameraPos).Rotation();
		View.Yaw -= 22.0;
		ResultsCamera->SetActorLocationAndRotation(ResultsCameraPos, View);
	}
}
