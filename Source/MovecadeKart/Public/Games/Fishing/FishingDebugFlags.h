#pragma once

#include "CoreMinimal.h"

/**
 * Fishing Frenzy's test and debug switches, parsed once from the command line by AFishingGameMode and handed to
 * AFishingGame::Setup. Every one of them changes gameplay, so none is read in Shipping (Parse returns the defaults).
 *
 *   -FishingDemo      the first angler lands a trophy goldie 0.3 s into the round (screenshots of the record reveal)
 *   -FishingClock=N   an N-second round instead of three minutes (short test runs)
 *   -FishingSeed=N    a fixed match seed, so a run's fish are the same every time
 *
 * -FishingPlayers=, -FishingBots= and -FishingAuto (the local seat fishes by itself) are the arcade framework's
 * launch options (FArcadeLaunchOptions, CommandLinePrefix "Fishing"), parsed by AArcadeGameModeBase.
 */
struct MOVECADEKART_API FFishingDebugFlags
{
	bool bDemo = false;
	/** The round's length in seconds; 0 keeps the rules' RoundDurationMs. */
	double RoundSeconds = 0.0;
	TOptional<uint32> Seed;

	static FFishingDebugFlags Parse(const TCHAR* CommandLine);
};
