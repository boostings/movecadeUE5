#include "Games/Sumo/SumoGameMode.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/ArcadeSession.h"
#include "Games/SArcadeOverlay.h"
#include "Games/Sumo/SumoArena.h"
#include "Games/Sumo/SumoGame.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"
#include "TimerManager.h"

using namespace MovecadeKart::Sumo;

namespace
{
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };
	constexpr uint32 DefaultSeed = 0x50e0;

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

ASumoGameMode::ASumoGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ASumoGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ASumoArena> It(World); It; ++It)
	{
		Arena = *It;
		break;
	}
	if (!Arena)
	{
		Arena = World->SpawnActor<ASumoArena>();
	}
	int32 Locals = UGameplayStatics::GetIntOption(OptionsString, TEXT("Players"), 1);
	int32 Bots = UGameplayStatics::GetIntOption(OptionsString, TEXT("Bots"), 2);
	FParse::Value(FCommandLine::Get(), TEXT("SumoPlayers="), Locals);
	FParse::Value(FCommandLine::Get(), TEXT("SumoBots="), Bots);
	float Skill = -1.f;
	FParse::Value(FCommandLine::Get(), TEXT("SumoSkill="), Skill);
	const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("SumoAuto"));
	// One local player (a simultaneous game needs a pair of hands each); a sumo needs an opponent.
	Locals = FMath::Clamp(Locals, 0, 1);
	Bots = FMath::Clamp(Bots, 0, MaxPlayers - Locals);
	if (Locals + Bots < 2)
	{
		Bots = 2 - Locals;
	}
	TArray<FSumoSeat> Seats;
	uint32 Seed = DefaultSeed + static_cast<uint32>(FDateTime::Now().GetTicks() & 0xffff);
	bRoomMatch = SetupRoom(Seats, Seed);
	for (int32 i = 0; !bRoomMatch && i < Locals + Bots; ++i)
	{
		FSumoSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !bAuto ? ESumoPilot::Local : ESumoPilot::Bot;
		// House bots of mixed skill: one keen, one steady, one wild.
		Seat.Skill = bLocal ? 0.85f : (Skill >= 0.f ? Skill : 0.45f + 0.17f * ((i - Locals) % 3));
		Seat.Color = ASumoArena::SlotColor(i);
		Seat.Look = FKartLook::MakeVaried(i + 11);
	}
	Game = World->SpawnActor<ASumoGame>();
	Game->Setup(Arena, Seats, World->GetFirstPlayerController(), Seed);
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
			Session->NoteLaunch(TEXT("sumo"), Locals, Bots);
		}
	}
	Overlay = SArcadeOverlay::Attach(Game.Get(), bRoomMatch);
	SetupTestHooks();
}

bool ASumoGameMode::SetupRoom(TArray<FSumoSeat>& OutSeats, uint32& OutSeed)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Room->IsMatchRunning(TEXT("sumo")))
	{
		return false;
	}
	TArray<FMcSeat> Players = Room->GetSeats();
	Players.Sort([](const FMcSeat& A, const FMcSeat& B) { return A.Seat < B.Seat; });
	for (const FMcSeat& Player : Players)
	{
		if (Player.bEliminated || OutSeats.Num() >= MaxPlayers)
		{
			continue;
		}
		FSumoSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? ESumoPilot::Bot : ESumoPilot::Phone;
		Seat.Skill = 0.6f;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	// A lone phone gets a house sparring partner: sumo needs two.
	if (OutSeats.Num() == 1)
	{
		FSumoSeat& Seat = OutSeats.AddDefaulted_GetRef();
		Seat.Id = TEXT("house-1");
		Seat.Name = HouseNames[0];
		Seat.Kind = ESumoPilot::Bot;
		Seat.Skill = 0.5f;
		Seat.Look = FKartLook::MakeVaried(12);
	}
	OutSeed = DefaultSeed + static_cast<uint32>(FMath::Max(0, Room->GetRoom().MatchNumber));
	UE_LOG(LogTemp, Display, TEXT("Sumo: room match %d with %d wrestler(s)"), Room->GetRoom().MatchNumber, OutSeats.Num());
	return OutSeats.Num() > 0;
}

void ASumoGameMode::BindRoom()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	Room->SetPreparationReady(true);
	Room->OnReturnedToLobby.AddDynamic(this, &ASumoGameMode::HandleRoomLobby);
	Room->OnPauseChanged.AddDynamic(this, &ASumoGameMode::HandleRoomPause);
	Game->SetPaused(Room->GetRoom().Status == EMcRoomStatus::Paused);
	TWeakObjectPtr<ASumoGame> WeakGame = Game;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = Room;
	SensorHandle = Room->OnSensorFrame.AddLambda([WeakGame, WeakRoom](int32 SeatIndex, const MovecadeKart::Net::FSensorFrame& Frame)
	{
		ASumoGame* G = WeakGame.Get();
		const URoomHostSubsystem* R = WeakRoom.Get();
		const FMcSeat* Seat = R ? R->GetRoom().FindBySeat(SeatIndex + 1) : nullptr;
		if (G && Seat)
		{
			G->FeedPhone(Seat->PlayerId, Frame);
		}
	});
	// The motion pad: its big A button dashes (B too, so either thumb works); releases are not needed.
	GameEventHandle = Room->OnGameEvent.AddLambda([WeakGame](const FString& From, const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		ASumoGame* G = WeakGame.Get();
		TCHAR Button = 0;
		if (G && Event == MovecadeKart::Net::PadPressEvent && Data.IsValid() && MovecadeKart::Net::ReadPadButton(*Data, Button))
		{
			G->PressDash(From);
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
	Game->OnRoundStart = [this](int32 Round) { SendPrompts(); };
	Game->OnEvent = [this, WeakRoom](const FRingEvent& E)
	{
		URoomHostSubsystem* R = WeakRoom.Get();
		if (!R || !Game)
		{
			return;
		}
		const FMatch& M = Game->GetMatch();
		auto IdOf = [&M](int32 Slot) { return M.Pucks.IsValidIndex(Slot) ? M.Pucks[Slot].Id : FString(); };
		auto IsPhone = [this](int32 Slot) { const FSumoSeat* S = Game->SeatOf(Slot); return S && S->Kind == ESumoPilot::Phone; };
		auto SeatOf = [R](const FString& Id) { const FMcSeat* S = R->GetRoom().FindByPlayerId(Id); return S ? S->SeatIndex() : INDEX_NONE; };
		switch (E.Kind)
		{
		case EEventKind::Bump:
		case EEventKind::DashHit:
		case EEventKind::ShieldBlock:
			for (const int32 Slot : { E.A, E.B })
			{
				const int32 Seat = IsPhone(Slot) ? SeatOf(IdOf(Slot)) : INDEX_NONE;
				if (Seat != INDEX_NONE)
				{
					R->SendHaptic(Seat, static_cast<float>(FMath::Clamp(E.Strength / 12.0, 0.2, 1.0)));
				}
			}
			break;
		case EEventKind::RingOut:
		{
			const FPuck* Victim = M.Pucks.IsValidIndex(E.A) ? &M.Pucks[E.A] : nullptr;
			const FPuck* Killer = M.Pucks.IsValidIndex(E.B) ? &M.Pucks[E.B] : nullptr;
			if (Victim && IsPhone(E.A))
			{
				R->SendPlayFeedback(Victim->Id, Killer ? TEXT("Knocked out!") : TEXT("Ring out!"),
					Killer ? FString::Printf(TEXT("%s pushed you in · round %d"), *Killer->Name, M.Round) : FString::Printf(TEXT("You rolled off · round %d"), M.Round), TEXT("warning"), TEXT("warning"));
			}
			if (Killer && IsPhone(E.B))
			{
				R->SendPlayFeedback(Killer->Id, TEXT("Knockout!"), FString::Printf(TEXT("+%d point · %s is in the sea · %d KOs"), KnockoutPoints, Victim ? *Victim->Name : TEXT(""), Killer->Knockouts),
					TEXT("success"), TEXT("celebration"), true);
			}
			break;
		}
		case EEventKind::RoundWin:
			if (M.Pucks.IsValidIndex(E.A) && IsPhone(E.A))
			{
				const FPuck& W = M.Pucks[E.A];
				R->SendPlayFeedback(W.Id, TEXT("Round won!"), FString::Printf(TEXT("+%d points · %d of %d round wins"), RoundWinPoints, W.RoundWins, WinsForMatch), TEXT("success"), TEXT("celebration"), true);
			}
			break;
		case EEventKind::PowerPickup:
			if (M.Pucks.IsValidIndex(E.A) && IsPhone(E.A))
			{
				const TCHAR* Name = E.Power == EPower::Heavy ? TEXT("Heavy!") : E.Power == EPower::Shield ? TEXT("Shield!") : TEXT("Turbo!");
				const TCHAR* Detail = E.Power == EPower::Heavy ? TEXT("Hard to move, hits like a truck") : E.Power == EPower::Shield ? TEXT("The next hit bounces off")
					: TEXT("Dash recharges fast");
				R->SendPlayFeedback(M.Pucks[E.A].Id, Name, Detail, TEXT("success"), TEXT("impact"));
			}
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

void ASumoGameMode::SendPrompts()
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	// The motion pad contract: one prompt per phone, in its colour, with the tilt hint and the A label.
	for (int32 i = 0; i < Game->GetSeats().Num(); ++i)
	{
		const FSumoSeat& Seat = Game->GetSeats()[i];
		if (Seat.Kind != ESumoPilot::Phone)
		{
			continue;
		}
		Room->SendPadPrompt(Seat.Id, FString::Printf(TEXT("Sumo Bumpers · round %d"), Game->GetMatch().Round), TEXT("Tilt to roll · A: dash"), TEXT("DASH"), FString(),
			HexOf(Game->ColorOf(i)), TEXT("tilt"));
	}
}

void ASumoGameMode::PushScores(bool bFinal)
{
	URoomHostSubsystem* Room = RoomOf(this);
	if (!Room || !Game)
	{
		return;
	}
	TArray<FMcScoreEntry> Scores;
	FString Key;
	for (const FPuck& P : Game->GetMatch().Pucks)
	{
		if (P.Id.StartsWith(TEXT("house-")))
		{
			continue;
		}
		FMcScoreEntry& Entry = Scores.AddDefaulted_GetRef();
		Entry.PlayerId = P.Id;
		Entry.Score = P.Score;
		Key += FString::Printf(TEXT("%s=%d;"), *P.Id, P.Score);
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

void ASumoGameMode::HandleRoomLobby()
{
	UGameplayStatics::OpenLevel(this, TEXT("L_SunsetHarbor"));
}

void ASumoGameMode::HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId)
{
	if (Game)
	{
		Game->SetPaused(bPaused);
	}
}

void ASumoGameMode::EndPlay(const EEndPlayReason::Type Reason)
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
		Room->OnReturnedToLobby.RemoveDynamic(this, &ASumoGameMode::HandleRoomLobby);
		Room->OnPauseChanged.RemoveDynamic(this, &ASumoGameMode::HandleRoomPause);
	}
	Super::EndPlay(Reason);
}

void ASumoGameMode::SetupTestHooks()
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
