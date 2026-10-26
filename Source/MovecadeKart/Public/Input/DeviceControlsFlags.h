#pragma once

#include "CoreMinimal.h"

/**
 * Whether this run plays with the device it runs on, and where the motion comes from. Parsed once from the command
 * line (Get) or from any string (Parse, for the tests).
 *
 *  - On iOS and Android the device controls are on: the first local seat is the device.
 *  - -NoDeviceControls turns them off anywhere (a phone driving with a pad).
 *  - Not in Shipping: -DeviceControls turns them on on the desktop (the on-screen controls answer the mouse);
 *    -DeviceMotion=sim plays each game's scripted gestures (the simulator's moves, like phone-sim.mjs);
 *    -DeviceMotionReplay=<file> plays a recording back; -DeviceMotionRecord=<file> writes one (on a device, to bring
 *    real motion to Windows). Paths are relative to the project's Saved folder unless absolute.
 */
namespace MovecadeKart::Input
{
	enum class EDeviceMotionSource : uint8
	{
		/** The device's motion sensors (FMotionEvent). */
		Live,
		/** The scheme's scripted gestures. */
		Simulated,
		/** A recording (-DeviceMotionReplay). */
		Replay,
	};

	struct MOVECADEKART_API FDeviceControlsFlags
	{
		bool bEnabled = false;
		EDeviceMotionSource Source = EDeviceMotionSource::Live;
		FString ReplayPath;
		FString RecordPath;

		/** bMobilePlatform: what the platform default is (true on iOS and Android). */
		static FDeviceControlsFlags Parse(const TCHAR* CommandLine, bool bMobilePlatform);
		/** This run's, parsed once. */
		static const FDeviceControlsFlags& Get();
		static bool IsMobilePlatform();
	};
}
