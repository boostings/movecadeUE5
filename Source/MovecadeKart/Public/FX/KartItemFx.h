#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"

/**
 * The item world's VFX recipes as data, next to the race recipes in KartFx.h: what AKartItemPresenter emits for
 * each item event, the streams that follow projectiles and boxes, and the flash lights that go with the big
 * moments. Positions and velocities are UE world space (cm, cm/s); sizes, speeds and lives follow FKartFxEmit.
 * Everything here is pure presentation.
 */
namespace MovecadeKart::Fx
{
	/** A short point-light flash to go with a burst (AKartItemPresenter pools the lights). */
	struct FKartFxLight
	{
		FVector Position = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		double RadiusCm = 0.0;
		/** Candelas at the peak. */
		double PeakCandela = 0.0;
		double Seconds = 0.0;
	};

	/** The finish moment over a kart that crossed the line: confetti, a gold shock ring, two spark fountains and a
	 *  crown of twinkles. Returns the warm flash of light that goes with it. */
	MOVECADEKART_API FKartFxLight FinishCelebration(const FVector& KartCm, const FVector& Forward, const FVector& InheritCm, TArray<FKartFxEmit>& Out);

	/** A projectile hit, on top of ImpactBurst: a white-hot pop, shards and a ground ring in the colours of what hit.
	 *  Returns the quick light on the kart and the road. */
	MOVECADEKART_API FKartFxLight ProjectileHitFlash(const FVector& KartCm, double GroundZ, const FItemColors& Colors, const FVector& InheritCm, TArray<FKartFxEmit>& Out);

	/** The light under a bomb or sky-seeker fireball (BlastCentreCm is the fireball's centre). */
	MOVECADEKART_API FKartFxLight ExplosionLight(const FVector& BlastCentreCm, double RadiusMetres);

	/** Dizzy stars circling up off a kart that spun out. */
	MOVECADEKART_API void DizzyStars(const FVector& KartCm, const FVector& KartVelocityCm, TArray<FKartFxEmit>& Out);

	/** A box collected: cyan twinkles, sparks and a ring at the box (RingCm is on the road under it). */
	MOVECADEKART_API void BoxCollectBurst(const FVector& BoxCm, const FVector& RingCm, TArray<FKartFxEmit>& Out);

	/** A box coming back: a ring closing in under it and a poof of glass. */
	MOVECADEKART_API void BoxRespawnBurst(const FVector& BoxCm, TArray<FKartFxEmit>& Out);

	/** The idle twinkles over every visible box (a stream at BoxTwinkleRate). */
	MOVECADEKART_API const FKartFxEmit& BoxTwinkleStream();
	constexpr double BoxTwinkleRate = 5.0;

	/** An item thrown or dropped: a poof and a pinch of sparks in Tint at the thrower. */
	MOVECADEKART_API void ThrowPuff(const FVector& Cm, const FLinearColor& Tint, const FVector& ThrowerVelocityCm, TArray<FKartFxEmit>& Out);

	/** A projectile bouncing off a wall. */
	MOVECADEKART_API void BounceSparks(const FVector& GroundCm, TArray<FKartFxEmit>& Out);

	/** Where a lightning bolt strikes a kart: sparks, a ring, a flash and a small spin-out burst. */
	MOVECADEKART_API void LightningStrikeBurst(const FVector& KartCm, TArray<FKartFxEmit>& Out);

	/** A splash of ink bursting over a kart. */
	MOVECADEKART_API void InkSplash(const FVector& KartCm, const FVector& KartVelocityCm, TArray<FKartFxEmit>& Out);

	/** The prism (or rocket) kicking in: a burst of twinkles (or flame) and a ring. */
	MOVECADEKART_API void PowerUpBurst(const FVector& KartCm, bool bRocket, const FVector& KartVelocityCm, TArray<FKartFxEmit>& Out);

	/** A used item bursting over its kart at the end of its pop-up. */
	MOVECADEKART_API void PopupBurst(const FVector& Cm, const FItemColors& Colors, const FVector& InheritCm, TArray<FKartFxEmit>& Out);

	/** Trail streams behind a flying projectile (rates in particles per second). */
	MOVECADEKART_API FKartFxEmit BombFuseSmoke();
	constexpr double BombFuseSmokeRate = 16.0;
	/** A dense stream of soft glows in the item colour, hot white near the item, left behind as it flies. */
	MOVECADEKART_API FKartFxEmit ProjectileGlowTrail(const FItemColors& Colors, const FVector& VelocityCm, bool bSkySeeker);
	constexpr double ProjectileGlowTrailRate = 60.0;
	/** The bomb's lit fuse. */
	MOVECADEKART_API FKartFxEmit BombFuseSparks(const FVector& VelocityCm);
	constexpr double BombFuseSparksRate = 45.0;
	/** Sparks shed by a seeker (faster for the sky seeker, see ProjectileSparksRate). */
	MOVECADEKART_API FKartFxEmit ProjectileSparks(const FLinearColor& Color, const FVector& VelocityCm);
	MOVECADEKART_API double ProjectileSparksRate(bool bSkySeeker);
}
