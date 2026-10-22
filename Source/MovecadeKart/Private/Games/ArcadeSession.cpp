#include "Games/ArcadeSession.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Games/ArcadeCatalog.h"
#include "Kismet/GameplayStatics.h"
#include "Store/KartStoreSubsystem.h" // store: unlock progress
#include "UI/KartUISubsystem.h"

UArcadeSession* UArcadeSession::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UArcadeSession>() : nullptr;
}

void UArcadeSession::NoteLaunch(const FString& InGameId, int32 InLocals, int32 InBots)
{
	GameId = InGameId;
	Locals = InLocals;
	Bots = InBots;
}

void UArcadeSession::LaunchLocal(const UObject* WorldContext, const FString& InGameId)
{
	const MovecadeKart::Arcade::FGameInfo* Game = MovecadeKart::Arcade::Find(InGameId);
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!Game || !World)
	{
		return;
	}
	// The seats carry over; a simultaneous game takes one player at the screen, the rest become house players.
	int32 L = Locals, B = Bots;
	if (L > Game->MaxLocal)
	{
		B += L - Game->MaxLocal;
	}
	MovecadeKart::Arcade::ClampSeats(*Game, L, B);
	GameId = Game->Id;
	const FString Url = MovecadeKart::Arcade::TravelUrl(*Game, L, B);
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(World))
	{
		UI->TravelTo(Url);
	}
	else
	{
		UGameplayStatics::OpenLevel(World, FName(*Game->Map), true, FString::Printf(TEXT("Players=%d?Bots=%d"), L, B));
	}
}

void UArcadeSession::RecordResult(const FString& MatchKey, const TArray<FArcadeStanding>& Standings)
{
	if (RecordedKeys.Contains(MatchKey) || Standings.Num() == 0)
	{
		return;
	}
	RecordedKeys.Add(MatchKey);
	++GamesPlayed;
	// Store: free unlocks count finished party games (the key starts with the game id).
	UKartStoreSubsystem::NoteArcadeGame(this, MatchKey.Left(MatchKey.Find(TEXT("-"))), Standings.ContainsByPredicate([](const FArcadeStanding& S) { return S.bHuman; }));
	for (FArcadePartyRow& Row : Party)
	{
		Row.LastGain = 0;
	}
	// A game alone (one seat) still counts as played, but there is nobody to beat.
	const bool bContest = Standings.Num() > 1;
	for (const FArcadeStanding& S : Standings)
	{
		FArcadePartyRow* Row = Party.FindByPredicate([&S](const FArcadePartyRow& R) { return R.Name == S.Name; });
		if (!Row)
		{
			Row = &Party.AddDefaulted_GetRef();
			Row->Name = S.Name;
		}
		Row->Color = S.Color;
		Row->Games += 1;
		const int32 Gain = bContest ? FMath::Max(0, 4 - S.Place) : 0;
		Row->Points += Gain;
		Row->LastGain = Gain;
		Row->Wins += bContest && S.Place == 1 ? 1 : 0;
	}
	Party.StableSort([](const FArcadePartyRow& A, const FArcadePartyRow& B) { return A.Points != B.Points ? A.Points > B.Points : A.Wins > B.Wins; });
}

void UArcadeSession::ResetParty()
{
	Party.Reset();
	RecordedKeys.Reset();
	GamesPlayed = 0;
}

FString UArcadeSession::ConsumeQueuedRoomGame()
{
	FString Out = MoveTemp(QueuedRoomGame);
	QueuedRoomGame.Reset();
	return Out;
}
