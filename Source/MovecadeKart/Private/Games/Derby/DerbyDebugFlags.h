#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

/**
 * Home Run Derby's test and debug command-line flags, parsed once by ADerbyGameMode. The shared ones come from
 * AArcadeGameModeBase (FArcadeLaunchOptions, FArcadeTestHooks):
 *   -DerbyPlayers=N -DerbyBots=N   local batters and house bots (over the travel URL's ?Players=&Bots=)
 *   -DerbyAuto                     the local seats bat as bots (soak tests, screenshots)
 *   -KartShots=t1,t2 -KartQuitAfter=N
 * and this one:
 *   -DerbySeed=N                   a fixed pitch schedule (and so a repeatable run with bots) instead of the clock's
 * Flags that change gameplay are compiled out of Shipping.
 */
struct FDerbyDebugFlags
{
	/** -DerbySeed=: the local match seed. Unset in Shipping. A room match always uses the room's seed. */
	TOptional<uint32> Seed;

	static FDerbyDebugFlags Parse(const TCHAR* CommandLine)
	{
		FDerbyDebugFlags Flags;
#if !UE_BUILD_SHIPPING
		uint32 Value = 0;
		if (CommandLine && FParse::Value(CommandLine, TEXT("DerbySeed="), Value))
		{
			Flags.Seed = Value;
		}
#endif
		return Flags;
	}
};
