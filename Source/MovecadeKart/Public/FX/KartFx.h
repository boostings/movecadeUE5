#pragma once

#include "CoreMinimal.h"
#include "Sim/KartItems.h"

/**
 * The race VFX recipes as data. They start from the web build's (legacy/Experience/kart/engine/misc/KartFxLayer.tsx,
 * games/kart/scene/kartFxBus.ts, kartTrails.ts, constants.ts drift levels) and add the port's AAA pass (toon puffs,
 * a stylised explosion, confetti, item colours). Units are the web's where they come from the web: metres, m/s,
 * seconds, three.js colours as sRGB hex; positions and directions are UE space (cm, Z up). UKartFxSubsystem turns an
 * FKartFxEmit into Niagara particles.
 *
 * The web draws every effect from pooled particle fields (ParticleField.tsx); the port keeps that model with one
 * Niagara system per field (Content/FX/Niagara/NS_FX_*), so each recipe below maps 1:1 onto an emit() call.
 * Everything here is pure presentation: nothing reads back into the sim.
 */
namespace MovecadeKart::Fx
{
	/** One Niagara system per field; gravity, drag and blending live in the asset (KartFxLayer.tsx:268-274). */
	enum class EField : uint8
	{
		Smoke,      // toon puffs, alpha blended, drag 1.6, gravity +0.6 (also dust and explosion fire)
		Fire,       // soft additive glow, drag 3, gravity +1.5
		Sparks,     // velocity-stretched streaks, additive, drag 1.2, gravity -9, stretch 1.4
		Ring,       // ground shock ring, additive
		Flare,      // star twinkles (box idle, collect, prism aura, flashes)
		Confetti,   // flipping paper in a party palette (finish line)
		SpeedLines, // local space, rides a camera
		Count
	};

	/** ParticleField.tsx ParticleEmitOptions. Position and Inherit are UE world space (cm, cm/s). */
	struct FKartFxEmit
	{
		EField Field = EField::Fire;
		FVector Position = FVector::ZeroVector;
		int32 Count = 1;
		/** Mean emission direction in UE axes (Z up). Defaults to straight up. */
		FVector Direction = FVector::UpVector;
		/** Cone half-angle, radians. */
		double Spread = 0.5;
		/** m/s. */
		FVector2D Speed = FVector2D(2.0, 2.0);
		/** Seconds. */
		FVector2D Life = FVector2D(0.6, 0.6);
		/** Metres. */
		FVector2D Size = FVector2D(0.12, 0.12);
		/** Size multiplier reached at the end of the life. */
		double SizeEnd = 0.2;
		FLinearColor Color = FLinearColor::White;
		FLinearColor ColorEnd = FLinearColor::White;
		/** Colour multiplier (values above 1 bloom). */
		double Intensity = 1.0;
		/** Start position scatter, metres. */
		double Jitter = 0.0;
		FVector InheritCm = FVector::ZeroVector;
	};

	/** kartFxBus.ts KART_FX_PROFILES. */
	enum class EImpact : uint8
	{
		ProjectileHit,
		Explosion,
		SpinOut,
		Wall,
		Bump,
	};

	struct FImpactProfile
	{
		double Shake = 0.0;  // metres
		double Flash = 0.0;
		FLinearColor FlashColor = FLinearColor::White;
		double Burst = 1.0;
	};

	MOVECADEKART_API const FImpactProfile& ImpactProfile(EImpact Kind);

	/** FColor::FromHex without building an FString: "rgb", "rrggbb" or "rrggbbaa", each optionally after one '#'.
	 *  Anything else is transparent black, and a non-hex digit reads as 0, exactly as FromHex does. */
	[[nodiscard]] MOVECADEKART_API FColor HexToColor(FStringView Value);

	/** sRGB hex ("ffe2a0") to linear. Allocation-free, so it is fine in per-frame code. */
	[[nodiscard]] MOVECADEKART_API FLinearColor Hex(FStringView Value);

	/**
	 * KartFxLayer.tsx onKartFx burst: sparks 34*burst, smoke 12*burst, and for heavy kinds (explosion,
	 * projectile-hit) fire 18*burst plus one ground ring. Center is the ground point under the event (UE);
	 * the burst sits 0.6 m above it. RingEndMetres overrides the ring's final size (9 explosion / 5 hit).
	 * Tint, if not transparent, colours the sparks' tails (the item that hit).
	 */
	MOVECADEKART_API void ImpactBurst(EImpact Kind, const FVector& GroundCm, double Intensity, TArray<FKartFxEmit>& Out,
		double RingEndMetres = -1.0, const FLinearColor& Tint = FLinearColor::Transparent);

	/**
	 * The stylised bomb / sky-seeker explosion around GroundCm with a blast radius in metres: a white flash, a
	 * billow of toon fire puffs that cool to red and erode, a column of dark smoke, flung debris sparks, a bloom
	 * core and a shock ring that reaches the blast radius (so the danger zone reads).
	 */
	MOVECADEKART_API void ExplosionBurst(const FVector& GroundCm, double RadiusMetres, TArray<FKartFxEmit>& Out);

	/** A small puff and a pinch of sparks in Color: an item that ran out (expired) or a box that blinked out. */
	MOVECADEKART_API void Poof(const FVector& Cm, const FLinearColor& Color, double Scale, TArray<FKartFxEmit>& Out);

	/** Dust puffs where a kart lands after a hop, scaled by Strength in [0, 1]. Dirt colours on dirt. */
	MOVECADEKART_API void LandingPuff(const FVector& GroundCm, const FVector& InheritCm, double Strength, bool bDirt, TArray<FKartFxEmit>& Out);

	/** A finish-line confetti cannon: two arcs of paper and a spray of twinkles over Cm. */
	MOVECADEKART_API void ConfettiBurst(const FVector& Cm, const FVector& Forward, TArray<FKartFxEmit>& Out);

	/** constants.ts drifts: level 1 blue #00ffff >= 1 s, 2 orange #FFA22B >= 3 s, 3 purple #b100ff >= 6 s. */
	struct FDriftLevel
	{
		int32 Level = 0;
		double Threshold = 0.0;
		FLinearColor Color = FLinearColor::White;
	};

	MOVECADEKART_API FDriftLevel DriftLevel(double DriftPower);

	/** The mini-turbo grade's colour from its label ("DRIFT_MINI", "..._SUPER", "..._ULTRA", "..._APEX..."). */
	MOVECADEKART_API FLinearColor DriftGradeColor(const FString& Label);

	/** ITEM_CONFIGS colours (MultiplayerItems.tsx:51-155) for the thrown items, and the port's for the rest. */
	struct FItemColors
	{
		FLinearColor Color;
		FLinearColor Inner;
		FLinearColor Emissive;
	};

	MOVECADEKART_API FItemColors ItemColors(EKartItem Item);

	/** kartTrails.ts particlesDue: whole particles due after Dt at Rate, carrying the fraction. */
	MOVECADEKART_API int32 ParticlesDue(double& Carry, double Rate, double Dt);

	/** kartTrails.ts kartContinuousFx thresholds. */
	constexpr double DriftSmokeMinSpeed = 8.0;
	constexpr double DustMinSpeed = 5.0;
	constexpr double BoostFlameRate = 70.0;   // per exhaust pipe
	constexpr double DriftSmokeRate = 26.0;   // per rear wheel
	constexpr double DustRate = 22.0;         // per rear wheel

	/** Speed lines fade in over this speed range (SU; top speed is 40, boosts reach 80+). */
	constexpr double SpeedLinesFrom = 38.0;
	constexpr double SpeedLinesFull = 60.0;
}
