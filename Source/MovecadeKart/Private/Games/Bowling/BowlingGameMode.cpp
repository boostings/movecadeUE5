#include "Games/Bowling/BowlingGameMode.h"

#include "Art/KartMaterials.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Games/Bowling/BowlingGame.h"
#include "Games/ArcadeSession.h"
#include "Games/SArcadeOverlay.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "TimerManager.h"

namespace
{
	/** The web's lane colours, one per seat. */
	const TCHAR* const SeatColors[] = { TEXT("#6d8bff"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Mika"), TEXT("Dante"), TEXT("Priya"), TEXT("Rosa") };

	/** presentation.ts beatLabel, for the phones' toasts and the replay event. */
	FString BeatLabel(EBowlingBeat Beat, int32 Pins)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return TEXT("TURKEY");
		case EBowlingBeat::Strike: return TEXT("STRIKE");
		case EBowlingBeat::Spare: return TEXT("SPARE");
		case EBowlingBeat::SplitConvert: return TEXT("SPLIT CONVERT");
		case EBowlingBeat::Split: return TEXT("SPLIT");
		case EBowlingBeat::Gutter: return TEXT("GUTTER");
		case EBowlingBeat::Good: return FString::Printf(TEXT("%d DOWN"), Pins);
		default: return Pins == 0 ? FString(TEXT("NO PINS")) : FString::Printf(TEXT("%d DOWN"), Pins);
		}
	}

	/** The web's BowlingBeat names. */
	const TCHAR* BeatId(EBowlingBeat Beat)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return TEXT("turkey");
		case EBowlingBeat::Strike: return TEXT("strike");
		case EBowlingBeat::Spare: return TEXT("spare");
		case EBowlingBeat::SplitConvert: return TEXT("split-convert");
		case EBowlingBeat::Split: return TEXT("split");
		case EBowlingBeat::Gutter: return TEXT("gutter");
		case EBowlingBeat::Good: return TEXT("good");
		default: return TEXT("open");
		}
	}

	URoomHostSubsystem* RoomOf(const UObject* Context)
	{
		const UGameInstance* GI = Context && Context->GetWorld() ? Context->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}
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

	// The front end's picker passes ?Players=&Bots=; -BowlPlayers= / -BowlBots= on the command line override (tests).
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 1);
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
	bRoomMatch = SetupRoom(Seats);
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
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
			Session->NoteLaunch(TEXT("bowling"), Locals, Bots);
		}
	}
	Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch);
	SetupTestHooks();
}

bool ABowlingGameMode::SetupRoom(TArray<FBowlerSeat>& OutSeats)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("bowling")))
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
		FBowlerSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EBowlerKind::Bot : EBowlerKind::Phone;
		Seat.Skill = 0.6f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	UE_LOG(LogTemp, Display, TEXT("Bowling: room match %d with %d bowler(s)"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void ABowlingGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &ABowlingGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &ABowlingGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);

	TWeakObjectPtr<ABowlingGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	GameEventHandle = Room->OnGameEvent.AddLambda([WeakGame](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		ABowlingGame* G = WeakGame.Get();
		double Power = 0.0;
		if (!G || Event != TEXT("bowling:release") || !Data.IsValid() || !Data->TryGetNumberField(TEXT("power"), Power) || !FMath::IsFinite(Power))
		{
			return;
		}
		auto Field = [&Data](const TCHAR* Key)
		{
			double V = 0.0;
			return Data->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? V : 0.0;
		};
		MovecadeKart::Bowling::FShot Throw;
		Throw.Aim = Field(TEXT("aim"));
		Throw.Power = Power;
		Throw.Curve = Field(TEXT("curve"));
		Throw.Loft = Field(TEXT("loft"));
		G->SubmitRelease(From, Throw);
	});
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		ABowlingGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});

	// runtime.ts: announce the bowler up on each change, so the server lets only their phone throw.
	Game->OnTurn = [WeakRoom](const FString& PlayerId)
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(PlayerId);
		}
	};
	Game->OnRoll = [this, WeakRoom, WeakGame](const FString& PlayerId, int32 Pins, EBowlingBeat Beat)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		const ABowlingGame* G = WeakGame.Get();
		if (!R || !G || PlayerId.IsEmpty())
		{
			return;
		}
		const MovecadeKart::Bowling::FShotResult* Result = G->GetShot();
		const bool bBig = Beat == EBowlingBeat::Strike || Beat == EBowlingBeat::Turkey || Beat == EBowlingBeat::SplitConvert;
		const FString Label = BeatLabel(Beat, Pins);
		R->SendPlayFeedback(PlayerId, Label, FString::Printf(TEXT("%d pins · %.1f m/s"), Pins, Result ? Result->Speed : 0.0),
			Beat == EBowlingBeat::Gutter || Pins == 0 ? TEXT("warning") : TEXT("success"),
			bBig ? TEXT("celebration") : (Pins > 0 ? TEXT("impact") : TEXT("warning")), bBig);
		R->SendGameEvent(TEXT("bowling:replay-outcome"), FString::Printf(TEXT("{\"playerId\":%s,\"pins\":%d,\"label\":%s,\"beat\":\"%s\"}"),
			*MovecadeKart::Net::QuoteJson(PlayerId), Pins, *MovecadeKart::Net::QuoteJson(Label), BeatId(Beat)));
		PushScores(false);
	};
	Game->OnFinished = [this]() { PushScores(true); };
	// A room rematches (and quits) through its lobby: everyone re-readies there (engine.ts restart-game).
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

void ABowlingGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	for (const FString& Id : Game->GetMatch().Order)
	{
		const MovecadeKart::Bowling::FCard* Card = Game->GetMatch().CardOf(Id);
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = Id;
		Entry.Score = Card ? Card->Score : 0;
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

void ABowlingGameMode::HandleRoomLobby()
{
	// The lobby lives on the front end's level; the room (a game instance subsystem) rides along.
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void ABowlingGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void ABowlingGameMode::EndPlay(const EEndPlayReason::Type Reason)
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
		Room->OnReturnedToLobby.RemoveDynamic(this, &ABowlingGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &ABowlingGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
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
