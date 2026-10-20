#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

/**
 * The UI's design tokens: one palette, one type scale, the motion curves and the layout metrics every
 * screen and the HUD share. Screens are designed on a 1920 x 1080 canvas; Slate's DPI curve scales it.
 */
namespace KartUI
{
	namespace Color
	{
		/** Deep indigo: dark panels, outlines and the dark side of text shadows. */
		MOVECADEKART_API extern const FLinearColor Night;
		MOVECADEKART_API extern const FLinearColor NightSoft;
		/** Text on light cards. */
		MOVECADEKART_API extern const FLinearColor Navy;
		MOVECADEKART_API extern const FLinearColor NavyDim;
		MOVECADEKART_API extern const FLinearColor White;
		/** Card fill. */
		MOVECADEKART_API extern const FLinearColor Paper;
		MOVECADEKART_API extern const FLinearColor PaperShade;
		/** Primary: the focus colour (sunny yellow) and its orange edge. */
		MOVECADEKART_API extern const FLinearColor Sun;
		MOVECADEKART_API extern const FLinearColor SunDeep;
		MOVECADEKART_API extern const FLinearColor Sky;
		MOVECADEKART_API extern const FLinearColor SkyDeep;
		MOVECADEKART_API extern const FLinearColor Hot;
		MOVECADEKART_API extern const FLinearColor Mint;
		MOVECADEKART_API extern const FLinearColor Grape;
		MOVECADEKART_API extern const FLinearColor Gold;
		MOVECADEKART_API extern const FLinearColor Silver;
		MOVECADEKART_API extern const FLinearColor Bronze;
		/** Drop shadows. */
		MOVECADEKART_API extern const FLinearColor Shadow;

		/** 1st gold, 2nd silver, 3rd bronze, then sky. */
		MOVECADEKART_API FLinearColor ForPlace(int32 Place);
		/** Navy or white, whichever reads on Background. */
		MOVECADEKART_API FLinearColor TextOn(const FLinearColor& Background);
		MOVECADEKART_API FLinearColor WithAlpha(const FLinearColor& Color, float Alpha);
	}

	enum class EFont : uint8
	{
		/** Lilita One: the logo, places, the countdown, banners. */
		Display,
		/** Fredoka Bold: titles and big labels. */
		Title,
		/** Fredoka SemiBold: buttons and option values. */
		Button,
		/** Nunito Black: chips, small caps labels. */
		Label,
		/** Nunito ExtraBold: body text. */
		Body,
	};

	/** A font from the type set at Size (Slate units), optionally with an outline. */
	MOVECADEKART_API FSlateFontInfo Font(EFont Face, float Size, float OutlineSize = 0.f, const FLinearColor& OutlineColor = Color::Night);
	/** True once the /Game/UI/Fonts faces loaded (otherwise Font() falls back to the engine's Roboto). */
	MOVECADEKART_API bool HasGameFonts();

	/** Motion curves on t in [0, 1]. */
	namespace Ease
	{
		MOVECADEKART_API float OutCubic(float T);
		MOVECADEKART_API float InCubic(float T);
		MOVECADEKART_API float InOutSine(float T);
		/** Overshoots by Overshoot, the classic pop-in. */
		MOVECADEKART_API float OutBack(float T, float Overshoot = 1.70158f);
		MOVECADEKART_API float OutElastic(float T);
		/** 0 -> 1 -> 0 over the interval. */
		MOVECADEKART_API float Pulse(float T);
		/** Normalised progress of Now within [Start, Start + Duration]. */
		MOVECADEKART_API float Progress(double Now, double Start, double Duration);
	}

	namespace Metric
	{
		/** The reference canvas. */
		constexpr float CanvasW = 1920.f;
		constexpr float CanvasH = 1080.f;
		/** Action-safe margin for a TV at couch distance, as a fraction of each axis. */
		constexpr float SafeFraction = 0.05f;
		constexpr float Radius = 18.f;
		constexpr float RadiusLarge = 28.f;
		constexpr float Pad = 24.f;
	}
}
