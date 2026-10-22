#pragma once

#include "CoreMinimal.h"

/**
 * Bullseye Bash's own command-line test hooks, parsed once by ADartsGameMode (Tools/ue/play.ps1 -Extra passes them).
 * All off in Shipping. The seat flags (-DartsPlayers=, -DartsBots=, -DartsAuto) and -KartShots= / -KartQuitAfter=
 * belong to AArcadeGameModeBase.
 *
 *   -DartsLocalAuto  the local thrower plays through its real controls (grip, the meter, a release in the sweet
 *                    spot), so a run can drive the local path to the results.
 *   -DartsShotBig    a UI screenshot just after each big hit (a bounty or the bull) lands: the punch and the flash.
 */
struct FDartsDebugFlags
{
	bool bLocalAuto = false;
	bool bShotOnBigHit = false;

	static FDartsDebugFlags Parse(const TCHAR* CommandLine);
};
