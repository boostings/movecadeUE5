#pragma once

#include "CoreMinimal.h"
#include "Sim/KartSim.h"

/**
 * Every tuning number of the item world, in web units, grouped by the phase that uses it, with the web source
 * it came from. Port-parity values: changing one changes the shipped game (and the determinism fingerprint).
 */
namespace MovecadeKart::ItemTuning
{
	constexpr double ItemDt = FKartSim::StepSeconds;

	struct FTimedBoost
	{
		double Multiplier;
		double Seconds;
	};

	// ---- Using an item (useItemAction.ts:35-185) ----
	constexpr FTimedBoost NitroBoost = { 2.0, 2.0 };
	constexpr FTimedBoost TripleNitroBoost = { 2.0, 5.0 };
	/** The Prism's boost; it is invincible for as long. */
	constexpr FTimedBoost PrismBoost = { 1.5, 8.0 };
	/** The Rocket's boost; it is invincible and on autopilot for as long. */
	constexpr FTimedBoost RocketBoost = { 2.5, 6.0 };
	/** Lightning shrinks and slows every other kart for this long. */
	constexpr FTimedBoost LightningSlow = { 0.5, 5.0 };
	constexpr double LightningSpeedKeep = 0.3;
	constexpr double InkSeconds = 4.0;
	/** Thrown items spawn this far ahead of the kart; bananas drop as far behind. */
	constexpr double ThrowOffset = 2.0;
	constexpr double DropOffset = -2.0;
	constexpr double ShotSpeed = 80.0;
	constexpr double SeekerSpeed = 90.0;
	constexpr double SkySeekerSpeed = 100.0;
	constexpr double SkySeekerLaunchHeight = 0.5;
	constexpr double BombThrowSpeed = 30.0;
	constexpr double BombLaunchHeight = 1.0;
	constexpr double BombLaunchUpSpeed = 15.0;

	// ---- Projectiles in flight (ItemPhysics.ts, MultiplayerItems.tsx) ----
	constexpr double ShotRetention = 0.75;
	constexpr int32 ShotMaxBounces = 5;
	/** calculateHomingDirection's per-step blend toward the target. */
	constexpr double SeekerHomingRate = 0.08;
	constexpr double SeekerRetention = 0.8;
	constexpr int32 SeekerMaxBounces = 8;
	constexpr double SkySeekerHomingRate = 0.12;
	constexpr double SkySeekerRetention = 0.9;
	constexpr int32 SkySeekerMaxBounces = 10;
	/** The Sky Seeker cruises at this height until it is within SkySeekerDiveDistance, then dives. */
	constexpr double SkySeekerCruiseHeight = 4.0;
	constexpr double SkySeekerDiveDistance = 20.0;
	/** While diving its target height is distance / SkySeekerDiveSlope, never below the launch height. */
	constexpr double SkySeekerDiveSlope = 5.0;
	constexpr double SkySeekerClimbRate = 0.05;
	constexpr double SkySeekerExplosionRadius = 10.0;
	/** calculateBombArc: gravity, and the height at which a bomb lands. */
	constexpr double BombGravity = -20.0;
	constexpr double BombGroundHeight = 0.5;
	constexpr double BombRetention = 0.5;
	/** A landed bomb explodes once it has flown this long. */
	constexpr double BombArmSeconds = 0.5;
	constexpr double BombExplosionRadius = 8.0;
	/** Walls stand this tall; projectiles probe them this far above their own height. */
	constexpr double BarrierHeight = 1.3;
	constexpr double WallProbeLift = 0.3;
	/** calculateShellBounce: the probe reaches this far past the step's travel, at least BounceMinProbe. */
	constexpr double BounceProbeLead = 0.3;
	constexpr double BounceMinProbe = 0.8;
	constexpr double BouncePushOut = 0.3;

	// ---- Hits (00-PLAN.md "better powerups") ----
	constexpr double SpinSpeedKeep = 0.1;
	constexpr FTimedBoost SpinSlow = { 0.3, 1.5 };
	constexpr double SpinSeconds = 1.0;
	constexpr double SpinLockSeconds = 0.8;
	constexpr double SpinProtectSeconds = 1.8;
	constexpr FTimedBoost ExplodedSlow = { 0.2, 2.5 };
	constexpr double ExplodeLockSeconds = 1.2;
	constexpr double ExplodeProtectSeconds = 2.2;
	constexpr double ExplodeFlashSeconds = 0.5;

	// ---- Odds brackets (store.ts getPositionBasedItem:2003-2091), by race position 0 (first) .. 1 (last) ----
	constexpr double FrontBracketMaxRatio = 0.2;
	constexpr double MidBracketMaxRatio = 0.55;
	/** A mid-pack kart this far behind the leader draws from the back pool instead. */
	constexpr double MidBracketMaxTrailing = 0.55;
	/** A lone kart rolls as if mid-pack. */
	constexpr double SoloPositionRatio = 0.5;

	// ---- Boxes and bots ----
	/** Score gap to the leader at which a kart counts as fully trailing for the odds. */
	constexpr double TrailingScoreRange = 120.0;
	/** A bot uses its item after 1.2 to 3.7 s (kartPilot.ts:222-231). */
	constexpr double BotUseMinDelay = 1.2;
	constexpr double BotUseDelayRange = 2.5;

	// ---- Race order (store.ts) ----
	constexpr double ScorePerLap = 100.0;
	constexpr double FinishedScoreBonus = 100000.0;
}
