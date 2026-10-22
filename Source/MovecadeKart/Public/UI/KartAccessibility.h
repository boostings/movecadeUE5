#pragma once

#include "CoreMinimal.h"

/** Colour-vision modes for the accents that carry meaning (good / bad / warning, players on the minimap). */
enum class EKartColorVision : uint8
{
	Standard,
	/** Red-green (protanopia / deuteranopia): good is blue, bad is orange. */
	RedGreen,
	/** Blue-yellow (tritanopia): good is teal, bad is magenta-red, warnings stay away from yellow. */
	BlueYellow,
	Count,
};

/**
 * The player's accessibility options (saved in UKartPlayerPrefs, Settings > Accessibility), read from anywhere
 * through these static accessors. They are cheap (a config object read), so call them where you use them.
 *
 *  - ReduceMotion / MotionScale: tone down screen shake, camera kicks, flashes and bouncy UI. Games multiply
 *    shake amplitudes and flash alphas by MotionScale() (1 normally, 0.25 with reduce motion on).
 *  - UIScale: the menu and HUD scale (0.85 .. 1.25), applied to every Slate widget in the game viewport.
 *  - Good / Bad / Warn: semantic accent colours that stay distinguishable under the chosen colour-vision mode.
 */
struct MOVECADEKART_API FKartAccessibility
{
	static bool ReduceMotion();
	/** 1 normally, 0.25 with reduce motion: multiply shake amplitudes, flash alphas and decorative bounces by it. */
	static float MotionScale();
	static float UIScale();
	static EKartColorVision ColorVision();

	/** "Good" (ready, gained a place, finished): mint, or a blue that reads under red-green colour blindness. */
	static FLinearColor Good();
	/** "Bad" (lost a place, wrong way, error). */
	static FLinearColor Bad();
	/** "Attention" (final lap, time running out). */
	static FLinearColor Warn();

	/** The UI scale steps the settings cycle through. */
	static TConstArrayView<float> UIScaleSteps();

	/** Pushes UIScale into the viewport DPI (call after the prefs change and on start-up). */
	static void ApplyUIScale();
};
