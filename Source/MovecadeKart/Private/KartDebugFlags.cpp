#include "KartDebugFlags.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	/** "a,b,c" into numbers; false unless there are at least MinCount. */
	bool ParseNumbers(const FString& Text, int32 MinCount, TArray<double>& Out)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","));
		Out.Reset();
		for (const FString& Part : Parts)
		{
			Out.Add(FCString::Atod(*Part));
		}
		return Out.Num() >= MinCount;
	}
}

const FKartDebugFlags& FKartDebugFlags::Get()
{
	// The command line is fixed for the life of the process, so it is read once.
	static const FKartDebugFlags Flags = Parse(FCommandLine::Get());
	return Flags;
}

FKartDebugFlags FKartDebugFlags::Parse(const TCHAR* Cmd)
{
	FKartDebugFlags Flags;
	FString Value;
	TArray<double> Numbers;

	if (FParse::Value(Cmd, TEXT("KartTrack="), Value) && !Value.IsEmpty())
	{
		Flags.Track = Value;
	}
	Flags.bWebFeel = FParse::Param(Cmd, TEXT("KartWebFeel"));
	Flags.bNoLife = FParse::Param(Cmd, TEXT("KartNoLife"));

#if !UE_BUILD_SHIPPING
	Flags.bCinematic = FParse::Param(Cmd, TEXT("KartCinematic"));
	if (FParse::Value(Cmd, TEXT("KartCloseCam="), Value, /*bShouldStopOnSeparator*/ false) && ParseNumbers(Value, 3, Numbers))
	{
		Flags.CloseCam = FVector4(Numbers[0], Numbers[1], Numbers[2], Numbers.Num() > 3 ? Numbers[3] : 0.0);
	}
	Flags.bDebugGrip = FParse::Param(Cmd, TEXT("KartDebugGrip"));
	if (FParse::Value(Cmd, TEXT("KartVehicle="), Value))
	{
		Flags.Vehicle = Value;
	}
	if (FParse::Value(Cmd, TEXT("KartOutfit="), Value))
	{
		Flags.Outfit = Value;
	}

	if (FParse::Value(Cmd, TEXT("KartTrackCam="), Value, /*bShouldStopOnSeparator*/ false) && ParseNumbers(Value, 6, Numbers) && Numbers.Num() == 6)
	{
		Flags.bTrackCam = true;
		Flags.TrackCamFrom = FVector(Numbers[0], Numbers[1], Numbers[2]);
		Flags.TrackCamLookAt = FVector(Numbers[3], Numbers[4], Numbers[5]);
	}
	if (FParse::Value(Cmd, TEXT("KartLookUE="), Value, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Pairs;
		Value.ParseIntoArray(Pairs, TEXT(","));
		for (const FString& Pair : Pairs)
		{
			FString Key, Number;
			if (Pair.Split(TEXT("="), &Key, &Number))
			{
				Flags.LookOverrides.Add(Key.TrimStartAndEnd(), FCString::Atod(*Number));
			}
		}
	}
	Flags.bSkyRealtime = FParse::Param(Cmd, TEXT("KartSkyRealtime"));
	Flags.bLifeDemo = FParse::Param(Cmd, TEXT("KartLifeDemo"));
	Flags.bFieldDebug = FParse::Param(Cmd, TEXT("KartFieldDebug"));

	FString Beat;
	Flags.bFxShowcase = FParse::Param(Cmd, TEXT("KartFxShowcase")) || FParse::Value(Cmd, TEXT("KartFxShowcase="), Beat);
	Flags.FxShowcaseBeat = Beat.ToLower();
	Flags.bStepTrace = FParse::Param(Cmd, TEXT("KartStepTrace"));
#endif
	return Flags;
}
