#include "Games/Stacker/StackerGameMode.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Stacker/StackerGame.h"
#include "Games/Stacker/StackerRooftop.h"
#include "Games/ArcadeSession.h"
#include "Games/SArcadeOverlay.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "TimerManager.h"

using namespace MovecadeKart::Stacker;

namespace
{
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };

	URoomHostSubsystem* RoomOf(const UObject* Context)
	{
		const UGameInstance* GI = Context && Context->GetWorld() ? Context->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}
}

AStackerGameMode::AStackerGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void AStackerGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<AStackerRooftop> It(World); It; ++It)
	{
		Rooftop = *It;
		break;
	}
	if (!Rooftop)
	{
		Rooftop = World->SpawnActor<AStackerRooftop>();
	}
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 1);
	FParse::Value(FCommandLine::Get(), TEXT("StackerPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("StackerBots="), Bots);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("StackerAuto"));
	// One local builder: a simultaneous game needs a pair of hands each.
	Locals = FMath::Clamp(Locals, 0, 1);
	Bots = FMath::Clamp(Bots, 0, 4 - Locals);
	if (Locals + Bots == 0)
	{
		Bots = 1;
	}
	TArray<FStackerSeat> Seats;
	bRoomMatch = SetupRoom(Seats);
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
	{
		FStackerSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? EBuilderKind::Local : EBuilderKind::Bot;
		Seat.Skill = bLocal ? 0.9f : 0.5f + 0.1f * (i % 3);
		Seat.Color = AStackerRooftop::LaneColor(i);
		Seat.Look = FKartLook::MakeVaried(i + 8);
	}
	Game = World->SpawnActor<AStackerGame>();
	Game->Setup(Rooftop, Seats, World->GetFirstPlayerController());
	Game->OnQuit = [this]() { UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor")); };
	if (bRoomMatch)
	{
		BindRoom();
	}
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	if (!bRoomMatch)
	{
		// Play again and next game keep these seats.
		if (UArcadeSession* Session = UArcadeSession::Get(this))
		{
			Session->NoteLaunch(TEXT("stacker"), Locals, Bots);
		}
	}
	Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch);
	SetupTestHooks();
}

bool AStackerGameMode::SetupRoom(TArray<FStackerSeat>& OutSeats)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("stacker")))
	{
		return false;
	}
	TArray<FMcSeat> Players = Room->GetSeats();
	Players.Sort([](const FMcSeat& A, const FMcSeat& B) { return A.Seat < B.Seat; });
	for (const FMcSeat& Player : Players)
	{
		if (Player.bEliminated || OutSeats.Num() >= 4)
		{
			continue;
		}
		FStackerSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EBuilderKind::Bot : EBuilderKind::Phone;
		Seat.Skill = 0.65f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	UE_LOG(LogTemp, Display, TEXT("Stacker: room match %d with %d builder(s)"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void AStackerGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &AStackerGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &AStackerGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);
	TWeakObjectPtr<AStackerGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		AStackerGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});
	// runtime.ts 465-477: DROP has no payload and resolves at receipt (the cooldown de-dups repeats).
	GameEventHandle = Room->OnGameEvent.AddLambda([WeakGame](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		if (AStackerGame* G = WeakGame.Get(); G && Event == TEXT("stacker:drop"))
		{
			G->Drop(From);
		}
	});
	Game->OnMatchStart = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(FString());
			R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		}
	};
	// runtime.ts 540-549: a toast per drop and topple.
	Game->OnEffect = [this, WeakRoom](const FEffect& E)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		const FTower* Tower = Game ? Game->GetMatch().FindTower(E.PlayerId) : nullptr;
		if (!R || !Tower || E.Label.IsEmpty())
		{
			return;
		}
		const bool bBad = E.Kind == EEffectKind::Miss || E.Kind == EEffectKind::Topout;
		const bool bBig = E.Kind == EEffectKind::Complete || (E.Kind == EEffectKind::Perfect && Tower->Combo >= 3);
		const FString Detail = FString::Printf(TEXT("+%d points · %d lives · %d blocks"), FMath::Max(0, E.ScoreDelta), Tower->Lives, Tower->Height());
		R->SendPlayFeedback(E.PlayerId, E.Label, Detail, bBad ? TEXT("warning") : TEXT("success"),
			bBad ? TEXT("warning") : (E.Kind == EEffectKind::Perfect || E.Kind == EEffectKind::Complete ? TEXT("celebration") : TEXT("impact")), bBig);
	};
	Game->OnFinished = [this]() { PushScores(true); };
	Game->OnRematch = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->RestartGame();
		}
	};
	Game->OnQuit = Game->OnRematch;
	GetWorldTimerManager().SetTimer(ScoreTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PushScores(false); }), 1.0f, true);
}

void AStackerGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	FString Key;
	for (const FTower& T : Game->GetMatch().Towers)
	{
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = T.PlayerId;
		Entry.Score = T.Score;
		Key += FString::Printf(TEXT("%s=%d;"), *T.PlayerId, T.Score);
	}
	if (bFinal)
	{
		Room->CompleteGame(Scores);
	}
	else if (Key != LastScores)
	{
		LastScores = Key;
		Room->UpdateLiveScores(Scores);
	}
}

void AStackerGameMode::HandleRoomLobby()
{
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void AStackerGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void AStackerGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Overlay.IsValid())
	{
		Overlay->Detach();
		Overlay.Reset();
	}
	// The world's timers live on the game instance and outlast the level.
	GetWorldTimerManager().ClearTimer(ScoreTimer);
	if (URoomHostSubsystem* Room = RoomOf(this))
	{
		Room->OnGameEvent.Remove(GameEventHandle);
		Room->OnSensorFrame.Remove(SensorHandle);
		Room->OnReturnedToLobby.RemoveDynamic(this, &AStackerGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &AStackerGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
}

void AStackerGameMode::SetupTestHooks()
{
	FString Shots;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartShots="), Shots, false))
	{
		TArray<FString> Times;
		Shots.ParseIntoArray(Times, TEXT(","));
		for (const FString& Time : Times)
		{
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				{
					PC->ConsoleCommand(TEXT("Shot showui"));
				}
			}), FCString::Atof(*Time), false);
		}
	}
	float QuitAfter = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartQuitAfter="), QuitAfter) && QuitAfter > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), QuitAfter, false);
	}
}
