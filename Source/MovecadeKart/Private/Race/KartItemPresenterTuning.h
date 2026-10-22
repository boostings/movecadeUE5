#pragma once

#include "CoreMinimal.h"
#include "Sim/KartItems.h"

/** AKartItemPresenter's timings, sizes and easing curves, shared by its source files. */
namespace MovecadeKart::ItemPresenter
{
	/** MultiplayerPowerups.tsx BOX_SIZE 2 m. The orb is 0.9 of it, the gem 0.42, the halo twice the orb. */
	constexpr double BoxSize = 200.0;
	constexpr double BoxOrbScale = 0.9;
	constexpr double BoxGemScale = 0.42;
	/** A respawned box pops back in over this long. */
	constexpr double BoxPopSeconds = 0.5;
	/** MultiplayerPowerups.tsx: spin 1.5 rad/s, bob 0.1 m at 2.5 rad/s; the gem tumbles 2 rad/s. */
	constexpr double BoxSpinRate = 1.5;
	constexpr double BoxGemSpinRate = 2.0;
	constexpr double BoxBobCm = 10.0;
	constexpr double BoxBobRate = 2.5;

	/** A used item's pop-up over its kart: springs up in 0.35 s, rises over 0.4 s, swells in its last 0.12 s. */
	constexpr double PopupSeconds = 0.9;
	constexpr double PopupSpringSeconds = 0.35;
	constexpr double PopupRiseSeconds = 0.4;
	constexpr double PopupSwellSeconds = 0.12;

	/** A projectile that vanishes within this distance of a recent hit or explosion doesn't also puff out. */
	constexpr double ImpactMemorySeconds = 0.3;
	constexpr double ImpactMemoryRadiusCm = 400.0;
	/** A dead projectile's trail streams are freed once their particles have faded. */
	constexpr double TrailFadeSeconds = 1.5;

	constexpr double BlastSeconds = 0.8;
	/** Each fireball lobe swells to this share of the blast radius. */
	constexpr double BlastLobeScale = 0.38;
	constexpr double StrikeSeconds = 0.35;
	constexpr double StrikeHeight = 1100.0;
	constexpr int32 StrikePool = 4;
	constexpr int32 LightPool = 4;
	/** A flash light rises to its peak over this long, then fades over the rest of its time. */
	constexpr double LightRiseSeconds = 0.05;

	/** The item-driven kart pose (MultiplayerKart.tsx): two turns over a 1 s spin-out, a 2 m arc over 0.8 s. */
	constexpr double SpinOutDegrees = 720.0;
	constexpr double SpinOutSeconds = 1.0;
	constexpr double ExplodePopSeconds = 0.8;
	constexpr double ExplodePopCm = 200.0;
	constexpr double ShrinkScale = 0.5;

	inline double EaseOut(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return 1.0 - FMath::Pow(1.0 - X, 3.0);
	}

	/** 0 -> 1 with a springy overshoot (easeOutBack): the "pop" of boxes and pop-ups. */
	inline double Pop(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		constexpr double C1 = 2.4;
		constexpr double C3 = C1 + 1.0;
		return 1.0 + C3 * FMath::Pow(X - 1.0, 3.0) + C1 * FMath::Pow(X - 1.0, 2.0);
	}

	/** Items that appear in the world as projectiles; the others pop up over the kart that uses them. */
	inline bool IsThrown(EKartItem Item)
	{
		return Item == EKartItem::Shot || Item == EKartItem::Seeker || Item == EKartItem::SkySeeker || Item == EKartItem::Banana || Item == EKartItem::Bomb;
	}
}
