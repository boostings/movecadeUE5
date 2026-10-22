#pragma once

#include "CoreMinimal.h"

/**
 * The party games, in one place: the Arcade picker, the Party lobby's game pills, the room's level for each game
 * and the "next game" button all read this list (Docs/Port/05-games-framework.md).
 */
namespace MovecadeKart::Arcade
{
	struct FGameInfo
	{
		/** The room's gameId ("bowling", "darts", ...). */
		FString Id;
		FText Name;
		FText Tagline;
		/** One line for the pause screen's "How to play". */
		FText HowTo;
		/** The level (L_Bowling, ...). */
		FString Map;
		FLinearColor Accent = FLinearColor::White;
		int32 MaxPlayers = 4;
		/** Players on one screen: turn-based games share the controls; simultaneous ones need a pair of hands each. */
		int32 MaxLocal = 4;
		/** Seconds of the game's own results before the "what next" bar slides in (a showcase can ask for more). */
		double ResultsDelay = 2.0;
		/** The lobby's line about what the phone does in this game. */
		FText PhoneHint;
	};

	MOVECADEKART_API const TArray<FGameInfo>& Games();
	MOVECADEKART_API const FGameInfo* Find(const FString& Id);
	/** By level name (with or without a path or options). */
	MOVECADEKART_API const FGameInfo* FindByMap(const FString& Level);
	/** The game after Id in the list, wrapping. */
	MOVECADEKART_API const FGameInfo& NextAfter(const FString& Id);
	/** "L_Darts?Players=1?Bots=2", clamped to the game's limits. */
	MOVECADEKART_API FString TravelUrl(const FGameInfo& Game, int32 Locals, int32 Bots);
	/** Players and house players clamped to the game: at least one seat, Locals <= MaxLocal, total <= MaxPlayers. */
	MOVECADEKART_API void ClampSeats(const FGameInfo& Game, int32& Locals, int32& Bots);
}
