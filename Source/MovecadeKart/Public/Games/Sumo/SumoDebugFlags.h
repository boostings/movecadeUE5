#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

/**
 * Sumo Bumpers' own test flags, parsed once by ASumoGameMode (the shared -SumoPlayers= -SumoBots= -SumoAuto are
 * FArcadeLaunchOptions'). They change the match, so Shipping never reads them (Parse returns the defaults).
 *
 *   -SumoSkill=0..1   every local house bot at this skill (soak tests); unset keeps the mixed line-up.
 *   -SumoSeed=N       the match seed (repeatable screenshots); unset keeps the clock or room-match seed.
 */
struct FSumoDebugFlags
{
	/** < 0: the house line-up's own skills. */
	float BotSkill = -1.f;
	TOptional<uint32> Seed;

	static FSumoDebugFlags Parse(const TCHAR* CommandLine);
};
