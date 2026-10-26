#include "KartGameMode.h"

#include "EngineUtils.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeTestHooks.h"
#include "Input/DeviceControlsSubsystem.h"
#include "Input/KartDeviceControls.h"
#include "Race/SKartRaceOverlay.h"
#include "Kismet/GameplayStatics.h"
#include "KartHUD.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "KartPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Net/KartLobbyWidget.h"
#include "Net/RoomHostSubsystem.h"
#include "Race/KartRaceManager.h"
#include "TimerManager.h"
#include "TrackBuilder.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartGameMode, Log, All);

namespace
{
	/** v2 paint palette (games/kart/kartPaint.ts): pink, cyan, gold, violet. */
	const FLinearColor SeatPaint[] = {
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("f0508e"))),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("5ec8e5"))),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("f7c948"))),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("a67cf0"))),
	};
	const TCHAR* const BotNames[] = { TEXT("Dante"), TEXT("Priya"), TEXT("Mika"), TEXT("Rosa") };

	/** legacyBridge.ts: live scores once a second. */
	constexpr float LiveScoreIntervalSeconds = 1.f;

	/** -KartLaps=N: a shorter race for tests (not in Shipping; FKartRaceFlags). */
	TSharedPtr<const MovecadeKart::FTrack> WithLapOverride(TSharedPtr<const MovecadeKart::FTrack> Track)
	{
		const TOptional<int32> Laps = FKartRaceFlags::Parse(FCommandLine::Get()).Laps;
		if (Track.IsValid() && Laps.IsSet())
		{
			TSharedPtr<MovecadeKart::FTrack> Shorter = MakeShared<MovecadeKart::FTrack>(*Track);
			Shorter->Laps = Laps.GetValue();
			return Shorter;
		}
		return Track;
	}

	/**
	 * legacyBridge.ts:165-188: laps * 100 + (finished ? max(0, 1000 - place * 100) : floor(lap %)), always < 10000.
	 */
	int32 LiveRaceScore(const MovecadeKart::FKartState& State, bool bFinished, int32 Place)
	{
		return State.LapsCompleted * 100 + (bFinished ? FMath::Max(0, 1000 - Place * 100) : FMath::Clamp(FMath::FloorToInt32(State.ProgressPercent), 0, 99));
	}

	EMcRacePhase ToRoomPhase(EKartRacePhase Phase, bool& bOutKnown)
	{
		bOutKnown = true;
		switch (Phase)
		{
		case EKartRacePhase::Intro: return EMcRacePhase::Intro;
		case EKartRacePhase::Countdown: return EMcRacePhase::Countdown;
		case EKartRacePhase::Racing: return EMcRacePhase::Racing;
		case EKartRacePhase::Finished: return EMcRacePhase::Finished;
		default: bOutKnown = false; return EMcRacePhase::Intro;
		}
	}
}

AKartGameMode::AKartGameMode()
{
	DefaultPawnClass = AKartPawn::StaticClass();
	PlayerControllerClass = AKartPlayerController::StaticClass();
	HUDClass = AKartHUD::StaticClass();
}

ATrackBuilder* AKartGameMode::GetTrackBuilder()
{
	if (!TrackBuilder)
	{
		if (TActorIterator<ATrackBuilder> It{ GetWorld() })
		{
			TrackBuilder = *It;
		}
	}
	if (!TrackBuilder)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		TrackBuilder = GetWorld()->SpawnActor<ATrackBuilder>(ATrackBuilder::StaticClass(), FTransform::Identity, Params);
		TrackBuilder->TrackId = DefaultTrackId;
		TrackBuilder->FinishSpawning(FTransform::Identity);
	}
	return TrackBuilder;
}

void AKartGameMode::StartPlay()
{
	Super::StartPlay();
	FArcadeTestHooks::Parse(FCommandLine::Get()).Schedule(*this, TestHookTimers);

	// Menus, a local race or the phone room (UI/KartSessionSettings): the test flags skip the menus.
	UKartSessionSettings* Session = UKartSessionSettings::Get(this);
	const EKartBoot Boot = Session ? Session->DecideBoot(FCommandLine::Get())
		: KartFrontEndFlags::DecideBoot(EKartSessionMode::None, true, FCommandLine::Get());
	bRoomMode = Boot == EKartBoot::Room;
	if (bRoomMode)
	{
		SetupRoom();
		return;
	}

	const TSharedPtr<const MovecadeKart::FTrack> Track = GetTrackBuilder() ? GetTrackBuilder()->GetTrack() : nullptr;
	if (!Track.IsValid())
	{
		UE_LOG(LogKartGameMode, Error, TEXT("No track loaded; the race cannot start."));
		return;
	}
	if (Boot == EKartBoot::FrontEnd)
	{
		AKartFrontEnd::Open(GetWorld(), Track);
		return;
	}
	SetupLocalRace(Track);
}

void AKartGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// The world's timer manager outlives level travel in practice: clear what this mode armed.
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(LiveScoreTimer);
	for (FTimerHandle& Handle : TestHookTimers)
	{
		Timers.ClearTimer(Handle);
	}
	TestHookTimers.Reset();
	if (UDeviceControlsSubsystem* Device = UDeviceControlsSubsystem::Get(this); Device && DeviceSeat.IsValid())
	{
		Device->Deactivate();
	}
	DeviceSeat.Reset();
	StopListeningToRoom();
	Super::EndPlay(Reason);
}

URoomHostSubsystem* AKartGameMode::GetRoom() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<URoomHostSubsystem>() : nullptr;
}

TArray<APlayerController*> AKartGameMode::MakeViewers(int32 Karts, int32 LocalPlayers, bool bSoloView)
{
	// One viewport per kart (SplitScreenRenderer), unless -KartSoloView. Local players beyond the
	// first get their own controller id, so a second gamepad drives the second seat.
	TArray<APlayerController*> Viewers;
	Viewers.Add(GetWorld()->GetFirstPlayerController());
	const int32 Views = bSoloView ? FMath::Max(1, LocalPlayers) : Karts;
	for (int32 i = 1; i < Views; ++i)
	{
		// Local players survive level travel (the room reloads the level), so reuse one that is already there.
		APlayerController* Existing = UGameplayStatics::GetPlayerControllerFromID(this, i);
		Viewers.Add(Existing ? Existing : UGameplayStatics::CreatePlayer(this, i, true));
	}
	return Viewers;
}

void AKartGameMode::SetupLocalRace(TSharedPtr<const MovecadeKart::FTrack> Track)
{
	// The roster, laps and switches: the menu's setup when the race came from the menus, then the
	// command-line flags on top (-KartBots, -KartLocalPlayers, -KartLaps, -KartBotSkill, -KartSoloView, ...).
	const UKartSessionSettings* Session = UKartSessionSettings::Get(this);
	const FKartRacePlan Plan = FKartRacePlan::Resolve(Session ? Session->GetMenuSetup() : nullptr, FCommandLine::Get(), Track->Grid.Num(), DefaultBots);
	if (Plan.Laps > 0 && Plan.Laps != Track->Laps)
	{
		TSharedPtr<MovecadeKart::FTrack> WithLaps = MakeShared<MovecadeKart::FTrack>(*Track);
		WithLaps->Laps = Plan.Laps;
		Track = WithLaps;
	}

	RaceManager = GetWorld()->SpawnActor<AKartRaceManager>();
	RaceManager->bAutoDriveLocal = Plan.bAutoDrive;
	RaceManager->bSkipIntro = Plan.bSkipIntro;
	RaceManager->bLegacyWalls = Plan.bLegacyWalls;
	RaceManager->Setup(Track, Plan.Seats, MakeViewers(Plan.Seats.Num(), Plan.LocalPlayers, Plan.bSoloView));
	bRaceBuilt = true;
	if (Plan.LocalPlayers > 0 && !Plan.bAutoDrive && UDeviceControlsSubsystem::IsEnabled())
	{
		ActivateDeviceControls();
	}
}

void AKartGameMode::ActivateDeviceControls()
{
	UDeviceControlsSubsystem* Device = UDeviceControlsSubsystem::Get(this);
	if (!Device)
	{
		return;
	}
	// The first local seat is the device; its kart-input and frames land in DeviceSeat, which its controller reads.
	DeviceSeat = MakeShared<MovecadeKart::Input::FKartDeviceSeat>();
	DeviceSeat->SetSensitivity(Device->GetSensitivity());
	Device->Activate(MakeShared<MovecadeKart::Input::FKartDeviceControls>(), DeviceSeat.ToSharedRef(),
		[]() { const TSharedPtr<SKartRaceOverlay> Race = SKartRaceOverlay::GetActive(); return Race.IsValid() && Race->AreMenusOpen(); },
		[]() { if (const TSharedPtr<SKartRaceOverlay> Race = SKartRaceOverlay::GetActive()) { Race->OpenPauseMenu(); } });
}

// ───────────────────────────── Party Mode ─────────────────────────────

void AKartGameMode::SetupRoom()
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room)
	{
		UE_LOG(LogKartGameMode, Error, TEXT("Party Mode without a room host subsystem."));
		return;
	}
	Room->OnStartRequested.AddDynamic(this, &AKartGameMode::HandleRoomStart);
	Room->OnReturnedToLobby.AddDynamic(this, &AKartGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &AKartGameMode::HandleRoomPause);
	Room->SetPreparationReady(true);
	Room->OpenRoom();

	// Arriving here by level travel after the room started the match: build the race at once.
	const EMcRoomStatus Status = Room->GetRoom().Status;
	if (Status == EMcRoomStatus::InProgress || Status == EMcRoomStatus::Paused)
	{
		HandleRoomStart(Room->GetRoom().SelectedMapId);
		return;
	}
	// A finished match left the room on its results: bring everyone back to the lobby, or nothing could start.
	if (Status == EMcRoomStatus::Complete && Room->IsHost())
	{
		Room->ReturnToLobby();
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		UKartLobbyWidget::Show(PC);
	}
}

void AKartGameMode::StopListeningToRoom()
{
	// Leaving this level: the room subsystem outlives this mode, and a state message that lands before teardown
	// must not reach it (it would travel a second time).
	if (URoomHostSubsystem* Room = GetRoom())
	{
		Room->OnStartRequested.RemoveAll(this);
		Room->OnReturnedToLobby.RemoveAll(this);
		Room->OnPauseChanged.RemoveAll(this);
	}
}

void AKartGameMode::HandleRoomStart(const FString& MapId)
{
	if (bRaceBuilt)
	{
		return;
	}
	// A party game plays in its own level; the room (a game instance subsystem) survives the travel.
	if (const URoomHostSubsystem* Room = GetRoom(); Room && Room->GetRoom().GameId != TEXT("kart"))
	{
		if (const MovecadeKart::Arcade::FGameInfo* Game = MovecadeKart::Arcade::Find(Room->GetRoom().GameId))
		{
			UGameplayStatics::OpenLevel(this, FName(*Game->Map));
			return;
		}
	}
	ATrackBuilder* Builder = GetTrackBuilder();
	if (Builder && !MapId.IsEmpty() && Builder->TrackId != MapId)
	{
		// The other track lives in its own level (UI/KartTrackCatalog). The room subsystem survives the travel.
		const FString Level = FKartTrackCatalog::LevelFor(MapId);
		if (FPackageName::DoesPackageExist(Level))
		{
			StopListeningToRoom();
			UGameplayStatics::OpenLevel(this, FName(*Level));
			return;
		}
		UE_LOG(LogKartGameMode, Warning, TEXT("No level for track %s yet; racing on %s."), *MapId, *Builder->TrackId);
	}
	const TSharedPtr<const MovecadeKart::FTrack> Track = Builder ? Builder->GetTrack() : nullptr;
	if (Track.IsValid())
	{
		BuildRoomRace(WithLapOverride(Track));
	}
}

TArray<FKartSeatSpec> AKartGameMode::BuildRoomSeats(const URoomHostSubsystem& Room, int32 GridSize, int32 Bots)
{
	TArray<FKartSeatSpec> Seats;
	RoomSeatOf.Reset();
	RoomPlayerOf.Reset();
	for (const FMcSeat& Player : Room.GetSeats())
	{
		if (Player.bEliminated || Seats.Num() >= GridSize)
		{
			continue;
		}
		FKartSeatSpec& Seat = Seats.AddDefaulted_GetRef();
		Seat.Kind = EKartSeatKind::Phone;
		Seat.Name = Player.Name;
		Seat.Paint = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Seats.Num() - 1);
		Seat.bHasLook = true;
		RoomSeatOf.Add(Player.SeatIndex());
		RoomPlayerOf.Add(Player.PlayerId);
	}
	for (int32 i = 0; i < Bots && Seats.Num() < GridSize; ++i)
	{
		FKartSeatSpec& Seat = Seats.AddDefaulted_GetRef();
		Seat.Kind = EKartSeatKind::Bot;
		Seat.Name = BotNames[i % UE_ARRAY_COUNT(BotNames)];
		Seat.Paint = SeatPaint[Seats.Num() % UE_ARRAY_COUNT(SeatPaint)];
		RoomSeatOf.Add(INDEX_NONE);
		RoomPlayerOf.Add(FString());
	}
	return Seats;
}

void AKartGameMode::BuildRoomRace(TSharedPtr<const MovecadeKart::FTrack> Track)
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room)
	{
		return;
	}
	const FKartRaceFlags Flags = FKartRaceFlags::Parse(FCommandLine::Get());
	const TArray<FKartSeatSpec> Seats = BuildRoomSeats(*Room, Track->Grid.Num(), Flags.Bots.Get(0));
	if (Seats.Num() == 0)
	{
		UE_LOG(LogKartGameMode, Warning, TEXT("The room started with nobody seated."));
		return;
	}

	RaceManager = GetWorld()->SpawnActor<AKartRaceManager>();
	RaceManager->bSkipIntro = Flags.bSkipIntro;
	RaceManager->bLegacyWalls = Flags.bLegacyWalls;
	BindRaceToRoom(*Room);
	RaceManager->Setup(Track, Seats, MakeViewers(Seats.Num(), 1, Flags.bSoloView));
	GetWorldTimerManager().SetTimer(LiveScoreTimer, this, &AKartGameMode::SendLiveScores, LiveScoreIntervalSeconds, /*bLoop*/ true);
	bRaceBuilt = true;
}

void AKartGameMode::BindRaceToRoom(URoomHostSubsystem& Room)
{
	const TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	const TWeakObjectPtr<AKartGameMode> WeakThis = this;
	RaceManager->ExternalInput = [WeakThis, WeakRoom](int32 RaceSeat, MovecadeKart::FKartInput& Out)
	{
		const AKartGameMode* Mode = WeakThis.Get();
		URoomHostSubsystem* R = WeakRoom.Get();
		const int32 RoomSeat = Mode ? Mode->RoomSeatFor(RaceSeat) : INDEX_NONE;
		if (!R || RoomSeat == INDEX_NONE || !R->GetSeatInput(RoomSeat, Out))
		{
			return false;
		}
		// One press per ITEM tap; the race layer turns held into an edge.
		Out.bUseItem = R->ConsumeUseItem(RoomSeat);
		return true;
	};
	RaceManager->OnRestartRequested = [WeakRoom]()
	{
		// "Play again" sends the room back to the lobby; everyone re-readies (spec 04 §5.3).
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->RestartGame();
		}
	};
	RaceManager->OnPhaseChanged.AddLambda([WeakRoom](EKartRacePhase Phase)
	{
		bool bKnown = false;
		const EMcRacePhase RoomPhase = ToRoomPhase(Phase, bKnown);
		if (URoomHostSubsystem* R = WeakRoom.Get(); R && bKnown)
		{
			R->PublishRacePhase(RoomPhase);
		}
	});
	RaceManager->OnHaptic.AddLambda([WeakThis, WeakRoom](int32 RaceSeat, float Intensity, FName Type, bool bSustained)
	{
		const AKartGameMode* Mode = WeakThis.Get();
		const int32 RoomSeat = Mode ? Mode->RoomSeatFor(RaceSeat) : INDEX_NONE;
		if (URoomHostSubsystem* R = WeakRoom.Get(); R && RoomSeat != INDEX_NONE)
		{
			R->SendHaptic(RoomSeat, Intensity, Type, bSustained);
		}
	});
	RaceManager->OnResults.AddLambda([WeakThis](const TArray<FKartResult>& Results)
	{
		if (AKartGameMode* Mode = WeakThis.Get())
		{
			Mode->SubmitRoomResults(Results);
		}
	});
}

int32 AKartGameMode::RoomSeatFor(int32 RaceSeat) const
{
	return RoomSeatOf.IsValidIndex(RaceSeat) ? RoomSeatOf[RaceSeat] : INDEX_NONE;
}

void AKartGameMode::SubmitRoomResults(const TArray<FKartResult>& Results)
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(LiveScoreTimer);
	SendLiveScores();
	TArray<FMcRaceResult> Out;
	for (const FKartResult& Result : Results)
	{
		const int32 RoomSeat = RoomSeatFor(Result.Seat);
		if (RoomSeat == INDEX_NONE)
		{
			continue; // bots are never scored
		}
		FMcRaceResult& Row = Out.AddDefaulted_GetRef();
		Row.SeatIndex = RoomSeat;
		Row.PlayerId = RoomPlayerOf[Result.Seat];
		Row.bFinished = Result.bFinished;
		Row.FinishTimeMs = Result.TimeMs;
	}
	Room->SubmitResults(Out);
}

void AKartGameMode::SendLiveScores()
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room || !RaceManager || RaceManager->GetPhase() == EKartRacePhase::Waiting || RaceManager->GetPhase() == EKartRacePhase::Intro)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	for (const FKartRacer& Racer : RaceManager->GetRacers())
	{
		if (!Racer.Kart || !RoomPlayerOf.IsValidIndex(Racer.Seat) || RoomPlayerOf[Racer.Seat].IsEmpty())
		{
			continue;
		}
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = RoomPlayerOf[Racer.Seat];
		Entry.Score = LiveRaceScore(Racer.Kart->GetKartMovement()->SimState(), Racer.bFinished, Racer.Place);
	}
	Room->UpdateLiveScores(Scores);
}

void AKartGameMode::HandleRoomLobby()
{
	if (bRaceBuilt)
	{
		// Back to the lobby: reload the level for a clean grid; the lobby shows on StartPlay.
		StopListeningToRoom();
		UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
	}
}

void AKartGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (RaceManager)
	{
		RaceManager->SetPaused(bPaused, Reason);
	}
}
