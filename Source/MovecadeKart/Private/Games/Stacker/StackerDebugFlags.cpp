#include "Games/Stacker/StackerDebugFlags.h"

#include "Misc/Parse.h"

FStackerDebugFlags FStackerDebugFlags::Parse(const TCHAR* CommandLine)
{
	FStackerDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	if (CommandLine)
	{
		Flags.bLeanTest = FParse::Param(CommandLine, TEXT("StackerLeanTest"));
	}
#endif
	return Flags;
}
