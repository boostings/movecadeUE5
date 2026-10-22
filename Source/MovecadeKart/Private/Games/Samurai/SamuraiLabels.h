#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"

/** The round's own words (notes and banners), each built once; the HUD keeps its labels next to where it paints them. */
namespace MovecadeKart::Samurai::Labels
{
	inline const FText& SwingFaster()
	{
		return ARCADE_LABEL("Samurai", "TooSlow", "Swing faster!");
	}

	inline const FText& Missed()
	{
		return ARCADE_LABEL("Samurai", "Whiff", "Missed");
	}

	inline const FText& Dropped()
	{
		return ARCADE_LABEL("Samurai", "Dropped", "Dropped");
	}

	inline const FText& CleanCut()
	{
		return ARCADE_LABEL("Samurai", "PracticeCut", "Clean cut!");
	}

	inline const FText& FocusShout()
	{
		return ARCADE_LABEL("Samurai", "FocusShout", "FOCUS!");
	}

	/** "{0}x COMBO!" */
	inline const FText& ComboShout()
	{
		return ARCADE_LABEL("Samurai", "ComboShout", "{0}x COMBO!");
	}
}
