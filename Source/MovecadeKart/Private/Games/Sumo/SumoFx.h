#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"

/**
 * Sumo Bumpers' particle recipes for UKartFxSubsystem: pure functions that append emits (or return a stream's
 * settings), so ASumoGame and ASumoArena only decide when, where and in which colours. Positions are UE cm.
 */
namespace MovecadeKart::Sumo::SumoFx
{
	/** A hit at least this strong (0..1) throws a shock ring even without a dash. */
	constexpr double ShockRingPower = 0.6;

	/**
	 * Two pucks meeting at At: sparks in both colours, a white flash, and a shock ring on the ring's surface for a
	 * dash or a hard hit. Power is the hit's strength normalised to 0..1; a dash throws twice the sparks.
	 */
	void HitBurst(const FVector& At, const FLinearColor& A, const FLinearColor& B, double Power, bool bDash, TArray<Fx::FKartFxEmit>& Out);
	/** A dash's launch: a ring in Color at RingCm and a puff of dust at PuffCm thrown back against Heading (radians). */
	void DashBurst(const FVector& RingCm, const FVector& PuffCm, double Heading, const FLinearColor& Color, TArray<Fx::FKartFxEmit>& Out);
	/** A puck meeting the sea: spray tinted toward its colour, foam, a ring on the water. */
	void SeaSplash(const FVector& SeaCm, const FLinearColor& Color, TArray<Fx::FKartFxEmit>& Out);
	/** A power-up taken: a burst of flares fading to its colour. */
	void PowerFlash(const FVector& At, const FLinearColor& Color, TArray<Fx::FKartFxEmit>& Out);
	/** A ring segment hitting the water (Scale ~1: more and bigger with it). */
	void BandSplash(const FVector& SeaCm, double Scale, TArray<Fx::FKartFxEmit>& Out);

	/** The dash's fire trail stream, thrown out the back (Back: unit, against the roll). */
	Fx::FKartFxEmit DashFire(const FLinearColor& Color, const FVector& Back);
	/** Sparks off the skirt at speed. */
	Fx::FKartFxEmit SkirtSparks(const FLinearColor& Color, const FVector& Back);
}
