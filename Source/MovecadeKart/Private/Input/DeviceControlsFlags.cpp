#include "Input/DeviceControlsFlags.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace MovecadeKart::Input
{
	namespace
	{
		FString ResolvePath(const FString& Path)
		{
			return FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectSavedDir(), Path) : Path;
		}
	}

	bool FDeviceControlsFlags::IsMobilePlatform()
	{
		return PLATFORM_IOS || PLATFORM_ANDROID;
	}

	FDeviceControlsFlags FDeviceControlsFlags::Parse(const TCHAR* CommandLine, bool bMobilePlatform)
	{
		FDeviceControlsFlags Flags;
		Flags.bEnabled = bMobilePlatform;
		if (!CommandLine)
		{
			return Flags;
		}
#if !UE_BUILD_SHIPPING
		if (FParse::Param(CommandLine, TEXT("DeviceControls")))
		{
			Flags.bEnabled = true;
		}
		FString Source;
		if (FParse::Value(CommandLine, TEXT("DeviceMotion="), Source) && Source.Equals(TEXT("sim"), ESearchCase::IgnoreCase))
		{
			Flags.Source = EDeviceMotionSource::Simulated;
		}
		FString Path;
		if (FParse::Value(CommandLine, TEXT("DeviceMotionReplay="), Path) && !Path.IsEmpty())
		{
			Flags.Source = EDeviceMotionSource::Replay;
			Flags.ReplayPath = ResolvePath(Path);
		}
		if (FParse::Value(CommandLine, TEXT("DeviceMotionRecord="), Path) && !Path.IsEmpty())
		{
			Flags.RecordPath = ResolvePath(Path);
		}
		// A simulated or replayed motion is a request to play with it.
		Flags.bEnabled |= Flags.Source != EDeviceMotionSource::Live;
#endif
		if (FParse::Param(CommandLine, TEXT("NoDeviceControls")))
		{
			Flags.bEnabled = false;
		}
		return Flags;
	}

	const FDeviceControlsFlags& FDeviceControlsFlags::Get()
	{
		static const FDeviceControlsFlags Flags = Parse(FCommandLine::Get(), IsMobilePlatform());
		return Flags;
	}
}
