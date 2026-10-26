#include "Games/ArcadeGameModeBase.h"

#include "Algo/Compare.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeGame.h"
#include "Games/ArcadeSession.h"
#include "Games/ArcadeTestHooks.h"
#include "Games/SArcadeOverlay.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceControlsSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcadeMode, Log, All);

namespace
{
	/** runtime.ts: live scores once a second (the room drops anything faster anyway). */
	constexpr float LiveScoreIntervalSeconds = 1.f;
}

FArcadeLaunchOptions FArcadeLaunchOptions::Parse(const MovecadeKart::Arcade::FGameInfo& Game, const FString& TravelOptions,
	const TCHAR* CommandLine, const FString& Prefix, int32 DefaultLocals, int32 DefaultBots)
{
	FArcadeLaunchOptions Options;
	Options.Locals = UGameplayStatics::GetIntOption(TravelOptions, TEXT("Players"), DefaultLocals);
	Options.Bots = UGameplayStatics::GetIntOption(TravelOptions, TEXT("Bots"), DefaultBots);
#if !UE_BUILD_SHIPPING
	if (CommandLine && !Prefix.IsEmpty())
	{
		FParse::Value(CommandLine, *(Prefix + TEXT("Players=")), Options.Locals);
		FParse::Value(CommandLine, *(Prefix + TEXT("Bots=")), Options.Bots);
		Options.bAutoPlay = FParse::Param(CommandLine, *(Prefix + TEXT("Auto")));
	}
#endif
	MovecadeKart::Arcade::ClampSeats(Game, Options.Locals, Options.Bots);
	Options.bDeviceSeat = UDeviceControlsSubsystem::IsEnabled() && Options.Locals > 0 && !Options.bAutoPlay;
	return Options;
}

/** The device seat's phone traffic, into the mode's room handlers as if a phone in seat 1 sent it. */
class FArcadeDeviceSink : public MovecadeKart::Input::IDeviceInputSink
{
public:
	explicit FArcadeDeviceSink(AArcadeGameModeBase& InMode) : Mode(&InMode)
	{
		Seat.PlayerId = AArcadeGameModeBase::DeviceSeatId();
		Seat.Seat = 1;
		Seat.bConnected = true;
	}

	virtual void SendFrame(const MovecadeKart::Net::FSensorFrame& Frame) override
	{
		if (AArcadeGameModeBase* M = Mode.Get(); M && M->GameActor)
		{
			M->HandleSensorFrame(Seat, Frame);
		}
	}

	virtual void SendEvent(const FString& Event, const TSharedPtr<FJsonObject>& Data) override
	{
		if (AArcadeGameModeBase* M = Mode.Get(); M && M->GameActor)
		{
			M->HandleGameEvent(Seat.PlayerId, Event, Data);
		}
	}

private:
	TWeakObjectPtr<AArcadeGameModeBase> Mode;
	FMcSeat Seat;
};

AArcadeGameModeBase::AArcadeGameModeBase()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void AArcadeGameModeBase::StartPlay()
{
	Super::StartPlay();
	const MovecadeKart::Arcade::FGameInfo* Info = MovecadeKart::Arcade::Find(ArcadeId);
	if (!ensureMsgf(Info, TEXT("%s: ArcadeId '%s' is not in the arcade catalog"), *GetName(), *ArcadeId))
	{
		return;
	}
	SpawnVenue();
	Launch = FArcadeLaunchOptions::Parse(*Info, OptionsString, FCommandLine::Get(), CommandLinePrefix, DefaultLocals, DefaultBots);
	AdjustLaunchSeats(Launch);

	const TArray<FMcSeat> RoomPlayers = GatherRoomPlayers();
	bRoomMatch = RoomPlayers.Num() > 0;
	if (bRoomMatch)
	{
		BuildRoomSeats(RoomPlayers);
		UE_LOG(LogArcadeMode, Display, TEXT("%s: room match %d with %d seat(s)"), *ArcadeId, GetRoom()->GetRoom().MatchNumber, RoomPlayers.Num());
	}
	else
	{
		BuildLocalSeats(Launch);
	}
	SpawnGame();
	if (!ensureMsgf(GameActor && ArcadeGame, TEXT("%s: SpawnGame must spawn the game with SpawnArcadeGame<T>()"), *GetName()))
	{
		return;
	}
	if (URoomHostSubsystem* Room = GetRoom(); Room && bRoomMatch)
	{
		BindRoom(*Room);
	}
	SetGameInputMode();
	if (!bRoomMatch)
	{
		// Play again and next game keep these seats.
		if (UArcadeSession* Session = UArcadeSession::Get(this))
		{
			Session->NoteLaunch(ArcadeId, Launch.Locals, Launch.Bots);
		}
	}
	Overlay = SArcadeOverlay::AttachTo(GameActor, ArcadeGame, bRoomMatch);
	if (!bRoomMatch && Launch.bDeviceSeat)
	{
		ActivateDeviceControls();
	}
	FArcadeTestHooks::Parse(FCommandLine::Get()).Schedule(*this, TestHookTimers);
}

void AArcadeGameModeBase::ActivateDeviceControls()
{
	UDeviceControlsSubsystem* Device = UDeviceControlsSubsystem::Get(this);
	const TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> Scheme = MakeDeviceControls();
	if (!Device || !Scheme.IsValid())
	{
		return;
	}
	const TWeakPtr<SArcadeOverlay> WeakOverlay = Overlay;
	Device->Activate(Scheme.ToSharedRef(), MakeShared<FArcadeDeviceSink>(*this),
		[WeakOverlay]() { const TSharedPtr<SArcadeOverlay> O = WeakOverlay.Pin(); return O.IsValid() && O->AreMenusOpen(); },
		[WeakOverlay]() { if (const TSharedPtr<SArcadeOverlay> O = WeakOverlay.Pin()) { O->OpenPause(); } });
}

void AArcadeGameModeBase::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDeviceControlsSubsystem* Device = UDeviceControlsSubsystem::Get(this))
	{
		Device->Deactivate();
	}
	if (Overlay.IsValid())
	{
		Overlay->Detach();
		Overlay.Reset();
	}
	// The world's timer manager outlives level travel in practice: clear everything this mode armed.
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(LiveScoreTimer);
	for (FTimerHandle& Handle : TestHookTimers)
	{
		Timers.ClearTimer(Handle);
	}
	TestHookTimers.Reset();
	UnbindRoom();
	Super::EndPlay(Reason);
}

void AArcadeGameModeBase::RegisterArcadeGame(AActor* Actor, IArcadeGame* Game)
{
	check(Actor && Game);
	GameActor = Actor;
	ArcadeGame = Game;
}

URoomHostSubsystem* AArcadeGameModeBase::GetRoom() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<URoomHostSubsystem>() : nullptr;
}

TArray<FMcSeat> AArcadeGameModeBase::GatherRoomPlayers() const
{
	TArray<FMcSeat> Players;
	const URoomHostSubsystem* Room = GetRoom();
	const MovecadeKart::Arcade::FGameInfo* Info = MovecadeKart::Arcade::Find(ArcadeId);
	if (!Room || !Info || !Room->IsMatchRunning(ArcadeId))
	{
		return Players;
	}
	TArray<FMcSeat> BySeat = Room->GetSeats();
	BySeat.Sort([](const FMcSeat& A, const FMcSeat& B) { return A.Seat < B.Seat; });
	for (FMcSeat& Player : BySeat)
	{
		if (!Player.bEliminated && Players.Num() < Info->MaxPlayers)
		{
			Players.Add(MoveTemp(Player));
		}
	}
	return Players;
}

void AArcadeGameModeBase::BindRoom(URoomHostSubsystem& Room)
{
	BoundRoom = &Room;
	Room.SetPreparationReady(true);
	Room.OnReturnedToLobby.AddDynamic(this, &AArcadeGameModeBase::HandleRoomLobby);
	Room.OnPauseChanged.AddDynamic(this, &AArcadeGameModeBase::HandleRoomPauseChanged);
	ArcadeGame->SetArcadePaused(Room.GetRoom().Status == EMcRoomStatus::Paused);

	SensorHandle = Room.OnSensorFrame.AddWeakLambda(this, [this](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		const URoomHostSubsystem* BoundTo = BoundRoom.Get();
		if (const FMcSeat* Seat = BoundTo ? BoundTo->GetRoom().FindBySeat(SeatIndex + 1) : nullptr; Seat && GameActor)
		{
			HandleSensorFrame(*Seat, Frame);
		}
	});
	GameEventHandle = Room.OnGameEvent.AddWeakLambda(this, [this](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		if (GameActor)
		{
			HandleGameEvent(From, Event, Data);
		}
	});
	BindRoomCallbacks(Room);
	GetWorldTimerManager().SetTimer(LiveScoreTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PushLiveScores(); }),
		LiveScoreIntervalSeconds, /*bLoop*/ true);
}

void AArcadeGameModeBase::UnbindRoom()
{
	URoomHostSubsystem* Room = BoundRoom.Get();
	BoundRoom.Reset();
	if (!Room)
	{
		return;
	}
	Room->OnSensorFrame.Remove(SensorHandle);
	Room->OnGameEvent.Remove(GameEventHandle);
	Room->OnReturnedToLobby.RemoveDynamic(this, &AArcadeGameModeBase::HandleRoomLobby);
	Room->OnPauseChanged.RemoveDynamic(this, &AArcadeGameModeBase::HandleRoomPauseChanged);
	SensorHandle.Reset();
	GameEventHandle.Reset();
}

void AArcadeGameModeBase::SetGameInputMode() const
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

void AArcadeGameModeBase::PushLiveScores()
{
	URoomHostSubsystem* Room = BoundRoom.Get();
	TArray<FMcScoreEntry> Scores;
	if (!Room || !GameActor || !CollectScores(Scores, /*bFinal*/ false))
	{
		return;
	}
	const bool bSame = Scores.Num() == LastLiveScores.Num() && Algo::Compare(Scores, LastLiveScores, [](const FMcScoreEntry& A, const FMcScoreEntry& B)
	{
		return A.PlayerId == B.PlayerId && A.Score == B.Score;
	});
	if (!bSame && Room->UpdateLiveScores(Scores))
	{
		LastLiveScores = MoveTemp(Scores);
	}
}

void AArcadeGameModeBase::CompleteRoomMatch()
{
	URoomHostSubsystem* Room = BoundRoom.Get();
	TArray<FMcScoreEntry> Scores;
	if (Room && GameActor && CollectScores(Scores, /*bFinal*/ true))
	{
		Room->CompleteGame(Scores);
	}
}

void AArcadeGameModeBase::RequestRematch()
{
	if (URoomHostSubsystem* Room = BoundRoom.Get())
	{
		Room->RestartGame();
	}
}

void AArcadeGameModeBase::TravelToFrontEnd()
{
	UGameplayStatics::OpenLevel(this, FrontEndLevel);
}

TFunction<void()> AArcadeGameModeBase::MakeRematchCallback()
{
	if (!bRoomMatch)
	{
		return nullptr;
	}
	return [WeakThis = TWeakObjectPtr<AArcadeGameModeBase>(this)]()
	{
		if (AArcadeGameModeBase* Mode = WeakThis.Get())
		{
			Mode->RequestRematch();
		}
	};
}

TFunction<void()> AArcadeGameModeBase::MakeQuitCallback()
{
	// A room quits through its lobby, like a rematch: everyone re-readies there (engine.ts restart-game).
	if (bRoomMatch)
	{
		return MakeRematchCallback();
	}
	return [WeakThis = TWeakObjectPtr<AArcadeGameModeBase>(this)]()
	{
		if (AArcadeGameModeBase* Mode = WeakThis.Get())
		{
			Mode->TravelToFrontEnd();
		}
	};
}

void AArcadeGameModeBase::HandleRoomPaused(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (ArcadeGame && GameActor)
	{
		ArcadeGame->SetArcadePaused(bPaused);
	}
}

void AArcadeGameModeBase::HandleRoomLobby()
{
	// The lobby lives on the front end's level; the room (a game instance subsystem) rides along.
	TravelToFrontEnd();
}

void AArcadeGameModeBase::HandleRoomPauseChanged(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	HandleRoomPaused(bPaused, Reason, PlayerId);
}
