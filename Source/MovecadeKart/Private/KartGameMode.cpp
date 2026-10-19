#include "KartGameMode.h"

#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "KartCoords.h"
#include "KartHUD.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "KartPlayerController.h"
#include "Net/KartLobbyWidget.h"
#include "TrackBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartGameMode, Log, All);

AKartGameMode::AKartGameMode()
{
	DefaultPawnClass = AKartPawn::StaticClass();
	PlayerControllerClass = AKartPlayerController::StaticClass();
	HUDClass = AKartHUD::StaticClass();
}

void AKartGameMode::StartPlay()
{
	Super::StartPlay();

	FString Shots;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartShots="), Shots, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Times;
		Shots.ParseIntoArray(Times, TEXT(","));
		for (const FString& Time : Times)
		{
			const float Seconds = FCString::Atof(*Time);
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, [this]()
			{
				// Saved/Screenshots/<platform>/HighresScreenshotNNNNN.png
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				{
					PC->ConsoleCommand(TEXT("HighResShot 1"));
				}
			}, Seconds, false);
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("KartLobby")))
	{
		// Phone lobby (join code, QR, seats); it opens the room. The race layer will call Show() itself.
		UKartLobbyWidget::Show(GetWorld()->GetFirstPlayerController());
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

void AKartGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}
	ATrackBuilder* Builder = GetTrackBuilder();
	TSharedPtr<const MovecadeKart::FTrack> Track = Builder ? Builder->GetTrack() : nullptr;
	if (!Track.IsValid())
	{
		UE_LOG(LogKartGameMode, Error, TEXT("No track loaded; the kart cannot spawn."));
		return;
	}

	const int32 Slot = NextGridSlot++ % Track->Grid.Num();
	const MovecadeKart::FSpawnSlot& Spawn = Track->Grid[Slot];
	const FTransform Transform(FRotator(0.0, MovecadeKart::Coords::YawToUE(Spawn.Yaw), 0.0), MovecadeKart::Coords::ToUE(Spawn.Position));

	if (APawn* Old = NewPlayer->GetPawn())
	{
		Old->Destroy();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AKartPawn* Kart = GetWorld()->SpawnActor<AKartPawn>(DefaultPawnClass, Transform, Params);
	if (!Kart)
	{
		UE_LOG(LogKartGameMode, Error, TEXT("Kart failed to spawn."));
		return;
	}
	Kart->GetKartMovement()->InitSim(Track, Slot);
	NewPlayer->Possess(Kart);
	Kart->SnapCamera();
}
