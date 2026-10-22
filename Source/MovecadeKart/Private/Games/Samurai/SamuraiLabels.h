#pragma once

#include "CoreMinimal.h"

/** The round's own words (notes and banners), each built once; the HUD keeps its labels next to where it paints them. */
namespace MovecadeKart::Samurai::Labels
{
	inline const FText& SwingFaster()
	{
		static const FText Label = NSLOCTEXT("Samurai", "TooSlow", "Swing faster!");
		return Label;
	}

	inline const FText& Missed()
	{
		static const FText Label = NSLOCTEXT("Samurai", "Whiff", "Missed");
		return Label;
	}

	inline const FText& Dropped()
	{
		static const FText Label = NSLOCTEXT("Samurai", "Dropped", "Dropped");
		return Label;
	}

	inline const FText& CleanCut()
	{
		static const FText Label = NSLOCTEXT("Samurai", "PracticeCut", "Clean cut!");
		return Label;
	}

	inline const FText& FocusShout()
	{
		static const FText Label = NSLOCTEXT("Samurai", "FocusShout", "FOCUS!");
		return Label;
	}

	/** "{0}x COMBO!" */
	inline const FText& ComboShout()
	{
		static const FText Pattern = NSLOCTEXT("Samurai", "ComboShout", "{0}x COMBO!");
		return Pattern;
	}
}
