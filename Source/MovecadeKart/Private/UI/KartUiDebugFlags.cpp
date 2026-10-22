#include "UI/KartUiDebugFlags.h"

#include "Misc/Parse.h"

FKartUiDebugFlags FKartUiDebugFlags::Parse(const TCHAR* CommandLine)
{
	FKartUiDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	if (!CommandLine)
	{
		return Flags;
	}
	FString Shots;
	// The whole comma list, not just up to the first separator.
	if (FParse::Value(CommandLine, TEXT("KartUiShots="), Shots, /*bShouldStopOnSeparator*/ false))
	{
		Flags.bTour = true;
		TArray<FString> Steps;
		Shots.ParseIntoArray(Steps, TEXT(","));
		for (const FString& Step : Steps)
		{
			Flags.TourSteps.Add(FName(*Step.TrimStartAndEnd()));
		}
	}
	Flags.bForcePad = FParse::Param(CommandLine, TEXT("KartUiPad"));
#endif
	return Flags;
}
