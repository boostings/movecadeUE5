#include "Games/Fishing/FishingGameMode.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/ArcadeSession.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingLake.h"
#include "Games/SArcadeOverlay.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"
#include "TimerManager.h"

using namespace MovecadeKart::Fishing;

namespace
{
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };

	URoomHostSubsystem* RoomOf(const UObject* Context)
	{
		const UGameInstance* GI = Context && Context->GetWorld() ? Context->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}

	FString HexOf(const FLinearColor& C)
	{
		const FColor S = C.ToFColor(true);
		return FString::Printf(TEXT("#%02x%02x%02x"), S.R, S.G, S.B);
	}
}

AFishingGameMode::AFishingGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

FString AFishingGameMode::PromptJson(const FAngler& A, const FLinearColor& Accent)
{
	using MovecadeKart::Net::QuoteJson;
	FString Title, Subtitle, ButtonA, Mode = TEXT("flick");
	switch (A.State)
	{
	case EAnglerState::Idle:
		Title = TEXT("CAST!");
		Subtitle = TEXT("Flick the phone forward like a rod. Harder casts reach the big fish.");
		ButtonA = TEXT("HOLD TO CAST");
		break;
	case EAnglerState::Casting:
	case EAnglerState::Waiting:
		Title = TEXT("Wait for the bite...");
		Subtitle = TEXT("When the phone buzzes, jerk it up to hook. Roll your wrist to reel the lure back.");
		ButtonA = TEXT("HOOK");
		break;
	case EAnglerState::Bite:
		Title = TEXT("BITE! JERK UP!");
		Subtitle = TEXT("Snap the phone up now to set the hook.");
		ButtonA = TEXT("HOOK");
		break;
	case EAnglerState::Fighting:
		Title = TEXT("REEL IT IN!");
		Subtitle = TEXT("Roll your wrist to reel. Keep the tension in the green; ease off when it runs.");
		Mode = TEXT("swing");
		break;
	case EAnglerState::Landed:
		Title = FString::Printf(TEXT("%s!"), Species()[A.LastCatch.Species].Name);
		Subtitle = Species()[A.LastCatch.Species].Kind == EKind::Junk ? TEXT("Junk. Back in the lake it goes.") : FString::Printf(TEXT("%s on the scale"), *FormatWeight(A.LastCatch.WeightG));
		break;
	case EAnglerState::Lost:
		Title = A.LastLoss == ELoss::Snap ? TEXT("Line snapped!") : (A.LastLoss == ELoss::Escape ? TEXT("It got away!") : TEXT("Bait stolen!"));
		Subtitle = A.LastLoss == ELoss::Snap ? TEXT("Too much tension. Ease off when it runs.") : (A.LastLoss == ELoss::Escape ? TEXT("Too slack. Keep reeling.") : TEXT("Hook faster when it bites."));
		break;
	}
	return FString::Printf(TEXT("{\"playerId\":%s,\"title\":%s,\"subtitle\":%s,\"a\":%s,\"b\":\"\",\"accent\":%s,\"mode\":%s}"), *QuoteJson(A.PlayerId), *QuoteJson(Title),
		*QuoteJson(Subtitle), *QuoteJson(ButtonA), *QuoteJson(HexOf(Accent)), *QuoteJson(Mode));
}

void AFishingGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<AFishingLake> It(World); It; ++It)
	{
		Lake = *It;
		break;
	}
	if (!Lake)
	{
		Lake = World->SpawnActor<AFishingLake>();
	}
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 2);
	FParse::Value(FCommandLine::Get(), TEXT("FishingPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("FishingBots="), Bots);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("FishingAuto"));
	uint32 Seed = static_cast<uint32>(FDateTime::Now().GetTicks() & 0x7fffffff);
	FParse::Value(FCommandLine::Get(), TEXT("FishingSeed="), Seed);
	// One local angler: a simultaneous game needs a pair of hands each.
	Locals = FMath::Clamp(Locals, 0, 1);
	Bots = FMath::Clamp(Bots, 0, 4 - Locals);
	if (Locals + Bots == 0)
	{
		Bots = 1;
	}
	TArray<FFishingSeat> Seats;
	bRoomMatch = SetupRoom(Seats);
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
	{
		FFishingSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? EAnglerKind::Local : EAnglerKind::Bot;
		Seat.Skill = bLocal ? 0.92f : 0.45f + 0.15f * (i % 3);
		Seat.Color = AFishingLake::LaneColor(i);
		Seat.Look = FKartLook::MakeVaried(i + 3);
	}
	Game = World->SpawnActor<AFishingGame>();
	Game->Setup(Lake, Seats, World->GetFirstPlayerController(), Seed);
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
			Session->NoteLaunch(TEXT("fishing"), Locals, Bots);
		}
	}
	Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch);
	SetupTestHooks();
}

bool AFishingGameMode::SetupRoom(TArray<FFishingSeat>& OutSeats)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("fishing")))
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
		FFishingSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EAnglerKind::Bot : EAnglerKind::Phone;
		Seat.Skill = 0.65f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	UE_LOG(LogTemp, Display, TEXT("Fishing: room match %d with %d angler(s)"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void AFishingGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &AFishingGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &AFishingGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);
	TWeakObjectPtr<AFishingGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		AFishingGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});
	// The motion pad's button: hold to charge a cast, press to hook.
	GameEventHandle = Room->OnGameEvent.AddLambda([WeakGame](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		AFishingGame* G = WeakGame.Get();
		FString Button = TEXT("a");
		if (Data.IsValid())
		{
			Data->TryGetStringField(TEXT("button"), Button);
		}
		if (!G || Button != TEXT("a"))
		{
			return;
		}
		if (Event == TEXT("pad:press"))
		{
			G->PadPress(From);
		}
		else if (Event == TEXT("pad:release"))
		{
			G->PadRelease(From);
		}
	});
	Game->OnMatchStart = [this, WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(FString());
			R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
			for (const FAngler& A : Game->GetMatch().Anglers)
			{
				const FFishingSeat* Seat = Game->SeatOf(A.PlayerId);
				if (Seat && Seat->Kind == EAnglerKind::Phone)
				{
					R->SendGameEvent(TEXT("pad:prompt"), PromptJson(A, AFishingLake::LaneColor(A.LaneIndex)));
				}
			}
		}
	};
	Game->OnAnglerState = [this, WeakRoom](const FAngler& A)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		const FFishingSeat* Seat = Game ? Game->SeatOf(A.PlayerId) : nullptr;
		if (R && Seat && Seat->Kind == EAnglerKind::Phone && A.State != EAnglerState::Casting)
		{
			R->SendGameEvent(TEXT("pad:prompt"), PromptJson(A, AFishingLake::LaneColor(A.LaneIndex)));
		}
	};
	// Toasts and haptics: a buzz on the nibble, a hard one on the bite, a celebration on the catch.
	Game->OnEffect = [this, WeakRoom](const FEffect& E)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		const FAngler* A = Game ? Game->GetMatch().Find(E.PlayerId) : nullptr;
		const FFishingSeat* Seat = Game ? Game->SeatOf(E.PlayerId) : nullptr;
		if (!R || !A || !Seat || Seat->Kind != EAnglerKind::Phone)
		{
			return;
		}
		const FMcSeat* Player = R->GetRoom().FindByPlayerId(E.PlayerId);
		const int32 SeatIndex = Player ? Player->SeatIndex() : INDEX_NONE;
		switch (E.Kind)
		{
		case EEffectKind::Nibble:
			R->SendHaptic(SeatIndex, 0.3f, TEXT("nibble"));
			break;
		case EEffectKind::Bite:
			R->SendHaptic(SeatIndex, 1.f, TEXT("bite"));
			R->SendPlayFeedback(E.PlayerId, TEXT("BITE!"), TEXT("Jerk the phone up to hook it"), TEXT("success"), TEXT("impact"), false);
			break;
		case EEffectKind::Hooked:
			R->SendPlayFeedback(E.PlayerId, E.Label, TEXT("Roll your wrist to reel"), TEXT("success"), TEXT("impact"), false);
			break;
		case EEffectKind::Burst:
			R->SendHaptic(SeatIndex, 0.7f, TEXT("run"));
			break;
		case EEffectKind::Catch:
		case EEffectKind::Record:
		case EEffectKind::Junk:
		{
			const FSpecies& S = Species()[FMath::Max(0, E.Species)];
			const FString Title = E.Kind == EEffectKind::Record ? FString(TEXT("NEW RECORD!")) : FString::Printf(TEXT("%s!"), S.Name);
			const FString Detail = E.Kind == EEffectKind::Junk ? FString(TEXT("Junk scores nothing, but your combo lives"))
				: FString::Printf(TEXT("%s · +%s · total %s"), *FormatWeight(E.WeightG), *FormatWeight(E.ScoreG), *FormatWeight(A->Score));
			R->SendPlayFeedback(E.PlayerId, Title, Detail, TEXT("success"), E.Kind == EEffectKind::Junk ? TEXT("impact") : TEXT("celebration"), E.Kind == EEffectKind::Record || S.Kind == EKind::Trophy);
			break;
		}
		case EEffectKind::Snap:
		case EEffectKind::Escape:
		case EEffectKind::StolenBait:
		case EEffectKind::TooEarly:
			R->SendPlayFeedback(E.PlayerId, E.Label, E.Kind == EEffectKind::Snap ? TEXT("Ease off when the fish runs") : (E.Kind == EEffectKind::TooEarly ? TEXT("Wait for the big buzz") : TEXT("Combo lost")),
				TEXT("warning"), TEXT("warning"), false);
			break;
		default:
			break;
		}
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

void AFishingGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	FString Key;
	for (const FAngler& A : Game->GetMatch().Anglers)
	{
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = A.PlayerId;
		// Grams: whole numbers inside the room's score range.
		Entry.Score = FMath::Clamp(A.Score, 0, MovecadeKart::Net::MaxScore);
		Key += FString::Printf(TEXT("%s=%d;"), *A.PlayerId, Entry.Score);
	}
	if (bFinal)
	{
		Room->CompleteGame(Scores);
		UE_LOG(LogTemp, Display, TEXT("Fishing: complete-game %s"), *Key);
	}
	else if (Key != LastScores)
	{
		LastScores = Key;
		Room->UpdateLiveScores(Scores);
	}
}

void AFishingGameMode::HandleRoomLobby()
{
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void AFishingGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void AFishingGameMode::EndPlay(const EEndPlayReason::Type Reason)
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
		Room->OnReturnedToLobby.RemoveDynamic(this, &AFishingGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &AFishingGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
}

void AFishingGameMode::SetupTestHooks()
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
