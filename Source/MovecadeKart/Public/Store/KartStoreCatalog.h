#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreTypes.h"

/**
 * The one list of cosmetics. Def ids match Docs/Store/itemdefs.json (the file uploaded to Steamworks):
 *   1xx kart paints, 2xx colourways, 3xx headwear, 4xx bowling balls, 5xx katanas, 6xx stacker blocks,
 *   7xx victory emotes, 9xx bundles and the playtime generator.
 * x50..x59 are earned unlocks and x60..x69 playtime drops; the rest are paid.
 */
namespace MovecadeKart::Store
{
	/** The playtime drop generator's def id (type "playtimegenerator"). */
	constexpr int32 PlaytimeGeneratorDef = 990;

	/** Play stats the earned unlocks count. */
	namespace Stat
	{
		MOVECADEKART_API extern const FName ArcadeGames;
		MOVECADEKART_API extern const FName RacesFinished;
		MOVECADEKART_API extern const FName RacesWon;
		MOVECADEKART_API extern const FName BowlingGames;
		MOVECADEKART_API extern const FName SamuraiGames;
		MOVECADEKART_API extern const FName StackerGames;
	}

	MOVECADEKART_API const TArray<FItem>& Catalog();
	MOVECADEKART_API const FItem* FindItem(int32 DefId);
	MOVECADEKART_API const FItem* FindItem(FName Key);
	/** The stat a party game id counts towards (bowling -> BowlingGames), or NAME_None. */
	MOVECADEKART_API FName GameStat(const FString& GameId);
	/** The drop-only items the playtime generator can grant. */
	MOVECADEKART_API TArray<int32> DropPool();
	/** Docs/Store/itemdefs.json, generated from the catalog (the test checks the file matches). */
	MOVECADEKART_API FString MakeItemDefsJson(int32 AppId);
}
