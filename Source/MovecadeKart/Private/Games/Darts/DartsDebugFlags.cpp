#include "Games/Darts/DartsDebugFlags.h"

#include "Misc/Parse.h"

FDartsDebugFlags FDartsDebugFlags::Parse(const TCHAR* CommandLine)
{
	FDartsDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	if (CommandLine)
	{
		Flags.bLocalAuto = FParse::Param(CommandLine, TEXT("DartsLocalAuto"));
		Flags.bShotOnBigHit = FParse::Param(CommandLine, TEXT("DartsShotBig"));
	}
#endif
	return Flags;
}
