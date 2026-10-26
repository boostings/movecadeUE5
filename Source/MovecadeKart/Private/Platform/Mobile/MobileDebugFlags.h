#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Platform/Mobile/MobileLifecycle.h"

namespace MovecadeKart::Mobile
{
	/** One scripted step of -MobileSimScript: at At seconds after startup, run Command (see ParseSimCommand). */
	struct FSimStep
	{
		double At = 0.0;
		FString Command;
	};

	/**
	 * The phone-behaviour test hooks (all off in Shipping), parsed once:
	 *   -MobileSim                 desktop behaves like the phone build: the frame cap, the screen-awake policy, haptics
	 *                              (to a gamepad), the boot splash, the status banner and the shader warm-up card.
	 *   -MobileSimScript=10:background,14:foreground,20:network=none,24:network=wifi
	 *                              scripted lifecycle events (implies -MobileSim), for unattended runs and screenshots.
	 * The same commands work in the console: Mobile.Sim background (see ParseSimCommand for the list).
	 */
	struct FMobileDebugFlags
	{
		bool bSimulate = false;
		TArray<FSimStep> Script;

		static FMobileDebugFlags Parse(const TCHAR* CommandLine);
		/** The running game's flags (parsed from FCommandLine on first use). */
		static const FMobileDebugFlags& Get();
	};

	/** What a simulated command does: an app event, or a device network change. */
	struct FSimCommand
	{
		TOptional<EAppEvent> Event;
		TOptional<ENetworkConnectionType> Network;
		/** A fake shader warm-up of this many pipelines (the desktop editor has no PSO cache to precompile). */
		TOptional<uint32> Warmup;
	};

	/**
	 * background | foreground | deactivate | reactivate | interrupt | interrupt-end | focus-lost | focus |
	 * music | music-end | unplug | plug | network=none|wifi|cell|airplane | warmup=N. Unset when Text isn't one of those.
	 */
	FSimCommand ParseSimCommand(const FString& Text);

	/** The phone behaviour is on: an iOS or Android build, or desktop with -MobileSim. */
	bool IsPhoneBehaviourOn();
}
