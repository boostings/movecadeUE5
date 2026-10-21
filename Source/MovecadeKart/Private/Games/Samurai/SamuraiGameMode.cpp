#include "Games/Samurai/SamuraiGameMode.h"

#include "Art/KartMaterials.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Samurai/SamuraiDojo.h"
#include "Games/Samurai/SamuraiGame.h"
#include "Games/ArcadeSession.h"
#include "Games/SArcadeOverlay.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "TimerManager.h"

using namespace MovecadeKart::Samurai;

namespace
{
	const TCHAR* const SeatColors[] = { TEXT("#f0506e"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };

	URoomHostSubsystem* RoomOf(const UObject* Context)
	{
		const UGameInstance* GI = Context && Context->GetWorld() ? Context->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}
}

ASamuraiGameMode::ASamuraiGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ASamuraiGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ASamuraiDojo> It(World); It; ++It)
	{
		Dojo = *It;
		break;
	}
	if (!Dojo)
	{
		Dojo = World->SpawnActor<ASamuraiDojo>();
	}
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 1);
	FParse::Value(FCommandLine::Get(), TEXT("SamuraiPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("SamuraiBots="), Bots);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("SamuraiAuto"));
	// One local swordsman: a simultaneous game needs a pair of hands each.
	Locals = FMath::Clamp(Locals, 0, 1);
	Bots = FMath::Clamp(Bots, 0, 4 - Locals);
	if (Locals + Bots == 0)
	{
		Bots = 1;
	}
	TArray<FSamuraiSeat> Seats;
	uint32 Seed = DefaultSeed;
	bRoomMatch = SetupRoom(Seats, Seed);
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
	{
		FSamuraiSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? ESwordKind::Local : ESwordKind::Bot;
		Seat.Skill = bLocal ? 0.85f : 0.45f + 0.15f * (i % 3);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i + 4);
	}
	Game = World->SpawnActor<ASamuraiGame>();
	Game->Setup(Dojo, Seats, World->GetFirstPlayerController(), Seed);
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
			Session->NoteLaunch(TEXT("samurai"), Locals, Bots);
		}
	}
	Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch);
	SetupTestHooks();
}

bool ASamuraiGameMode::SetupRoom(TArray<FSamuraiSeat>& OutSeats, uint32& OutSeed)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("samurai")))
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
		FSamuraiSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? ESwordKind::Bot : ESwordKind::Phone;
		Seat.Skill = 0.6f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	// runtime.ts: the round's seed is 0x5a3c + the match number.
	OutSeed = DefaultSeed + static_cast<uint32>(FMath::Max(0, Room->GetRoom().MatchNumber));
	UE_LOG(LogTemp, Display, TEXT("Samurai: room match %d with %d swordsm(e)n"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void ASamuraiGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &ASamuraiGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &ASamuraiGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);
	TWeakObjectPtr<ASamuraiGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		ASamuraiGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});
	// Simultaneous: every phone streams; the engine counts its own 3-2-1.
	Game->OnMatchStart = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(FString());
			R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		}
	};
	Game->OnEffect = [this, WeakRoom](const FEffect& E)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		const FPlayer* Player = Game ? Game->GetMatch().FindPlayer(E.PlayerId) : nullptr;
		if (!R || E.Label.IsEmpty())
		{
			return;
		}
		const bool bBad = E.Kind == EEffectKind::Bomb || E.Kind == EEffectKind::Miss;
		const FString Detail = FString::Printf(TEXT("%s%d points · %d lives · combo %d"), E.ScoreDelta > 0 ? TEXT("+") : TEXT(""), E.ScoreDelta,
			Player ? Player->Lives : 0, Player ? Player->Combo : 0);
		R->SendPlayFeedback(E.PlayerId, E.Label, Detail, bBad ? TEXT("warning") : TEXT("success"),
			bBad ? TEXT("warning") : (E.Kind == EEffectKind::Combo ? TEXT("celebration") : TEXT("impact")), E.Kind == EEffectKind::Combo && Player && Player->Combo >= 10);
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

void ASamuraiGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	for (const FPlayer& P : Game->GetMatch().Players)
	{
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = P.Id;
		Entry.Score = P.Score;
	}
	if (bFinal)
	{
		Room->CompleteGame(Scores);
	}
	else
	{
		Room->UpdateLiveScores(Scores);
	}
}

void ASamuraiGameMode::HandleRoomLobby()
{
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void ASamuraiGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void ASamuraiGameMode::EndPlay(const EEndPlayReason::Type Reason)
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
		Room->OnSensorFrame.Remove(SensorHandle);
		Room->OnReturnedToLobby.RemoveDynamic(this, &ASamuraiGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &ASamuraiGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
}

void ASamuraiGameMode::SetupTestHooks()
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
