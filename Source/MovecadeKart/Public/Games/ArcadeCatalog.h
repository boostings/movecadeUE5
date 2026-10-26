#pragma once

#include "CoreMinimal.h"

/**
 * The party games, in one place: the Arcade picker, the Party lobby's game pills, the room's level for each game
 * and the "next game" button all read this list (Docs/Port/05-games-framework.md).
 */
namespace MovecadeKart::Arcade
{
	/**
	 * Which way up a screen is meant to be held on a phone or tablet. Desktop windows don't rotate; this is what
	 * UMovecadeOrientationSubsystem (Mobile/MovecadeOrientationSubsystem.h) locks the device to while a level runs.
	 */
	enum class EArcadeOrientation : uint8
	{
		/** Wide, either landscape side (follows the rotation sensor between the two). */
		Landscape,
		/** Tall and upright (home indicator at the bottom; upside down isn't offered on notched phones). */
		Portrait,
		/** Whatever way the player holds it: the layout must work both ways. */
		Any,
	};

	MOVECADEKART_API const TCHAR* LexToString(EArcadeOrientation Orientation);
	/** "landscape", "portrait", "any" (any case); false leaves Out untouched. */
	MOVECADEKART_API bool LexFromString(EArcadeOrientation& Out, const TCHAR* Text);

	/** Kart races: every track level that isn't a party game's. */
	inline constexpr EArcadeOrientation KartOrientation = EArcadeOrientation::Landscape;
	/**
	 * The front end and the Party lobby (staged on a track level, so this matches KartOrientation). Landscape, not
	 * Any: the screens are laid out wide over the track flyover, and the races and most games are wide too, so the
	 * common menu -> game hops never rotate the phone.
	 */
	inline constexpr EArcadeOrientation MenuOrientation = EArcadeOrientation::Landscape;

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
		/** How a phone running this game as the host screen is held (aim-down-the-lane games are tall). */
		EArcadeOrientation Orientation = EArcadeOrientation::Landscape;
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
	/** The orientation a level is played in: its party game's, else KartOrientation (track levels: races and menus). */
	MOVECADEKART_API EArcadeOrientation OrientationForLevel(const FString& Level);
}
