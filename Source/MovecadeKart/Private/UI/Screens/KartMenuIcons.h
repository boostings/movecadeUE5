#pragma once

#include "CoreMinimal.h"

class FKartPainter;

/**
 * The big flat-shaded card icons of the main menu and the Arcade picker, drawn from primitives (no art).
 * Each one is centred on Centre and spans about two Size units across; Now drives its idle animation.
 */
namespace KartMenuIcons
{
	using FPaintIcon = void (*)(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);

	// Main menu entries.
	/** Race: a waving chequered flag on a pole. */
	void RaceFlag(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Party Mode: two phones with a signal arc. */
	void PartyPhones(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Settings: a turning gear. */
	void SettingsGear(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Credits: a spinning star. */
	void CreditsStar(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Quit: a door with an arrow. */
	void QuitDoor(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);

	// Party games (also Arcade on the main menu).
	/** Bowling: a ball rolling into three pins. */
	void BowlingPins(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Bullseye Bash: a dartboard with a dart in it. */
	void Dartboard(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Samurai Slice: a fruit falling apart under a blade stroke. */
	void SlicedFruit(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Sky Stacker: a swaying tower of blocks. */
	void BlockTower(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Sumo Bumpers: two bumper pucks colliding on a ring, sparks between them. */
	void BumperPucks(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Home Run Derby: a bat swinging through a ball that streaks away. */
	void BatAndBall(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);
	/** Fishing Frenzy: a bobber dipping on the ripples, its line up out of frame. */
	void Bobber(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);

	/** The card icon for a party game by catalog id; the block tower for a game without its own art yet. */
	FPaintIcon ForArcadeGame(FName GameId);
}
