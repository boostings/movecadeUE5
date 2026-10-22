#pragma once

#include "CoreMinimal.h"

/**
 * Sky Stacker's test and debug command-line flags, parsed once when a round is set up (AStackerGame::Setup).
 *
 *   -StackerLeanTest   the local builder's palm tips hard right from ten seconds into play, to see the lean coach,
 *                      the danger gauge and the topple in screenshots.
 *
 * The seat flags (-StackerPlayers=, -StackerBots=, -StackerAuto) belong to the arcade base (FArcadeLaunchOptions),
 * and the capture hooks (-KartShots=, -KartQuitAfter=) to FArcadeTestHooks. Every flag here changes gameplay, so
 * Shipping never reads them: Parse returns the defaults.
 */
struct FStackerDebugFlags
{
	bool bLeanTest = false;

	static FStackerDebugFlags Parse(const TCHAR* CommandLine);
};
