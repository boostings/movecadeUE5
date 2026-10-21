#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ArcadeSession.generated.h"

/** A player's running total across the evening's games. */
USTRUCT()
struct FArcadePartyRow
{
	GENERATED_BODY()

	FString Name;
	FLinearColor Color = FLinearColor::White;
	int32 Points = 0;
	int32 Wins = 0;
	int32 Games = 0;
	/** Points from the latest game, for the "+3" flourish. */
	int32 LastGain = 0;
};

/**
 * The party loop across levels: which arcade game is on, with how many players and house players, so "play again"
 * and "next game" need no trip through the menus; and the party table, a running score across games (3 points
 * for a win, 2 for second, 1 for third), so an evening of short games builds to a champion.
 *
 * In Party Mode the room decides the seats; "next game" asks the room back to its lobby with the next game queued,
 * and the lobby selects it and starts as soon as the phones are ready.
 */
UCLASS()
class MOVECADEKART_API UArcadeSession : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UArcadeSession* Get(const UObject* WorldContext);

	/** Remembers a local launch (the picker, play again, next game). */
	void NoteLaunch(const FString& GameId, int32 Locals, int32 Bots);
	const FString& GetGameId() const { return GameId; }
	int32 GetLocals() const { return Locals; }
	int32 GetBots() const { return Bots; }

	/** Travels (with the curtain) to GameId with the remembered seats, clamped to that game. */
	void LaunchLocal(const UObject* WorldContext, const FString& InGameId);

	/** Adds a finished game to the party table, once per (game, match) key. */
	void RecordResult(const FString& MatchKey, const TArray<FArcadeStanding>& Standings);
	const TArray<FArcadePartyRow>& GetPartyTable() const { return Party; }
	int32 GetGamesPlayed() const { return GamesPlayed; }
	void ResetParty();

	/** Party Mode: the game the lobby should select and start when it next opens (empty: none). */
	void QueueRoomGame(const FString& InGameId) { QueuedRoomGame = InGameId; }
	FString ConsumeQueuedRoomGame();
	bool HasQueuedRoomGame() const { return !QueuedRoomGame.IsEmpty(); }

private:
	FString GameId;
	int32 Locals = 1;
	int32 Bots = 1;
	FString QueuedRoomGame;
	TSet<FString> RecordedKeys;

	UPROPERTY()
	TArray<FArcadePartyRow> Party;

	int32 GamesPlayed = 0;
};
