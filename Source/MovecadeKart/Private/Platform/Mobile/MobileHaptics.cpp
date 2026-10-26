#include "Platform/Mobile/MobileHaptics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Platform/Mobile/MobileDebugFlags.h"
#include "Platform/Mobile/MobilePrefs.h"

namespace MovecadeKart::Mobile
{
	namespace
	{
		// The feel of each kind. iOS maps intensity to the impact weight: light up to ~0.4, medium to ~0.7, then heavy
		// (r.IOS.HapticsKick*); Android vibrates for Duration. Durations stay under MinHapticGapSeconds.
		constexpr FHapticPulse TapPulse{ 0.35f, 0.04f };
		constexpr FHapticPulse ThrowPulse{ 0.55f, 0.06f };
		constexpr FHapticPulse LandingPulse{ 0.6f, 0.07f };
		constexpr FHapticPulse HitPulse{ 0.85f, 0.09f };
		constexpr FHapticPulse BigPulse{ 1.f, 0.11f };
		/** A soft hit still kicks (iOS ignores anything under its light cutoff). */
		constexpr float MinStrengthScale = 0.6f;

		/** The phone has one motor: one throttle for the process, a deliberate session cache. */
		FHapticThrottle& ProcessThrottle()
		{
			static FHapticThrottle Throttle;
			return Throttle;
		}
	}

	FHapticPulse PulseFor(EHaptic Kind, float Strength)
	{
		FHapticPulse Pulse;
		switch (Kind)
		{
		case EHaptic::Tap: Pulse = TapPulse; break;
		case EHaptic::Throw: Pulse = ThrowPulse; break;
		case EHaptic::Landing: Pulse = LandingPulse; break;
		case EHaptic::Hit: Pulse = HitPulse; break;
		case EHaptic::Big: Pulse = BigPulse; break;
		}
		const float Scale = FMath::Lerp(MinStrengthScale, 1.f, FMath::Clamp(Strength, 0.f, 1.f));
		Pulse.Intensity = FMath::Max(TapPulse.Intensity, Pulse.Intensity * Scale);
		return Pulse;
	}

	bool FHapticThrottle::Allow(EHaptic Kind, double Now)
	{
		if (Kind != EHaptic::Big && Now - LastAt < MinHapticGapSeconds)
		{
			return false;
		}
		LastAt = Now;
		return true;
	}

	void FHaptics::Play(APlayerController* PC, EHaptic Kind, float Strength)
	{
		if (!PC || !PC->IsLocalController() || !IsAvailable() || !IsEnabled())
		{
			return;
		}
		if (!ProcessThrottle().Allow(Kind, FPlatformTime::Seconds()))
		{
			return;
		}
		const FHapticPulse Pulse = PulseFor(Kind, Strength);
		PC->PlayDynamicForceFeedback(Pulse.Intensity, Pulse.Duration, true, true, true, true);
	}

	void FHaptics::PlayForLocalPlayer(const UObject* WorldContext, EHaptic Kind, float Strength)
	{
		const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (World)
		{
			Play(World->GetFirstPlayerController(), Kind, Strength);
		}
	}

	bool FHaptics::IsEnabled()
	{
		return GetDefault<UMobilePrefs>()->bHapticsEnabled;
	}

	void FHaptics::SetEnabled(bool bEnabled)
	{
		UMobilePrefs* Prefs = GetMutableDefault<UMobilePrefs>();
		if (Prefs->bHapticsEnabled != bEnabled)
		{
			Prefs->bHapticsEnabled = bEnabled;
			Prefs->Save();
		}
	}

	bool FHaptics::IsAvailable()
	{
		return IsPhoneBehaviourOn();
	}
}
