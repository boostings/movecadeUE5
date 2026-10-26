#pragma once

#include "CoreMinimal.h"

class APlayerController;

namespace MovecadeKart::Mobile
{
	/** The few kinds of buzz the game uses; each has one feel (PulseFor). */
	enum class EHaptic : uint8
	{
		/** A light tick: a UI confirm, a small bump. */
		Tap,
		/** Something leaves the player's hand: a ball, a dart, a kart item. */
		Throw,
		/** Something lands or settles: a dart in the board, a block on the tower. */
		Landing,
		/** The player is hit: a wall, a kart, a shell. */
		Hit,
		/** A big moment: a strike, a perfect drop, an explosion. */
		Big,
	};

	/** One force feedback pulse. On iOS the intensity picks the impact weight (light, medium, heavy). */
	struct FHapticPulse
	{
		float Intensity = 0.f;
		float Duration = 0.f;
	};

	/** The pulse for Kind at Strength (0..1, how hard the hit was); Strength never makes it weaker than a light tick. */
	MOVECADEKART_API FHapticPulse PulseFor(EHaptic Kind, float Strength = 1.f);

	/** Pulses closer than this merge (iOS needs the motor back at rest between kicks; a wall scrape is one buzz). */
	constexpr double MinHapticGapSeconds = 0.12;

	/** Drops pulses that come too fast; a Big one always gets through. */
	class MOVECADEKART_API FHapticThrottle
	{
	public:
		bool Allow(EHaptic Kind, double Now);

	private:
		double LastAt = -1000.0;
	};

	/**
	 * The one way game code makes the phone vibrate: UE force feedback (APlayerController::PlayDynamicForceFeedback),
	 * which the engine turns into the iPhone's Taptic Engine impacts or Android vibration (the Android build needs the
	 * VIBRATE permission). Gamepads rumble through the same path. Off when the player turned haptics off
	 * (UMobilePrefs::bHapticsEnabled), and off on desktop unless -MobileSim (desktop play keeps its old feel).
	 *
	 *     FHaptics::Play(PC, EHaptic::Throw);
	 *     FHaptics::PlayForLocalPlayer(this, EHaptic::Landing, 0.6f);
	 */
	class MOVECADEKART_API FHaptics
	{
	public:
		static void Play(APlayerController* PC, EHaptic Kind, float Strength = 1.f);
		/** The first local player of WorldContext's world (single-screen phone play). */
		static void PlayForLocalPlayer(const UObject* WorldContext, EHaptic Kind, float Strength = 1.f);

		/** The player's switch. */
		static bool IsEnabled();
		/** Changes and saves the switch. */
		static void SetEnabled(bool bEnabled);
		/** This build vibrates at all: a phone, or desktop with -MobileSim. */
		static bool IsAvailable();
	};
}
