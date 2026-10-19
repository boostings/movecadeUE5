#include "KartGameMode.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "KartHUD.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "KartPlayerController.h"
#include "Misc/CommandLine.h"
#include "Race/KartRaceManager.h"
#include "TimerManager.h"
#include "Misc/PackageName.h"
#include "Net/KartLobbyWidget.h"
#include "Net/RoomHostSubsystem.h"
#include "TrackBuilder.h"

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
	const TCHAR* BotNames[] = { TEXT("Dante"), TEXT("Priya"), TEXT("Mika"), TEXT("Rosa") };
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
		for (TActorIterator<ATrackBuilder> It(GetWorld()); It; ++It)
		{
			TrackBuilder = *It;
			break;
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
	SetupTestHooks();

	const TCHAR* Cmd = FCommandLine::Get();
	bRoomMode = FParse::Param(Cmd, TEXT("KartRoom")) || FParse::Param(Cmd, TEXT("KartLobby"));
	if (bRoomMode)
	{
		SetupRoom();
		return;
	}

	TSharedPtr<const MovecadeKart::FTrack> Track = GetTrackBuilder() ? GetTrackBuilder()->GetTrack() : nullptr;
	if (!Track.IsValid())
	{
		UE_LOG(LogKartGameMode, Error, TEXT("No track loaded; the race cannot start."));
		return;
	}
	SetupLocalRace(Track);
}

namespace
{
	TSharedPtr<const MovecadeKart::FTrack> WithLapOverride(TSharedPtr<const MovecadeKart::FTrack> Track)
	{
		int32 Laps = 0;
		if (Track.IsValid() && FParse::Value(FCommandLine::Get(), TEXT("KartLaps="), Laps) && Laps > 0)
		{
			TSharedPtr<MovecadeKart::FTrack> Shorter = MakeShared<MovecadeKart::FTrack>(*Track);
			Shorter->Laps = Laps;
			return Shorter;
		}
		return Track;
	}
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
		Viewers.Add(UGameplayStatics::CreatePlayer(this, i, true));
	}
	return Viewers;
}

void AKartGameMode::SetupLocalRace(TSharedPtr<const MovecadeKart::FTrack> Track)
{
	Track = WithLapOverride(Track);
	const TCHAR* Cmd = FCommandLine::Get();
	int32 Bots = DefaultBots;
	int32 LocalPlayers = 1;
	FParse::Value(Cmd, TEXT("KartBots="), Bots);
	FParse::Value(Cmd, TEXT("KartLocalPlayers="), LocalPlayers);
	LocalPlayers = FMath::Clamp(LocalPlayers, 1, Track->Grid.Num());
	Bots = FMath::Clamp(Bots, 0, Track->Grid.Num() - LocalPlayers);

	TArray<FKartSeatSpec> Seats;
	for (int32 i = 0; i < LocalPlayers + Bots; ++i)
	{
		FKartSeatSpec& Seat = Seats.AddDefaulted_GetRef();
		Seat.Kind = i < LocalPlayers ? EKartSeatKind::Local : EKartSeatKind::Bot;
		Seat.Name = i < LocalPlayers ? FString::Printf(TEXT("Player %d"), i + 1) : BotNames[(i - LocalPlayers) % UE_ARRAY_COUNT(BotNames)];
		Seat.Paint = SeatPaint[i % UE_ARRAY_COUNT(SeatPaint)];
	}

	RaceManager = GetWorld()->SpawnActor<AKartRaceManager>();
	RaceManager->bAutoDriveLocal = FParse::Param(Cmd, TEXT("KartAutoDrive"));
	RaceManager->bSkipIntro = FParse::Param(Cmd, TEXT("KartSkipIntro"));
	RaceManager->bLegacyWalls = FParse::Param(Cmd, TEXT("KartLegacyWalls"));
	RaceManager->Setup(Track, Seats, MakeViewers(Seats.Num(), LocalPlayers, FParse::Param(Cmd, TEXT("KartSoloView"))));
	bRaceBuilt = true;
}

void AKartGameMode::SetupRoom()
{
	URoomHostSubsystem* Room = GetGameInstance()->GetSubsystem<URoomHostSubsystem>();
	if (!Room)
	{
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
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		UKartLobbyWidget::Show(PC);
	}
}

void AKartGameMode::HandleRoomStart(const FString& MapId)
{
	if (bRaceBuilt)
	{
		return;
	}
	ATrackBuilder* Builder = GetTrackBuilder();
	if (Builder && !MapId.IsEmpty() && Builder->TrackId != MapId)
	{
		// The other track lives in its own level. The room subsystem survives the travel.
		const FString Level = MapId == TEXT("violet-loop") ? TEXT("/Game/Maps/L_VioletLoop") : TEXT("/Game/Maps/L_SunsetHarbor");
		if (FPackageName::DoesPackageExist(Level))
		{
			UGameplayStatics::OpenLevel(this, FName(*Level));
			return;
		}
		UE_LOG(LogKartGameMode, Warning, TEXT("No level for track %s yet; racing on %s."), *MapId, *Builder->TrackId);
	}
	TSharedPtr<const MovecadeKart::FTrack> Track = Builder ? Builder->GetTrack() : nullptr;
	if (Track.IsValid())
	{
		BuildRoomRace(WithLapOverride(Track));
	}
}

void AKartGameMode::BuildRoomRace(TSharedPtr<const MovecadeKart::FTrack> Track)
{
	URoomHostSubsystem* Room = GetGameInstance()->GetSubsystem<URoomHostSubsystem>();
	const TCHAR* Cmd = FCommandLine::Get();
	int32 Bots = 0;
	FParse::Value(Cmd, TEXT("KartBots="), Bots);

	TArray<FKartSeatSpec> Seats;
	RoomSeatOf.Reset();
	RoomPlayerOf.Reset();
	for (const FMcSeat& Player : Room->GetSeats())
	{
		if (Player.bEliminated || Seats.Num() >= Track->Grid.Num())
		{
			continue;
		}
		FKartSeatSpec& Seat = Seats.AddDefaulted_GetRef();
		Seat.Kind = EKartSeatKind::Phone;
		Seat.Name = Player.Name;
		Seat.Paint = Player.PaintColor;
		RoomSeatOf.Add(Player.SeatIndex());
		RoomPlayerOf.Add(Player.PlayerId);
	}
	for (int32 i = 0; i < Bots && Seats.Num() < Track->Grid.Num(); ++i)
	{
		FKartSeatSpec& Seat = Seats.AddDefaulted_GetRef();
		Seat.Kind = EKartSeatKind::Bot;
		Seat.Name = BotNames[i % UE_ARRAY_COUNT(BotNames)];
		Seat.Paint = SeatPaint[Seats.Num() % UE_ARRAY_COUNT(SeatPaint)];
		RoomSeatOf.Add(INDEX_NONE);
		RoomPlayerOf.Add(FString());
	}
	if (Seats.Num() == 0)
	{
		UE_LOG(LogKartGameMode, Warning, TEXT("The room started with nobody seated."));
		return;
	}

	RaceManager = GetWorld()->SpawnActor<AKartRaceManager>();
	RaceManager->bSkipIntro = FParse::Param(Cmd, TEXT("KartSkipIntro"));
	RaceManager->bLegacyWalls = FParse::Param(Cmd, TEXT("KartLegacyWalls"));
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	RaceManager->ExternalInput = [this, WeakRoom](int32 RaceSeat, MovecadeKart::FKartInput& Out)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		const int32 RoomSeat = RoomSeatOf.IsValidIndex(RaceSeat) ? RoomSeatOf[RaceSeat] : INDEX_NONE;
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
		URoomHostSubsystem* R = WeakRoom.Get();
		if (!R)
		{
			return;
		}
		switch (Phase)
		{
		case EKartRacePhase::Intro: R->PublishRacePhase(EMcRacePhase::Intro); break;
		case EKartRacePhase::Countdown: R->PublishRacePhase(EMcRacePhase::Countdown); break;
		case EKartRacePhase::Racing: R->PublishRacePhase(EMcRacePhase::Racing); break;
		case EKartRacePhase::Finished: R->PublishRacePhase(EMcRacePhase::Finished); break;
		default: break;
		}
	});
	RaceManager->OnHaptic.AddLambda([this, WeakRoom](int32 RaceSeat, float Intensity, FName Type, bool bSustained)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		if (R && RoomSeatOf.IsValidIndex(RaceSeat) && RoomSeatOf[RaceSeat] != INDEX_NONE)
		{
			R->SendHaptic(RoomSeatOf[RaceSeat], Intensity, Type, bSustained);
		}
	});
	RaceManager->OnResults.AddLambda([this, WeakRoom](const TArray<FKartResult>& Results)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		if (!R)
		{
			return;
		}
		GetWorldTimerManager().ClearTimer(LiveScoreTimer);
		SendLiveScores();
		TArray<FMcRaceResult> Out;
		for (const FKartResult& Result : Results)
		{
			if (!RoomSeatOf.IsValidIndex(Result.Seat) || RoomSeatOf[Result.Seat] == INDEX_NONE)
			{
				continue; // bots are never scored
			}
			FMcRaceResult& Row = Out.AddDefaulted_GetRef();
			Row.SeatIndex = RoomSeatOf[Result.Seat];
			Row.PlayerId = RoomPlayerOf[Result.Seat];
			Row.bFinished = Result.bFinished;
			Row.FinishTimeMs = Result.TimeMs;
		}
		R->SubmitResults(Out);
	});
	RaceManager->Setup(Track, Seats, MakeViewers(Seats.Num(), 1, FParse::Param(Cmd, TEXT("KartSoloView"))));
	GetWorldTimerManager().SetTimer(LiveScoreTimer, this, &AKartGameMode::SendLiveScores, 1.0f, true);
	bRaceBuilt = true;
}

void AKartGameMode::SendLiveScores()
{
	// legacyBridge.ts:165-188: laps * 100 + (finished ? max(0, 1000 - place * 100) : floor(lap %)), always < 10000.
	URoomHostSubsystem* Room = GetGameInstance()->GetSubsystem<URoomHostSubsystem>();
	if (!Room || !RaceManager || RaceManager->GetPhase() == EKartRacePhase::Waiting || RaceManager->GetPhase() == EKartRacePhase::Intro)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	for (const FKartRacer& Racer : RaceManager->GetRacers())
	{
		if (!RoomPlayerOf.IsValidIndex(Racer.Seat) || RoomPlayerOf[Racer.Seat].IsEmpty())
		{
			continue;
		}
		const MovecadeKart::FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = RoomPlayerOf[Racer.Seat];
		Entry.Score = S.LapsCompleted * 100 + (Racer.bFinished ? FMath::Max(0, 1000 - Racer.Place * 100) : FMath::Clamp(FMath::FloorToInt32(S.ProgressPercent), 0, 99));
	}
	Room->UpdateLiveScores(Scores);
}

void AKartGameMode::HandleRoomLobby()
{
	if (bRaceBuilt)
	{
		// Back to the lobby: reload the level for a clean grid; the lobby shows on StartPlay.
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

void AKartGameMode::SetupTestHooks()
{
	FString Shots;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartShots="), Shots, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Times;
		Shots.ParseIntoArray(Times, TEXT(","));
		for (const FString& Time : Times)
		{
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, [this]()
			{
				// Saved/Screenshots/<platform>/ScreenShotNNNNN.png, with the UI
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				{
					PC->ConsoleCommand(TEXT("Shot showui"));
				}
			}, FCString::Atof(*Time), false);
		}
	}
	float QuitAfter = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartQuitAfter="), QuitAfter) && QuitAfter > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, [this]()
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}, QuitAfter, false);
	}
}
