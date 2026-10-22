#include "Games/Fishing/FishingDebugFlags.h"

#include "Misc/Parse.h"

FFishingDebugFlags FFishingDebugFlags::Parse(const TCHAR* CommandLine)
{
	FFishingDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	if (!CommandLine)
	{
		return Flags;
	}
	Flags.bDemo = FParse::Param(CommandLine, TEXT("FishingDemo"));
	if (double Seconds = 0.0; FParse::Value(CommandLine, TEXT("FishingClock="), Seconds) && Seconds > 0.0)
	{
		Flags.RoundSeconds = Seconds;
	}
	if (uint32 Seed = 0; FParse::Value(CommandLine, TEXT("FishingSeed="), Seed))
	{
		Flags.Seed = Seed;
	}
#endif
	return Flags;
}
