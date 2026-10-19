#include "KartGameMode.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "KartHUD.h"
#include "KartPawn.h"
#include "KartPlayerController.h"
#include "Misc/CommandLine.h"
#include "Race/KartRaceManager.h"
#include "TimerManager.h"
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

	TSharedPtr<const MovecadeKart::FTrack> Track = GetTrackBuilder() ? GetTrackBuilder()->GetTrack() : nullptr;
	if (!Track.IsValid())
	{
		UE_LOG(LogKartGameMode, Error, TEXT("No track loaded; the race cannot start."));
		return;
	}

	const TCHAR* Cmd = FCommandLine::Get();
	int32 LapsOverride = 0;
	if (FParse::Value(Cmd, TEXT("KartLaps="), LapsOverride) && LapsOverride > 0)
	{
		TSharedPtr<MovecadeKart::FTrack> Shorter = MakeShared<MovecadeKart::FTrack>(*Track);
		Shorter->Laps = LapsOverride;
		Track = Shorter;
	}
	int32 Bots = DefaultBots;
	int32 LocalPlayers = 1;
	FParse::Value(Cmd, TEXT("KartBots="), Bots);
	FParse::Value(Cmd, TEXT("KartLocalPlayers="), LocalPlayers);
	LocalPlayers = FMath::Clamp(LocalPlayers, 1, Track->Grid.Num());
	Bots = FMath::Clamp(Bots, 0, Track->Grid.Num() - LocalPlayers);
	const bool bSoloView = FParse::Param(Cmd, TEXT("KartSoloView"));

	TArray<FKartSeatSpec> Seats;
	for (int32 i = 0; i < LocalPlayers + Bots; ++i)
	{
		FKartSeatSpec& Seat = Seats.AddDefaulted_GetRef();
		Seat.Kind = i < LocalPlayers ? EKartSeatKind::Local : EKartSeatKind::Bot;
		Seat.Name = i < LocalPlayers ? FString::Printf(TEXT("Player %d"), i + 1) : BotNames[(i - LocalPlayers) % UE_ARRAY_COUNT(BotNames)];
		Seat.Paint = SeatPaint[i % UE_ARRAY_COUNT(SeatPaint)];
	}

	// One viewport per kart (SplitScreenRenderer), unless -KartSoloView. Local players beyond the
	// first get their own controller id, so a second gamepad drives the second seat.
	TArray<APlayerController*> Viewers;
	Viewers.Add(GetWorld()->GetFirstPlayerController());
	const int32 Views = bSoloView ? LocalPlayers : Seats.Num();
	for (int32 i = 1; i < Views; ++i)
	{
		Viewers.Add(UGameplayStatics::CreatePlayer(this, i, true));
	}

	RaceManager = GetWorld()->SpawnActor<AKartRaceManager>();
	RaceManager->bAutoDriveLocal = FParse::Param(Cmd, TEXT("KartAutoDrive"));
	RaceManager->bSkipIntro = FParse::Param(Cmd, TEXT("KartSkipIntro"));
	RaceManager->bLegacyWalls = FParse::Param(Cmd, TEXT("KartLegacyWalls"));
	RaceManager->Setup(Track, Seats, Viewers);
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
