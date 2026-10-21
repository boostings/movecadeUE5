#include "Games/Bowling/BowlingGameMode.h"

#include "Art/KartMaterials.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Games/Bowling/BowlingGame.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"

namespace
{
	/** The web's lane colours, one per seat. */
	const TCHAR* const SeatColors[] = { TEXT("#6d8bff"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Mika"), TEXT("Dante"), TEXT("Priya"), TEXT("Rosa") };
}

ABowlingGameMode::ABowlingGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ABowlingGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ABowlingAlley> It(World); It; ++It)
	{
		Alley = *It;
		break;
	}
	if (!Alley)
	{
		Alley = World->SpawnActor<ABowlingAlley>();
	}

	int32 Locals = 1, Bots = 1;
	FParse::Value(FCommandLine::Get(), TEXT("BowlPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("BowlBots="), Bots);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("BowlAuto"));
	Locals = FMath::Clamp(Locals, 0, 4);
	Bots = FMath::Clamp(Bots, 0, 4 - Locals);
	if (Locals + Bots == 0)
	{
		Bots = 1;
	}
	TArray<FBowlerSeat> Seats;
	for (int32 i = 0; i < Locals + Bots; ++i)
	{
		FBowlerSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? EBowlerKind::Local : EBowlerKind::Bot;
		Seat.Skill = bLocal ? 0.85f : 0.55f + 0.1f * (i % 3);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i);
	}
	Game = World->SpawnActor<ABowlingGame>();
	Game->Setup(Alley, Seats, World->GetFirstPlayerController());
	// Back to the front end, which lives on the default level.
	Game->OnQuit = [this]() { UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor")); };
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	SetupTestHooks();
}

void ABowlingGameMode::SetupTestHooks()
{
	FString Shots;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartShots="), Shots, false))
	{
		TArray<FString> Times;
		Shots.ParseIntoArray(Times, TEXT(","));
		for (const FString& Time : Times)
		{
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, [this]()
			{
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
