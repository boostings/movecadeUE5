#include "Games/Derby/DerbyGameMode.h"

#include "Art/KartMaterials.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/ArcadeSession.h"
#include "Games/Derby/DerbyBallpark.h"
#include "Games/Derby/DerbyGame.h"
#include "Games/SArcadeOverlay.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "TimerManager.h"

using namespace MovecadeKart::Derby;

namespace
{
	const TCHAR* const SeatColors[] = { TEXT("#ff6b5a"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Dante"), TEXT("Rosa"), TEXT("Mika"), TEXT("Priya") };

	URoomHostSubsystem* RoomOf(const UObject* Context)
	{
		const UGameInstance* GI = Context && Context->GetWorld() ? Context->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}

	FString Hex(const FLinearColor& C)
	{
		const FColor S = C.ToFColor(true);
		return FString::Printf(TEXT("#%02x%02x%02x"), S.R, S.G, S.B);
	}

	/** The phone's toast for a pitch: the call and the detail. */
	void CallFor(const FOutcome& O, int32 Strikes, FString& Title, FString& Detail, FString& Tone, FString& Haptic, bool& bMajor)
	{
		bMajor = IsHomeRun(O.Result);
		Tone = O.Points > 0 ? TEXT("success") : (O.bStrike ? TEXT("warning") : TEXT("neutral"));
		Haptic = IsHomeRun(O.Result) ? TEXT("celebration") : (O.Result == EResult::InPlay || O.Result == EResult::Foul ? TEXT("impact") : TEXT("warning"));
		switch (O.Result)
		{
		case EResult::Moonshot: Title = FString::Printf(TEXT("MOONSHOT! %d FT"), O.DistanceFt); break;
		case EResult::HomeRun: Title = FString::Printf(TEXT("HOME RUN! %d FT"), O.DistanceFt); break;
		case EResult::InPlay: Title = FString::Printf(TEXT("%d FT"), O.DistanceFt); break;
		case EResult::Foul: Title = TEXT("FOUL BALL"); break;
		case EResult::Whiff: Title = Strikes >= StrikesPerOut ? TEXT("STRUCK OUT") : TEXT("SWING AND A MISS"); break;
		default: Title = Strikes >= StrikesPerOut ? TEXT("STRUCK OUT") : TEXT("STRIKE"); break;
		}
		if (O.Result == EResult::Take)
		{
			Detail = FString::Printf(TEXT("%d strike%s · swing when the ball arrives"), Strikes, Strikes == 1 ? TEXT("") : TEXT("s"));
		}
		else
		{
			const double Ms = O.TimingMs;
			const FString Timing = O.Contact.Quality >= 0.97 ? FString(TEXT("perfect timing")) : FString::Printf(TEXT("%d ms %s"), FMath::RoundToInt32(FMath::Abs(Ms)), Ms < 0.0 ? TEXT("early") : TEXT("late"));
			Detail = O.Points > 0 ? FString::Printf(TEXT("+%d · %s"), O.Points, *Timing) : Timing;
		}
	}
}

ADerbyGameMode::ADerbyGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ADerbyGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ADerbyBallpark> It(World); It; ++It)
	{
		Park = *It;
		break;
	}
	if (!Park)
	{
		Park = World->SpawnActor<ADerbyBallpark>();
	}
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 1);
	FParse::Value(FCommandLine::Get(), TEXT("DerbyPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("DerbyBots="), Bots);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("DerbyAuto"));
	uint32 Seed = FPlatformTime::Cycles();
	FParse::Value(FCommandLine::Get(), TEXT("DerbySeed="), Seed);
	Locals = FMath::Clamp(Locals, 0, 4);
	Bots = FMath::Clamp(Bots, 0, 4 - Locals);
	if (Locals + Bots == 0)
	{
		Bots = 1;
	}
	TArray<FDerbySeat> Seats;
	bRoomMatch = SetupRoom(Seats);
	if (bRoomMatch)
	{
		if (const URoomHostSubsystem* Room = RoomOf(this))
		{
			Seed = 0xDE4Bu ^ (7919u * static_cast<uint32>(Room->GetRoom().MatchNumber + 1));
		}
	}
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
	{
		FDerbySeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? EDerbyKind::Local : EDerbyKind::Bot;
		Seat.Skill = bLocal ? 0.85f : 0.5f + 0.12f * (i % 3);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i + 4);
	}
	Game = World->SpawnActor<ADerbyGame>();
	Game->Setup(Park, Seats, World->GetFirstPlayerController(), Seed);
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
		if (UArcadeSession* Session = UArcadeSession::Get(this))
		{
			Session->NoteLaunch(TEXT("derby"), Locals, Bots);
		}
	}
	Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch);
	SetupTestHooks();
}

bool ADerbyGameMode::SetupRoom(TArray<FDerbySeat>& OutSeats)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("derby")))
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
		FDerbySeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EDerbyKind::Bot : EDerbyKind::Phone;
		Seat.Skill = 0.62f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	UE_LOG(LogTemp, Display, TEXT("Derby: room match %d with %d batter(s)"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void ADerbyGameMode::Prompt(const FString& BatterId)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	const FDerbySeat* Up = Game->SeatOf(BatterId);
	for (const FDerbySeat& Seat : Game->GetSeats())
	{
		if (Seat.Kind != EDerbyKind::Phone)
		{
			continue;
		}
		const bool bUp = Seat.Id == BatterId;
		const FString Title = bUp ? TEXT("Swing your phone like a bat") : FString::Printf(TEXT("%s is batting"), Up ? *Up->Name : TEXT("Someone"));
		const FString Subtitle = bUp ? TEXT("Hold it by the bottom, screen to you. Swing as the ball reaches you.") : TEXT("Get loose: you're up soon");
		Room->SendGameEvent(TEXT("pad:prompt"), FString::Printf(TEXT("{\"playerId\":%s,\"title\":%s,\"subtitle\":%s,\"a\":%s,\"b\":\"\",\"accent\":\"%s\",\"mode\":\"swing\"}"),
			*MovecadeKart::Net::QuoteJson(Seat.Id), *MovecadeKart::Net::QuoteJson(Title), *MovecadeKart::Net::QuoteJson(Subtitle), bUp ? TEXT("\"READY\"") : TEXT("\"\""), *Hex(Seat.Color)));
	}
}

void ADerbyGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &ADerbyGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &ADerbyGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);
	TWeakObjectPtr<ADerbyGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		ADerbyGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});
	// The motion pad's button: READY while the pitcher waits, a button swing while the ball is on its way.
	GameEventHandle = Room->OnGameEvent.AddLambda([WeakGame](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		ADerbyGame* G = WeakGame.Get();
		if (!G || Event != TEXT("pad:press"))
		{
			return;
		}
		FString Button = TEXT("a");
		if (Data.IsValid())
		{
			Data->TryGetStringField(TEXT("button"), Button);
		}
		if (Button == TEXT("a"))
		{
			G->PressButton(From);
		}
	});
	Game->OnTurn = [this, WeakRoom](const FString& PlayerId)
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(PlayerId);
		}
		Prompt(PlayerId);
	};
	Game->OnPitch = [WeakRoom](const FString& PlayerId, const FPitch& Pitch)
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->SendGameEvent(TEXT("derby:pitch"), FString::Printf(TEXT("{\"playerId\":%s,\"pitch\":%d,\"type\":\"%s\",\"mph\":%d,\"arriveInMs\":%d}"),
				*MovecadeKart::Net::QuoteJson(PlayerId), Pitch.Index + 1, *FString(PitchName(Pitch.Type)).ToLower(), FMath::RoundToInt32(Pitch.SpeedMph), FMath::RoundToInt32(Pitch.FlightMs)));
		}
	};
	Game->OnOutcome = [this, WeakRoom](const FString& PlayerId, const FOutcome& O)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		if (!R || !Game)
		{
			return;
		}
		FString Title, Detail, Tone, Haptic;
		bool bMajor = false;
		CallFor(O, Game->ShownStrikes(), Title, Detail, Tone, Haptic, bMajor);
		R->SendPlayFeedback(PlayerId, Title, Detail, Tone, Haptic, bMajor);
		PushScores(false);
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

void ADerbyGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	FString Key;
	for (const FBatter& B : Game->GetMatch().Batters)
	{
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = B.Id;
		Entry.Score = FMath::Clamp(B.Score, 0, MovecadeKart::Net::MaxScore);
		Key += FString::Printf(TEXT("%s=%d;"), *B.Id, B.Score);
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

void ADerbyGameMode::HandleRoomLobby()
{
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void ADerbyGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void ADerbyGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Overlay.IsValid())
	{
		Overlay->Detach();
		Overlay.Reset();
	}
	GetWorldTimerManager().ClearTimer(ScoreTimer);
	if (URoomHostSubsystem* Room = RoomOf(this))
	{
		Room->OnGameEvent.Remove(GameEventHandle);
		Room->OnSensorFrame.Remove(SensorHandle);
		Room->OnReturnedToLobby.RemoveDynamic(this, &ADerbyGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &ADerbyGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
}

void ADerbyGameMode::SetupTestHooks()
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
