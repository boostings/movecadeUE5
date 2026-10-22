#pragma once

#include "CoreMinimal.h"
#include "Games/Darts/DartsPub.h"
#include "Games/Darts/DartsRules.h"

/**
 * The presentation tunables ADartsGame's files (DartsGame*.cpp), the HUD and the game mode share, and the small
 * easing helpers they all use. Values used by one file only live at the top of that file. None of these touch the
 * rules: DartsRules decides every dart.
 */
namespace MovecadeKart::Darts::Tuning
{
	/** A house bot's first dart after the board passes to it, and its next darts (the old 1.4 s / 1.15 s dragged). */
	constexpr double BotFirstDelaySeconds = 0.95;
	constexpr double BotNextDelaySeconds = 0.85;
	/** A bot draws its arm back over this long before it throws (the pose, and the camera leaving the reaction). */
	constexpr double BotWindupSeconds = 0.55;

	/** The meter reads "in the sweet spot" (mint, "NOW!") within this much of SweetSpot. */
	constexpr double SweetBand = 0.05;

	/** Hermite smoothstep of X between A and B. */
	inline double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Perlin's smootherstep of T in 0..1. */
	inline double SmootherStep(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * T * (T * (6.0 * T - 15.0) + 10.0);
	}

	/** Web world (metres) to UE cm. */
	inline FVector ToWorld(const FVector& Web)
	{
		return ADartsPub::ToWorld(Web);
	}

	/** The board index of a sector's value (SectorCount when it isn't on the board). */
	inline int32 SectorIndexOfValue(int32 Value)
	{
		int32 Index = 0;
		while (Index < SectorCount && Sectors()[Index] != Value)
		{
			++Index;
		}
		return Index;
	}

	/** True when Power is in the sweet band. */
	inline bool IsSweet(double Power)
	{
		return FMath::Abs(Power - SweetSpot) < SweetBand;
	}
}
