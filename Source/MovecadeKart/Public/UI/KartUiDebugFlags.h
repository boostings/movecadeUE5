#pragma once

#include "CoreMinimal.h"

/**
 * The UI's test and debug command-line flags, read in one place:
 *   -KartUiShots=title,menu,...  the screenshot tour (UKartUISubsystem drives it; the boot decision opens the menus for it)
 *   -KartUiPad                   button hints show gamepad glyphs whatever was pressed (for the tour's -Pad pass)
 * In Shipping builds nothing is read: Parse always returns the empty set, so no flag can change what players see.
 */
struct MOVECADEKART_API FKartUiDebugFlags
{
	/** -KartUiShots was given (even with no steps). */
	bool bTour = false;
	/** The tour's steps, in order. */
	TArray<FName> TourSteps;
	/** -KartUiPad. */
	bool bForcePad = false;

	/** Reads the flags from CommandLine (pure: tests pass their own). */
	static FKartUiDebugFlags Parse(const TCHAR* CommandLine);
};
