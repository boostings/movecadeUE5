#include "Games/Darts/DartsGameMode.h"

#include "Art/KartMaterials.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Darts/DartsGame.h"
#include "Games/Darts/DartsPub.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "TimerManager.h"

using namespace MovecadeKart::Darts;

namespace
{
	/** The web's lane colours, one per seat. */
	const TCHAR* const SeatColors[] = { TEXT("#f0508e"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Dante"), TEXT("Priya"), TEXT("Mika"), TEXT("Rosa") };

	URoomHostSubsystem* RoomOf(const UObject* Context)
	{
		const UGameInstance* GI = Context && Context->GetWorld() ? Context->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}

	FString Quote(const FString& Value)
	{
		return MovecadeKart::Net::QuoteJson(Value);
	}
}

ADartsGameMode::ADartsGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ADartsGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ADartsPub> It(World); It; ++It)
	{
		Pub = *It;
		break;
	}
	if (!Pub)
	{
		Pub = World->SpawnActor<ADartsPub>();
	}

	// The front end's picker passes ?Players=&Bots=; -DartsPlayers= / -DartsBots= on the command line override (tests).
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 1);
	FParse::Value(FCommandLine::Get(), TEXT("DartsPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("DartsBots="), Bots);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("DartsAuto"));
	Locals = FMath::Clamp(Locals, 0, 4);
	Bots = FMath::Clamp(Bots, 0, 4 - Locals);
	if (Locals + Bots == 0)
	{
		Bots = 1;
	}
	TArray<FDartsSeat> Seats;
	bRoomMatch = SetupRoom(Seats);
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
	{
		FDartsSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? EThrowerKind::Local : EThrowerKind::Bot;
		Seat.Skill = bLocal ? 0.85f : 0.45f + 0.15f * (i % 3);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i + 2);
	}
	Game = World->SpawnActor<ADartsGame>();
	Game->Setup(Pub, Seats, World->GetFirstPlayerController());
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
	SetupTestHooks();
}

bool ADartsGameMode::SetupRoom(TArray<FDartsSeat>& OutSeats)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("darts")))
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
		FDartsSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EThrowerKind::Bot : EThrowerKind::Phone;
		Seat.Skill = 0.6f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	UE_LOG(LogTemp, Display, TEXT("Darts: room match %d with %d thrower(s)"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void ADartsGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &ADartsGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &ADartsGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);

	TWeakObjectPtr<ADartsGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	GameEventHandle = Room->OnGameEvent.AddLambda([WeakGame](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		ADartsGame* G = WeakGame.Get();
		double Power = 0.0;
		if (!G || Event != TEXT("darts:release") || !Data.IsValid() || !Data->TryGetNumberField(TEXT("power"), Power))
		{
			return;
		}
		auto Field = [&Data](const TCHAR* Key, double Fallback)
		{
			double V = 0.0;
			return Data->TryGetNumberField(Key, V) ? V : Fallback;
		};
		FDartGesture Gesture;
		Gesture.AimX = Field(TEXT("aimX"), 0.0);
		Gesture.AimY = Field(TEXT("aimY"), 0.0);
		Gesture.Power = Power;
		Gesture.SpinDegPerSec = Field(TEXT("spinDegPerSec"), 0.0);
		Gesture.RollDegPerSec = Field(TEXT("rollDegPerSec"), 0.0);
		Gesture.Confidence = Field(TEXT("confidence"), 0.5);
		Gesture.Steadiness = Field(TEXT("steadiness"), 0.5);
		Gesture.PeakMs2 = Field(TEXT("peakMs2"), 0.0);
		Gesture.DurationMs = Field(TEXT("durationMs"), 0.0);
		G->SubmitRelease(From, Gesture);
	});
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		ADartsGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});

	// runtime.ts: every seated phone streams (activePlayerId null), once per match; the engine counts its own 3-2-1.
	Game->OnMatchStart = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(FString());
			R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		}
	};
	Game->OnThrow = [this, WeakRoom](const FString& PlayerId, const FThrowOutcome& Outcome)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		if (!R)
		{
			return;
		}
		const FDartLanding& L = Outcome.Landing;
		const bool bMajor = L.bBounty || L.Hit.Ring == ERing::Bull;
		R->SendPlayFeedback(PlayerId, Outcome.Label, FString::Printf(TEXT("%d points%s"), L.ScoreDelta, L.bBounty ? TEXT(" · Double points") : TEXT("")),
			L.ScoreDelta > 0 ? TEXT("success") : TEXT("warning"), bMajor ? TEXT("celebration") : (L.ScoreDelta > 0 ? TEXT("impact") : TEXT("warning")), bMajor);
		R->SendGameEvent(TEXT("darts:result"), FString::Printf(TEXT("{\"playerId\":%s,\"label\":%s,\"score\":%d,\"bounty\":%s,\"roundBonus\":%d,\"finished\":%s}"),
			*Quote(PlayerId), *Quote(Outcome.Label), L.ScoreDelta, L.bBounty ? TEXT("true") : TEXT("false"), Outcome.RoundBonus, Outcome.bFinished ? TEXT("true") : TEXT("false")));
		R->SendGameEvent(TEXT("darts:replay-outcome"), FString::Printf(TEXT("{\"playerId\":%s,\"label\":%s,\"score\":%d}"), *Quote(PlayerId), *Quote(Outcome.Label), L.ScoreDelta));
	};
	Game->OnFinished = [this]()
	{
		PushTurnAndAim();
		PushScores(true);
	};
	// A room rematches (and quits) through its lobby: everyone re-readies there.
	Game->OnRematch = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->RestartGame();
		}
	};
	Game->OnQuit = Game->OnRematch;
	GetWorldTimerManager().SetTimer(ScoreTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PushScores(false); }), 1.0f, true);
	GetWorldTimerManager().SetTimer(AimTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PushTurnAndAim(); }), 0.08f, true);
}

void ADartsGameMode::PushTurnAndAim()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	const FDartsMatch& M = Game->GetMatch();
	const FDartsPlayer* Current = M.ActivePlayer();
	const bool bIntro = Game->GetStage() == EDartsStage::Intro;
	const TCHAR* Phase = M.Phase == EMatchPhase::Playing ? TEXT("playing") : (M.Phase == EMatchPhase::Ended ? TEXT("ended") : TEXT("countdown"));
	const int32 DartsLeft = Current && M.Phase == EMatchPhase::Playing && !bIntro ? FMath::Max(0, DartsPerRound - M.TurnDarts) : 0;
	const int32 Round = Current ? FMath::Min((Current->DartsThrown - M.TurnDarts) / DartsPerRound, RoundCount - 1) : RoundCount;
	const FString Turn = FString::Printf(TEXT("{\"activePlayerId\":%s,\"activePlayerName\":%s,\"dartsLeft\":%d,\"round\":%d,\"phase\":\"%s\"}"),
		Current ? *Quote(Current->Id) : TEXT("null"), Current ? *Quote(Current->Name) : TEXT("null"), DartsLeft, Round, Phase);
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (Turn != LastTurnKey || Now - TurnPushedAt >= 0.25)
	{
		LastTurnKey = Turn;
		TurnPushedAt = Now;
		Room->SendGameEvent(TEXT("darts:turn"), Turn);
	}
	FVector2D Aim;
	if (Current && M.Phase == EMatchPhase::Playing && Game->GetReticle(Aim))
	{
		Room->SendGameEvent(TEXT("darts:aim"), FString::Printf(TEXT("[{\"p\":%s,\"x\":%.3f,\"y\":%.3f}]"), *Quote(Current->Id), Aim.X, Aim.Y));
	}
}

void ADartsGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	for (const FDartsPlayer& P : Game->GetMatch().Players)
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

void ADartsGameMode::HandleRoomLobby()
{
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void ADartsGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void ADartsGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// The world's timers live on the game instance and outlast the level.
	GetWorldTimerManager().ClearTimer(ScoreTimer);
	GetWorldTimerManager().ClearTimer(AimTimer);
	if (URoomHostSubsystem* Room = RoomOf(this))
	{
		Room->OnGameEvent.Remove(GameEventHandle);
		Room->OnSensorFrame.Remove(SensorHandle);
		Room->OnReturnedToLobby.RemoveDynamic(this, &ADartsGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &ADartsGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
}

void ADartsGameMode::SetupTestHooks()
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
