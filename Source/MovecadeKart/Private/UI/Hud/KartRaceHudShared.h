#pragma once

#include "CoreMinimal.h"
#include "Race/KartRaceManager.h"

class FKartPainter;

/** The race HUD's shared timings and small helpers (SKartRaceHud.cpp, SKartRaceHudView.cpp, SKartRaceHudPanels.cpp). */
namespace KartRaceHud
{
	/** The sim's top speed is 40 SU (m/s); the HUD shows km/h (B12). */
	constexpr double TopSpeedKmh = 144.0;
	/** How long the finish, final-lap and "race over" banners stay up. */
	constexpr double FinishBannerSeconds = 2.6;
	/** The shared minimap's height in four views, as a fraction of the screen height. */
	constexpr double SharedMapFraction = 0.19;
	/** The shared minimap's backing disc radius, as a multiple of the map's height. */
	constexpr double SharedMapDisc = 0.72;
	/** Widgets scale down to 80 % in split screen. */
	constexpr float SplitScale = 0.8f;
	/** "GO!" stays up for the first part of the race. */
	constexpr double GoShownMs = 1200.0;
	/** The time-left warning appears with this much of the race's deadline left. */
	constexpr double DeadlineWarnMs = 30000.0;

	/** The phases with live views: the countdown and the race itself. */
	inline bool IsRacingPhase(EKartRacePhase Phase)
	{
		return Phase == EKartRacePhase::Countdown || Phase == EKartRacePhase::Racing;
	}

	/** A dark pill with a small label and a big value ("TIME 1:23"), placed by Anchor and Align (0..1 of its size). */
	void Chip(const FKartPainter& P, const FVector2D& Anchor, const FVector2D& Align, const FText& Label, const FText& Value,
		const FLinearColor& ValueColor, float Scale);
}
